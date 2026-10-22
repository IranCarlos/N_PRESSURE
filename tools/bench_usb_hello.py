#!/usr/bin/env python3
"""
Teste de bancada dos checkpoints da Fase 7.1 (HELLO/GET_STATUS) e 7.2
(AUTH_CHALLENGE/RESPONSE/LOGOUT) - ver Docs/dev/00-auditoria.md. Reimplementa
COBS + CRC32 em Python (poly 0x04C11DB7, init 0xFFFFFFFF, sem reflect/sem XOR
final) espelhando Core/Services/UsbProto/Src/cobs.c e usb_crc32.c byte a byte -
nao e' o "dispositivo virtual" completo da Fase 7.10 (tools/fake_device/, ainda
nao implementado), so' um script de bancada de uma ponta so'.

Uso:
    pip install pyserial
    python tools/bench_usb_hello.py --list
    python tools/bench_usb_hello.py --port COM5
    python tools/bench_usb_hello.py --port COM5 --cmd status
    python tools/bench_usb_hello.py --port COM5 --cmd auth --dev-key-hex <64 hex chars>

--dev-key-hex e' a MESMA chave de tools/gen_dev_key.py que voce colou em
dev_key_secret.h (NUNCA cole a chave aqui dentro do arquivo - so' na linha de
comando, que nao fica versionada).
"""
import argparse
import hmac
import hashlib
import struct
import sys

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("Falta o pyserial: pip install pyserial", file=sys.stderr)
    sys.exit(1)

# --- Apendice A: constantes do protocolo (ver usb_proto_frame.h/usb_proto_dispatch.h) ---
VERSION = 0x01
TYPE_REQ = 0x01
TYPE_RSP = 0x02

CMD_HELLO = 0x0001
CMD_GET_STATUS = 0x0002
CMD_AUTH_CHALLENGE = 0x0040
CMD_AUTH_RESPONSE = 0x0041
CMD_AUTH_LOGOUT = 0x0042

ROLE_NAMES = {0: "USER", 1: "DEV"}
SERIAL_FIELD_LEN = 12
NONCE_LEN = 16

RSP_STATUS_NAMES = {
    0: "OK", 1: "CMD_DESCONHECIDO", 2: "SEM_PERMISSAO", 3: "PARAM_INVALIDO",
    4: "OCUPADO", 5: "ESTADO_INVALIDO", 6: "ERRO_SD", 7: "FORA_DE_FAIXA",
    8: "NAO_AUTENTICADO",
}

DEVICE_ID_FIELD_LEN = 16
FW_VERSION_FIELD_LEN = 12

CRC32_POLY = 0x04C11DB7
CRC32_INIT = 0xFFFFFFFF
MASK32 = 0xFFFFFFFF


def usb_crc32(data: bytes) -> int:
    """Espelha UsbCrc32_Compute (usb_crc32.c): MSB-first, sem reflect, sem XOR final."""
    crc = CRC32_INIT
    for byte in data:
        crc ^= (byte << 24)
        for _ in range(8):
            if crc & 0x80000000:
                crc = ((crc << 1) ^ CRC32_POLY) & MASK32
            else:
                crc = (crc << 1) & MASK32
    return crc


def cobs_encode(data: bytes) -> bytes:
    """Espelha Cobs_Encode (cobs.c)."""
    out = bytearray()
    code_idx = 0
    out.append(0)  # placeholder do 1o byte de codigo
    code = 1
    for b in data:
        if b == 0x00:
            out[code_idx] = code
            code = 1
            code_idx = len(out)
            out.append(0)
        else:
            out.append(b)
            code += 1
            if code == 0xFF:
                out[code_idx] = code
                code = 1
                code_idx = len(out)
                out.append(0)
    out[code_idx] = code
    return bytes(out)


def cobs_decode(data: bytes) -> bytes:
    """Espelha Cobs_Decode (cobs.c)."""
    out = bytearray()
    read_idx = 0
    n = len(data)
    while read_idx < n:
        code = data[read_idx]
        if code == 0x00:
            raise ValueError("0x00 dentro do payload codificado (malformado)")
        if (read_idx + code > n) and (code != 1):
            raise ValueError("codigo COBS aponta alem do buffer")
        read_idx += 1
        for _ in range(1, code):
            out.append(data[read_idx])
            read_idx += 1
        if (code != 0xFF) and (read_idx != n):
            out.append(0x00)
    return bytes(out)


def build_request(seq: int, cmd: int, payload: bytes = b"") -> bytes:
    """VER(1) TYPE(1) SEQ(2 LE) CMD(2 LE) LEN(2 LE) PAYLOAD CRC32(4 LE)."""
    header = struct.pack("<BBHHH", VERSION, TYPE_REQ, seq, cmd, len(payload))
    body = header + payload
    crc = usb_crc32(body)
    return body + struct.pack("<I", crc)


def parse_response(frame: bytes):
    if len(frame) < 8 + 4:
        raise ValueError(f"frame curto demais ({len(frame)} bytes)")
    ver, typ, seq, cmd, length = struct.unpack("<BBHHH", frame[:8])
    payload = frame[8:8 + length]
    if len(frame) != 8 + length + 4:
        raise ValueError(f"LEN inconsistente: declarado={length}, frame={len(frame)}")
    (crc_recv,) = struct.unpack("<I", frame[8 + length:8 + length + 4])
    crc_calc = usb_crc32(frame[:8 + length])
    if crc_recv != crc_calc:
        raise ValueError(f"CRC invalido: recebido={crc_recv:#010x}, calculado={crc_calc:#010x}")
    return {"ver": ver, "type": typ, "seq": seq, "cmd": cmd, "len": length, "payload": payload}


def send_and_receive(ser: serial.Serial, seq: int, cmd: int, payload: bytes = b"", timeout_s: float = 2.0) -> dict:
    req = build_request(seq, cmd, payload)
    wire = cobs_encode(req) + b"\x00"
    ser.write(wire)

    ser.timeout = timeout_s
    raw = ser.read_until(b"\x00")
    if not raw or raw[-1] != 0x00:
        raise TimeoutError("sem resposta (timeout) ou frame sem delimitador 0x00")
    decoded = cobs_decode(raw[:-1])
    return parse_response(decoded)


def parse_hello(payload: bytes) -> dict:
    role_idx = 2 + DEVICE_ID_FIELD_LEN + FW_VERSION_FIELD_LEN
    return {
        "status": payload[0],
        "protocol_version": payload[1],
        "device_id": payload[2:2 + DEVICE_ID_FIELD_LEN].rstrip(b"\x00").decode("ascii", "replace"),
        "fw_version": payload[2 + DEVICE_ID_FIELD_LEN:role_idx].rstrip(b"\x00").decode("ascii", "replace"),
        "role": payload[role_idx],
        "serial": payload[role_idx + 1:role_idx + 1 + SERIAL_FIELD_LEN],
    }


def fmt_hello(payload: bytes) -> str:
    status = payload[0]
    if status != 0:
        return f"status={RSP_STATUS_NAMES.get(status, status)}"
    h = parse_hello(payload)
    return (f"status=OK protocol_version={h['protocol_version']} device_id={h['device_id']!r} "
            f"fw_version={h['fw_version']!r} role={ROLE_NAMES.get(h['role'], h['role'])} "
            f"serial={h['serial'].hex()}")


def fmt_get_status(payload: bytes) -> str:
    status = payload[0]
    if status != 0:
        return f"status={RSP_STATUS_NAMES.get(status, status)}"
    system_state = payload[1]
    pressure_mmhg, setpoint_mmhg = struct.unpack("<ff", payload[2:10])
    rtc_valid, sd_mounted, active_error = payload[10], payload[11], payload[12]
    (cycle_number,) = struct.unpack("<I", payload[13:17])
    return (f"status=OK system_state={system_state} pressure_mmhg={pressure_mmhg:.1f} "
            f"setpoint_mmhg={setpoint_mmhg:.1f} rtc_valid={rtc_valid} sd_mounted={sd_mounted} "
            f"active_error={active_error} cycle_number={cycle_number}")


def run_auth_flow(ser: serial.Serial, seq: int, dev_key: bytes, timeout_s: float) -> bool:
    """CHALLENGE -> RESPONSE (eleva a DEV) -> HELLO (confirma role) -> LOGOUT."""
    print(f"--> HELLO (seq={seq})")
    rsp = send_and_receive(ser, seq, CMD_HELLO, timeout_s=timeout_s)
    hello = parse_hello(rsp["payload"])
    if hello["status"] != 0:
        print(f"FALHA: HELLO status={RSP_STATUS_NAMES.get(hello['status'])}", file=sys.stderr)
        return False
    serial_bytes = hello["serial"]
    print(f"<-- serial={serial_bytes.hex()} role={ROLE_NAMES.get(hello['role'])}")
    seq += 1

    print(f"--> AUTH_CHALLENGE (seq={seq})")
    rsp = send_and_receive(ser, seq, CMD_AUTH_CHALLENGE, timeout_s=timeout_s)
    status = rsp["payload"][0]
    if status != 0:
        print(f"FALHA: AUTH_CHALLENGE status={RSP_STATUS_NAMES.get(status)}", file=sys.stderr)
        return False
    nonce = rsp["payload"][1:1 + NONCE_LEN]
    print(f"<-- nonce={nonce.hex()}")
    seq += 1

    mac = hmac.new(dev_key, nonce + serial_bytes, hashlib.sha256).digest()
    print(f"--> AUTH_RESPONSE (seq={seq}) hmac={mac.hex()}")
    rsp = send_and_receive(ser, seq, CMD_AUTH_RESPONSE, payload=mac, timeout_s=timeout_s)
    status, role = rsp["payload"][0], rsp["payload"][1]
    print(f"<-- status={RSP_STATUS_NAMES.get(status)} role={ROLE_NAMES.get(role, role)}")
    if status != 0 or role != 1:
        print("FALHA: AUTH_RESPONSE nao elevou a DEV (chave errada?)", file=sys.stderr)
        return False
    seq += 1

    print(f"--> AUTH_LOGOUT (seq={seq})")
    rsp = send_and_receive(ser, seq, CMD_AUTH_LOGOUT, timeout_s=timeout_s)
    status, role = rsp["payload"][0], rsp["payload"][1]
    print(f"<-- status={RSP_STATUS_NAMES.get(status)} role={ROLE_NAMES.get(role, role)}")
    return status == 0 and role == 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", help="Porta serial (ex.: COM5)")
    parser.add_argument("--list", action="store_true", help="Lista as portas seriais disponiveis e sai")
    parser.add_argument("--cmd", choices=["hello", "status", "auth", "both"], default="both")
    parser.add_argument("--dev-key-hex", help="Chave DEV (64 hex chars / 32 bytes) - exigido por --cmd auth")
    parser.add_argument("--baud", type=int, default=115200, help="Ignorado pelo CDC, mas o pyserial exige um valor")
    parser.add_argument("--timeout", type=float, default=2.0, help="Timeout de leitura em segundos")
    args = parser.parse_args()

    if args.cmd == "auth" and not args.dev_key_hex:
        print("--cmd auth exige --dev-key-hex <chave gerada por tools/gen_dev_key.py>", file=sys.stderr)
        sys.exit(1)

    if args.list or not args.port:
        ports = list(serial.tools.list_ports.comports())
        if not ports:
            print("Nenhuma porta serial encontrada.")
        else:
            print("Portas disponiveis:")
            for p in ports:
                print(f"  {p.device}  {p.description}")
        if not args.port:
            print("\nUse --port <porta> para rodar o teste.")
            return
        if args.list:
            return

    try:
        ser = serial.Serial(args.port, args.baud, timeout=args.timeout)
    except serial.SerialException as exc:
        print(f"Erro abrindo {args.port}: {exc}", file=sys.stderr)
        sys.exit(1)

    seq = 1
    ok = True
    with ser:
        if args.cmd in ("hello", "both"):
            print(f"--> HELLO (seq={seq})")
            try:
                rsp = send_and_receive(ser, seq, CMD_HELLO, timeout_s=args.timeout)
                print(f"<-- {fmt_hello(rsp['payload'])}")
            except (ValueError, TimeoutError) as exc:
                print(f"FALHA: {exc}", file=sys.stderr)
                ok = False
            seq += 1

        if args.cmd in ("status", "both"):
            print(f"--> GET_STATUS (seq={seq})")
            try:
                rsp = send_and_receive(ser, seq, CMD_GET_STATUS, timeout_s=args.timeout)
                print(f"<-- {fmt_get_status(rsp['payload'])}")
            except (ValueError, TimeoutError) as exc:
                print(f"FALHA: {exc}", file=sys.stderr)
                ok = False
            seq += 1

        if args.cmd == "auth":
            try:
                ok = run_auth_flow(ser, seq, bytes.fromhex(args.dev_key_hex), args.timeout)
            except (ValueError, TimeoutError) as exc:
                print(f"FALHA: {exc}", file=sys.stderr)
                ok = False

    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
