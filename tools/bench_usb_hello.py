#!/usr/bin/env python3
"""
Teste de bancada dos checkpoints da Fase 7.1 (HELLO/GET_STATUS), 7.2
(AUTH_CHALLENGE/RESPONSE/LOGOUT) e 7.3 (SET_SETPOINT/SET_DATETIME/START_TEST/
ABORT_TEST/SET_OPERATOR) - ver Docs/dev/00-auditoria.md. Reimplementa
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
    python tools/bench_usb_hello.py --port COM5 --cmd setpoint --value 120
    python tools/bench_usb_hello.py --port COM5 --cmd datetime          (hora atual do PC)
    python tools/bench_usb_hello.py --port COM5 --cmd operator --operator 3 --participant 37
    python tools/bench_usb_hello.py --port COM5 --cmd start
    python tools/bench_usb_hello.py --port COM5 --cmd abort

--dev-key-hex e' a MESMA chave de tools/gen_dev_key.py que voce colou em
dev_key_secret.h (NUNCA cole a chave aqui dentro do arquivo - so' na linha de
comando, que nao fica versionada).
"""
import argparse
import datetime
import hmac
import hashlib
import struct
import sys
import time

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
TYPE_EVT = 0x03
TYPE_STREAM = 0x04

CMD_HELLO = 0x0001
CMD_GET_STATUS = 0x0002
CMD_SET_SETPOINT = 0x0003
CMD_SET_DATETIME = 0x0004
CMD_START_TEST = 0x0005
CMD_ABORT_TEST = 0x0006
CMD_SET_OPERATOR = 0x0007
CMD_AUTH_CHALLENGE = 0x0040
CMD_AUTH_RESPONSE = 0x0041
CMD_AUTH_LOGOUT = 0x0042
CMD_STREAM_START = 0x0010
CMD_STREAM_STOP = 0x0011
CMD_GET_LOG_TAIL = 0x0030
CMD_FS_LIST = 0x0020
CMD_FS_STAT = 0x0021
CMD_FS_READ = 0x0022
CMD_FS_CRC = 0x0023
CMD_PARAM_LIST = 0x0100
CMD_PARAM_GET = 0x0101
CMD_PARAM_SET = 0x0102
CMD_PARAM_COMMIT = 0x0103
CMD_PARAM_REVERT = 0x0104
CMD_MANUAL_ENTER = 0x0110
CMD_MANUAL_EXIT = 0x0111
CMD_MANUAL_SET = 0x0112
CMD_MANUAL_HEARTBEAT = 0x0113
STATE_MANUAL = 9  # SystemState_t (system_types.h)
PARAM_GROUP_NAMES = {0: "PID", 1: "Estabilizacao", 2: "Valvula de alivio", 3: "Aproximacao",
                     4: "Seguranca", 5: "Vazamento", 6: "Limites"}
FS_READ_MAX = 1016

# Fase 7.4: eventos assincronos (usb_proto_events.h)
EVT_NAMES = {0x0301: "ESTADO_MUDOU", 0x0302: "ENSAIO_CONCLUIDO", 0x0303: "ERRO",
             0x0304: "SD", 0x0305: "AUTH_EXPIROU"}
STATE_NAMES = {0: "BOOT", 1: "STANDBY", 2: "INFLANDO", 3: "ESTABILIZANDO", 4: "MANUTENCAO",
               5: "DESINFLANDO", 6: "TESTE_VAZAMENTO", 7: "AJUSTES", 8: "ERRO", 9: "MANUAL_PC"}
LOG_LEVEL_NAMES = {0: "EVT", 1: "ERR", 2: "AUDIT"}

ROLE_NAMES = {0: "USER", 1: "DEV"}
SERIAL_FIELD_LEN = 12
NONCE_LEN = 16

RSP_STATUS_NAMES = {
    0: "OK", 1: "CMD_DESCONHECIDO", 2: "SEM_PERMISSAO", 3: "PARAM_INVALIDO",
    4: "OCUPADO", 5: "ESTADO_INVALIDO", 6: "ERRO_SD", 7: "FORA_DE_FAIXA",
    8: "NAO_AUTENTICADO", 9: "NAO_ENCONTRADO", 10: "CONFLITO",
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


def read_frame(ser: serial.Serial, timeout_s: float):
    """Le um frame qualquer (RSP/EVT/STREAM); None em timeout."""
    ser.timeout = timeout_s
    raw = ser.read_until(b"\x00")
    if not raw or raw[-1] != 0x00:
        return None
    return parse_response(cobs_decode(raw[:-1]))


def send_and_receive(ser: serial.Serial, seq: int, cmd: int, payload: bytes = b"", timeout_s: float = 2.0) -> dict:
    """Envia um REQ e espera a RSP com o mesmo seq. EVT/STREAM que chegarem
    no meio (Fase 7.4) sao mostrados e ignorados."""
    req = build_request(seq, cmd, payload)
    wire = cobs_encode(req) + b"\x00"
    ser.write(wire)

    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        frame = read_frame(ser, max(0.05, deadline - time.monotonic()))
        if frame is None:
            break
        if frame["type"] == TYPE_RSP and frame["seq"] == seq:
            return frame
        if frame["type"] == TYPE_EVT:
            print(f"    (evento no meio) {fmt_event(frame)}")
    raise TimeoutError("sem resposta (timeout) ou frame sem delimitador 0x00")


def fmt_event(frame: dict) -> str:
    p = frame["payload"]
    name = EVT_NAMES.get(frame["cmd"], f"0x{frame['cmd']:04x}")
    if frame["cmd"] == 0x0301 and len(p) >= 2:
        return f"EVT {name}: {STATE_NAMES.get(p[0], p[0])} -> {STATE_NAMES.get(p[1], p[1])}"
    if frame["cmd"] == 0x0302 and len(p) >= 27:
        sp, peak, resid = struct.unpack("<fff", p[0:12])
        t_inf, t_plat, t_def = struct.unpack("<III", p[12:24])
        return (f"EVT {name}: alvo={sp:.1f} pico={peak:.1f} residual={resid:.1f} mmHg "
                f"inflar={t_inf} ms plato={t_plat} ms desinflar={t_def} ms "
                f"abortado={p[24]} falha={p[25]} NC{p[26]:02d}")
    if frame["cmd"] == 0x0303 and len(p) >= 1:
        return f"EVT {name}: " + (f"E{p[0]:03d}" if p[0] else "limpo")
    if frame["cmd"] == 0x0304 and len(p) >= 1:
        return f"EVT {name}: " + ("montado" if p[0] else "REMOVIDO")
    return f"EVT {name} payload={p.hex()}"


def parse_stream(payload: bytes):
    n, dropped = struct.unpack("<HI", payload[0:6])
    samples = []
    for i in range(n):
        off = 6 + 16 * i
        t_ms, p, sp = struct.unpack("<Iff", payload[off:off + 12])
        st, pump, valves, err = payload[off + 12:off + 16]
        samples.append((t_ms, p, sp, st, pump, valves, err))
    return dropped, samples


def run_stream(ser: serial.Serial, seq: int, rate_hz: int, seconds: float, timeout_s: float) -> bool:
    """STREAM_START, recebe por `seconds`, mede a taxa real e mostra os eventos."""
    print(f"--> STREAM_START {rate_hz} Hz (seq={seq})")
    rsp = send_and_receive(ser, seq, CMD_STREAM_START, struct.pack("<H", rate_hz), timeout_s)
    status = rsp["payload"][0]
    if status != 0:
        print(f"<-- status={RSP_STATUS_NAMES.get(status, status)}")
        return False
    (eff,) = struct.unpack("<H", rsp["payload"][1:3])
    print(f"<-- status=OK taxa efetiva={eff} Hz")

    total = 0
    frames = 0
    dropped = 0
    first_t = last_t = None
    gaps = 0
    last = None
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        frame = read_frame(ser, 0.5)
        if frame is None:
            continue
        if frame["type"] == TYPE_EVT:
            print(f"    {fmt_event(frame)}")
            continue
        if frame["type"] != TYPE_STREAM:
            continue
        frames += 1
        dropped, samples = parse_stream(frame["payload"])
        for s in samples:
            if last_t is not None and s[0] - last_t > 2 * (1000 // eff):
                gaps += 1  # buraco maior que 2 periodos
            if first_t is None:
                first_t = s[0]
            last_t = s[0]
            last = s
        total += len(samples)
        if frames % max(1, eff // 2) == 0 and last is not None:
            t_ms, p, sp, st, pump, valves, err = last
            print(f"    t={t_ms} ms p={p:6.1f} sp={sp:5.1f} {STATE_NAMES.get(st, st):<13} "
                  f"bomba={pump:3d}% V1={valves & 1} V2={(valves >> 1) & 1} V3={(valves >> 2) & 1} "
                  f"erro={err}")

    seq += 1
    print(f"--> STREAM_STOP (seq={seq})")
    rsp = send_and_receive(ser, seq, CMD_STREAM_STOP, b"", timeout_s)
    print(f"<-- status={RSP_STATUS_NAMES.get(rsp['payload'][0])}")
    span_s = ((last_t - first_t) / 1000.0) if (first_t is not None and last_t != first_t) else 0.0
    real = (total - 1) / span_s if span_s > 0 else 0.0
    print(f"Resumo: {total} amostras em {frames} frames, taxa medida {real:.1f} Hz "
          f"(pedida {rate_hz}), descartadas no equipamento={dropped}, buracos={gaps}")
    return total > 0


def run_log_tail(ser: serial.Serial, seq: int, count: int, timeout_s: float) -> bool:
    print(f"--> GET_LOG_TAIL {count} (seq={seq})")
    rsp = send_and_receive(ser, seq, CMD_GET_LOG_TAIL, bytes([count, 0]), timeout_s)
    p = rsp["payload"]
    if p[0] != 0:
        print(f"<-- status={RSP_STATUS_NAMES.get(p[0], p[0])}")
        return False
    n = p[1]
    print(f"<-- {n} linha(s), da mais recente para a mais antiga:")
    off = 2
    for _ in range(n):
        (lseq,) = struct.unpack("<I", p[off:off + 4])
        (year,) = struct.unpack("<H", p[off + 4:off + 6])
        mo, d, h, mi, s = p[off + 6:off + 11]
        lvl = p[off + 11]
        code = p[off + 12:off + 17].rstrip(b"\x00").decode("ascii", "replace")
        mlen = p[off + 17]
        msg = p[off + 18:off + 18 + mlen].decode("ascii", "replace")
        off += 18 + mlen
        print(f"    #{lseq:<5} {d:02d}/{mo:02d}/{year} {h:02d}:{mi:02d}:{s:02d} "
              f"{LOG_LEVEL_NAMES.get(lvl, lvl):<5} {code:<5} {msg}")
    return True


def fat_datetime(fdate: int, ftime: int) -> str:
    if fdate == 0:
        return "-"
    return (f"{fdate & 0x1F:02d}/{(fdate >> 5) & 0x0F:02d}/{1980 + (fdate >> 9)} "
            f"{ftime >> 11:02d}:{(ftime >> 5) & 0x3F:02d}:{(ftime & 0x1F) * 2:02d}")


def run_ls(ser: serial.Serial, seq: int, path: str, timeout_s: float) -> bool:
    """FS_LIST com paginacao ate' acabar o diretorio."""
    start = 0
    total = 0
    while True:
        rsp = send_and_receive(ser, seq, CMD_FS_LIST, struct.pack("<H", start) + path.encode("ascii"), timeout_s)
        seq += 1
        p = rsp["payload"]
        if p[0] != 0:
            print(f"<-- {path}: status={RSP_STATUS_NAMES.get(p[0], p[0])}")
            return False
        n, more = p[1], p[2]
        off = 3
        for _ in range(n):
            is_dir = p[off]
            (size,) = struct.unpack("<I", p[off + 1:off + 5])
            fdate, ftime = struct.unpack("<HH", p[off + 5:off + 9])
            nlen = p[off + 9]
            name = p[off + 10:off + 10 + nlen].decode("ascii", "replace")
            off += 10 + nlen
            kind = "<DIR>" if is_dir else f"{size:>10}"
            print(f"    {kind:>10}  {fat_datetime(fdate, ftime)}  {name}")
        total += n
        if not more or n == 0:
            break
        start += n
    print(f"<-- {total} entrada(s) em {path}")
    return True


def run_stat(ser: serial.Serial, seq: int, path: str, timeout_s: float) -> bool:
    p = send_and_receive(ser, seq, CMD_FS_STAT, path.encode("ascii"), timeout_s)["payload"]
    if p[0] != 0:
        print(f"<-- {path}: status={RSP_STATUS_NAMES.get(p[0], p[0])}")
        return False
    (size,) = struct.unpack("<I", p[2:6])
    fdate, ftime = struct.unpack("<HH", p[6:10])
    print(f"<-- {path}: {'diretorio' if p[1] else f'{size} bytes'}, {fat_datetime(fdate, ftime)}")
    return True


def run_get(ser: serial.Serial, seq: int, path: str, out_file: str, timeout_s: float) -> bool:
    """Baixa o arquivo em blocos FS_READ, mede a vazao e confere com FS_CRC."""
    data = bytearray()
    t0 = time.monotonic()
    while True:
        req = struct.pack("<IH", len(data), FS_READ_MAX) + path.encode("ascii")
        p = send_and_receive(ser, seq, CMD_FS_READ, req, timeout_s)["payload"]
        seq += 1
        if p[0] != 0:
            print(f"<-- FS_READ offset={len(data)}: status={RSP_STATUS_NAMES.get(p[0], p[0])}")
            return False
        (off,) = struct.unpack("<I", p[1:5])
        (n,) = struct.unpack("<H", p[5:7])
        if off != len(data):
            print(f"FALHA: offset devolvido {off} != pedido {len(data)}", file=sys.stderr)
            return False
        data += p[7:7 + n]
        if n < FS_READ_MAX:
            break
    dt = time.monotonic() - t0
    with open(out_file, "wb") as f:
        f.write(data)
    rate = (len(data) / 1024.0) / dt if dt > 0 else 0.0
    print(f"<-- {len(data)} bytes em {dt:.2f} s ({rate:.0f} kB/s) -> {out_file}")

    p = send_and_receive(ser, seq, CMD_FS_CRC, path.encode("ascii"), max(timeout_s, 20.0))["payload"]
    if p[0] != 0:
        print(f"<-- FS_CRC: status={RSP_STATUS_NAMES.get(p[0], p[0])}")
        return False
    size, crc = struct.unpack("<II", p[1:9])
    sha_dev = p[9:41]
    sha_local = hashlib.sha256(bytes(data)).digest()
    crc_local = usb_crc32(bytes(data))
    ok = (size == len(data)) and (sha_dev == sha_local) and (crc == crc_local)
    print(f"<-- FS_CRC: tamanho={size} crc32={crc:#010x} sha256={sha_dev.hex()}")
    print("INTEGRO: copia identica ao arquivo do cartao" if ok else
          f"DIVERGENTE: local tamanho={len(data)} crc32={crc_local:#010x} sha256={sha_local.hex()}")
    return ok


def run_listen(ser: serial.Serial, seq: int, seconds: float, timeout_s: float) -> bool:
    """Manda um HELLO (ativa os eventos nesta conexao) e so' escuta."""
    send_and_receive(ser, seq, CMD_HELLO, b"", timeout_s)
    print(f"Escutando eventos por {seconds:.0f} s (mexa no equipamento: pedal, Ajustes, SD)...")
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        frame = read_frame(ser, 0.5)
        if frame is not None and frame["type"] == TYPE_EVT:
            print(f"    {time.strftime('%H:%M:%S')} {fmt_event(frame)}")
    return True


def param_list_all(ser: serial.Serial, seq: int, timeout_s: float):
    """PARAM_LIST paginado ate' o fim. Devolve (lista, alterado_nao_salvo) ou (None, status)."""
    params = []
    dirty = 0
    start = 0
    while True:
        rsp = send_and_receive(ser, seq, CMD_PARAM_LIST, struct.pack("<H", start), timeout_s)
        seq += 1
        p = rsp["payload"]
        if p[0] != 0:
            return None, p[0]
        total, dirty, n = struct.unpack("<HBB", p[1:5])
        i = 5
        for _ in range(n):
            pid, typ, grp, vmin, vmax, vdef, val = struct.unpack("<HBBffff", p[i:i + 20])
            i += 20
            nl = p[i]
            name = p[i + 1:i + 1 + nl].decode("ascii", "replace")
            i += 1 + nl
            ul = p[i]
            unit = p[i + 1:i + 1 + ul].decode("ascii", "replace")
            i += 1 + ul
            params.append({"id": pid, "type": typ, "group": grp, "min": vmin, "max": vmax,
                           "def": vdef, "value": val, "name": name, "unit": unit})
        start += n
        if n == 0 or start >= total:
            return params, dirty


def fmt_param_value(p: dict, v: float) -> str:
    return f"{int(v)}" if p["type"] == 1 else f"{v:.6g}"


def run_params(ser: serial.Serial, seq: int, timeout_s: float) -> bool:
    print(f"--> PARAM_LIST (seq={seq})")
    params, dirty = param_list_all(ser, seq, timeout_s)
    if params is None:
        print(f"<-- status={RSP_STATUS_NAMES.get(dirty, dirty)}")
        return False
    group = None
    for p in params:
        if p["group"] != group:
            group = p["group"]
            print(f"\n  [{PARAM_GROUP_NAMES.get(group, group)}]")
        mark = "*" if p["value"] != p["def"] else " "
        print(f"  {mark} {p['id']:#06x} {p['name']:<22} = {fmt_param_value(p, p['value']):>10} {p['unit']:<8}"
              f" fabrica={fmt_param_value(p, p['def']):<8} faixa=[{fmt_param_value(p, p['min'])}, "
              f"{fmt_param_value(p, p['max'])}]")
    print(f"\n{len(params)} parametros ('*' = diferente do de fabrica). "
          f"{'HA ALTERACOES NAO SALVAS (use --cmd pcommit)' if dirty else 'Tudo salvo.'}")
    return True


def find_param(ser: serial.Serial, seq: int, key: str, timeout_s: float):
    """Aceita nome ('pid.kp') ou id ('0x0101')."""
    params, _ = param_list_all(ser, seq, timeout_s)
    if params is None:
        return None
    for p in params:
        if p["name"] == key or (key.lower().startswith("0x") and p["id"] == int(key, 16)):
            return p
    return None


def run_pset(ser: serial.Serial, seq: int, key: str, value: float, timeout_s: float) -> bool:
    p = find_param(ser, seq, key, timeout_s)
    seq += 10
    if p is None:
        print(f"FALHA: parametro {key!r} nao existe (ou sessao nao e' DEV)", file=sys.stderr)
        return False
    print(f"--> PARAM_SET {p['name']} = {value} (era {fmt_param_value(p, p['value'])}) (seq={seq})")
    rsp = send_and_receive(ser, seq, CMD_PARAM_SET, struct.pack("<Hf", p["id"], value), timeout_s)
    st = rsp["payload"][0]
    now = struct.unpack("<f", rsp["payload"][1:5])[0] if len(rsp["payload"]) >= 5 else None
    print(f"<-- status={RSP_STATUS_NAMES.get(st, st)} valor atual="
          f"{fmt_param_value(p, now) if now is not None else '-'}")
    if st == 0:
        print("    (vale ja' para o controle; NAO salvo - --cmd pcommit para gravar)")
    return st == 0


def run_pcommit(ser: serial.Serial, seq: int, timeout_s: float) -> bool:
    print(f"--> PARAM_COMMIT (seq={seq})")
    rsp = send_and_receive(ser, seq, CMD_PARAM_COMMIT, b"", timeout_s)
    st = rsp["payload"][0]
    if st == 0:
        print(f"<-- status=OK gravado na EEPROM (seq {struct.unpack('<I', rsp['payload'][1:5])[0]})")
        return True
    print(f"<-- status={RSP_STATUS_NAMES.get(st, st)}")
    return False


def run_prevert(ser: serial.Serial, seq: int, factory: bool, timeout_s: float) -> bool:
    print(f"--> PARAM_REVERT {'(fabrica)' if factory else '(descartar nao salvos)'} (seq={seq})")
    rsp = send_and_receive(ser, seq, CMD_PARAM_REVERT, bytes([1 if factory else 0]), timeout_s)
    st = rsp["payload"][0]
    print(f"<-- status={RSP_STATUS_NAMES.get(st, st)}")
    return st == 0


def parse_valves(text: str) -> int:
    """'V1,V3' / 'v2' / '' / 'nenhuma' -> mascara (bit0=V1, bit1=V2, bit2=V3)."""
    mask = 0
    for part in text.replace(" ", "").upper().split(","):
        if part in ("", "NENHUMA", "0"):
            continue
        if part not in ("V1", "V2", "V3"):
            raise ValueError(f"valvula invalida: {part!r} (use V1, V2, V3)")
        mask |= 1 << (int(part[1]) - 1)
    return mask


def get_state_pressure(ser: serial.Serial, seq: int, timeout_s: float):
    p = send_and_receive(ser, seq, CMD_GET_STATUS, b"", timeout_s)["payload"]
    return p[1], struct.unpack("<f", p[2:6])[0]


def run_manual(ser: serial.Serial, seq: int, pump_pct: int, valves: int, seconds: float,
               heartbeat: bool, timeout_s: float) -> bool:
    """MANUAL_ENTER -> MANUAL_SET a 10 Hz por `seconds` -> MANUAL_EXIT.
    Sem heartbeat: manda UM MANUAL_SET e para; o equipamento tem que sair
    sozinho do manual em ~300 ms (mede o tempo pelo GET_STATUS)."""
    names = ",".join(f"V{i + 1}" for i in range(3) if valves & (1 << i)) or "nenhuma"
    print(f"--> MANUAL_ENTER (seq={seq})")
    st = send_and_receive(ser, seq, CMD_MANUAL_ENTER, b"", timeout_s)["payload"][0]
    seq += 1
    print(f"<-- status={RSP_STATUS_NAMES.get(st, st)}")
    if st != 0:
        return False

    print(f"--> MANUAL_SET bomba={pump_pct}% valvulas abertas={names}")
    if not heartbeat:
        t0 = time.monotonic()
        send_and_receive(ser, seq, CMD_MANUAL_SET, bytes([pump_pct, valves]), timeout_s)
        seq += 1
        while time.monotonic() - t0 < 2.0:
            state, p = get_state_pressure(ser, seq, timeout_s)
            seq += 1
            if state != STATE_MANUAL:
                ms = (time.monotonic() - t0) * 1000.0
                print(f"<-- saiu do manual sozinho apos ~{ms:.0f} ms sem sinal de vida (esperado ~300) "
                      f"estado={state} pressao={p:.1f} mmHg")
                return 250.0 <= ms <= 600.0
            time.sleep(0.02)
        print("FALHA: continuou em manual sem sinal de vida por 2 s", file=sys.stderr)
        return False

    end = time.monotonic() + seconds
    next_print = 0.0
    ok = True
    while time.monotonic() < end:
        st = send_and_receive(ser, seq, CMD_MANUAL_SET, bytes([pump_pct, valves]), timeout_s)["payload"][0]
        seq += 1
        if st != 0:
            print(f"<-- MANUAL_SET status={RSP_STATUS_NAMES.get(st, st)} (manual encerrado no equipamento?)")
            ok = False
            break
        now = time.monotonic()
        if now >= next_print:
            state, p = get_state_pressure(ser, seq, timeout_s)
            seq += 1
            print(f"    pressao={p:6.1f} mmHg estado={state}")
            next_print = now + 0.5
        time.sleep(0.1)  # 10 Hz (sinal de vida)

    print(f"--> MANUAL_EXIT (seq={seq})")
    st = send_and_receive(ser, seq, CMD_MANUAL_EXIT, b"", timeout_s)["payload"][0]
    print(f"<-- status={RSP_STATUS_NAMES.get(st, st)}")
    return ok and st == 0


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


def run_auth_flow(ser: serial.Serial, seq: int, dev_key: bytes, timeout_s: float, logout: bool = True) -> bool:
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
    if not logout:
        return True  # continua em DEV (ex.: stream a 1 kHz logo em seguida)

    print(f"--> AUTH_LOGOUT (seq={seq})")
    rsp = send_and_receive(ser, seq, CMD_AUTH_LOGOUT, timeout_s=timeout_s)
    status, role = rsp["payload"][0], rsp["payload"][1]
    print(f"<-- status={RSP_STATUS_NAMES.get(status)} role={ROLE_NAMES.get(role, role)}")
    return status == 0 and role == 0


def build_control_request(args):
    """Fase 7.3: (nome, cmd, payload) do comando de controle pedido em --cmd."""
    if args.cmd == "setpoint":
        return "SET_SETPOINT", CMD_SET_SETPOINT, struct.pack("<f", args.value)
    if args.cmd == "datetime":
        now = datetime.datetime.now()
        payload = struct.pack("<HBBBBB", now.year, now.month, now.day, now.hour, now.minute, now.second)
        return f"SET_DATETIME {now:%d/%m/%Y %H:%M:%S}", CMD_SET_DATETIME, payload
    if args.cmd == "operator":
        return "SET_OPERATOR", CMD_SET_OPERATOR, struct.pack("<HH", args.operator, args.participant)
    if args.cmd == "start":
        return "START_TEST", CMD_START_TEST, b""
    return "ABORT_TEST", CMD_ABORT_TEST, b""


# Identificacao do equipamento na USB (USB_DEVICE/App/usbd_desc.c):
# VID/PID filtram as portas candidatas; o HELLO confirma que e' mesmo o
# equipamento (o par VID/PID padrao da ST tambem aparece em outras placas
# STM32 com CDC).
USB_VID = 0x0483
USB_PID = 0x5740
DEVICE_ID_PREFIX = "myPRESSURE"


def find_device(timeout_s: float = 0.5):
    """Devolve (porta, hello) do primeiro equipamento que responder, ou (None, None)."""
    for p in serial.tools.list_ports.comports():
        if (p.vid, p.pid) != (USB_VID, USB_PID):
            continue
        try:
            with serial.Serial(p.device, 115200, timeout=timeout_s) as ser:
                ser.reset_input_buffer()
                rsp = send_and_receive(ser, 1, CMD_HELLO, timeout_s=timeout_s)
                hello = parse_hello(rsp["payload"])
        except (serial.SerialException, ValueError, TimeoutError, IndexError):
            continue  # porta ocupada por outro programa ou nao fala o protocolo
        if hello["status"] == 0 and hello["device_id"].startswith(DEVICE_ID_PREFIX):
            return p.device, hello
    return None, None


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", default="auto",
                        help="Porta serial (ex.: COM5). Padrao: 'auto' = acha o equipamento sozinho")
    parser.add_argument("--list", action="store_true", help="Lista as portas seriais disponiveis e sai")
    parser.add_argument("--cmd", choices=["hello", "status", "auth", "both",
                                          "setpoint", "datetime", "operator", "start", "abort",
                                          "stream", "logtail", "listen", "ls", "stat", "get",
                                          "params", "pset", "pcommit", "prevert", "manual"], default="both")
    parser.add_argument("--pump", type=int, default=0, help="--cmd manual: potencia da bomba 0..100 %%")
    parser.add_argument("--valves", default="V1,V2", help="--cmd manual: valvulas ABERTAS, ex.: 'V1,V3' ou 'nenhuma'")
    parser.add_argument("--no-heartbeat", action="store_true",
                        help="--cmd manual: para o sinal de vida (testa a saida automatica em 300 ms)")
    parser.add_argument("--name", help="Parametro: nome ('pid.kp') ou id ('0x0101') (--cmd pset)")
    parser.add_argument("--factory", action="store_true", help="--cmd prevert: volta aos valores de fabrica")
    parser.add_argument("--path", default="/", help="Caminho no cartao (--cmd ls/stat/get)")
    parser.add_argument("--out", help="Arquivo local de destino (--cmd get; padrao = nome do arquivo)")
    parser.add_argument("--rate", type=int, default=50, help="Taxa do stream em Hz (--cmd stream; USER ate' 50)")
    parser.add_argument("--seconds", type=float, default=10.0, help="Duracao de --cmd stream/listen")
    parser.add_argument("--count", type=int, default=20, help="Linhas do log (--cmd logtail, 1..32)")
    parser.add_argument("--value", type=float, default=100.0, help="Setpoint em mmHg (--cmd setpoint)")
    parser.add_argument("--operator", type=int, default=1, help="ID do operador 1..9999 (--cmd operator)")
    parser.add_argument("--participant", type=int, default=1, help="ID do participante 1..9999 (--cmd operator)")
    parser.add_argument("--dev-key-hex", help="Chave DEV (64 hex chars / 32 bytes) - exigido por --cmd auth")
    parser.add_argument("--baud", type=int, default=115200, help="Ignorado pelo CDC, mas o pyserial exige um valor")
    parser.add_argument("--timeout", type=float, default=2.0, help="Timeout de leitura em segundos")
    args = parser.parse_args()

    if args.cmd == "auth" and not args.dev_key_hex:
        print("--cmd auth exige --dev-key-hex <chave gerada por tools/gen_dev_key.py>", file=sys.stderr)
        sys.exit(1)

    if args.list:
        ports = list(serial.tools.list_ports.comports())
        if not ports:
            print("Nenhuma porta serial encontrada.")
        else:
            print("Portas disponiveis:")
            for p in ports:
                vidpid = f"{p.vid:04X}:{p.pid:04X}" if p.vid is not None else "----:----"
                print(f"  {p.device}  {vidpid}  serial={p.serial_number}  {p.description}")
        return

    if args.port.lower() == "auto":
        port, hello = find_device()
        if port is None:
            print(f"Equipamento nao encontrado (VID:PID {USB_VID:04X}:{USB_PID:04X} + HELLO). "
                  "Conferir o cabo ou usar --port COMx.", file=sys.stderr)
            sys.exit(1)
        print(f"Equipamento encontrado em {port}: {hello['device_id']} FW {hello['fw_version']} "
              f"serial={hello['serial'].hex()}")
        args.port = port

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

        if args.cmd in ("setpoint", "datetime", "operator", "start", "abort"):
            name, cmd, payload = build_control_request(args)
            print(f"--> {name} (seq={seq})")
            try:
                rsp = send_and_receive(ser, seq, cmd, payload=payload, timeout_s=args.timeout)
                status = rsp["payload"][0]
                print(f"<-- status={RSP_STATUS_NAMES.get(status, status)}")
                if cmd == CMD_SET_SETPOINT and len(rsp["payload"]) >= 2 and rsp["payload"][1] == 1:
                    print("    ALERTA: setpoint acima do limite recomendado - margem de sobrepressao reduzida")
                ok = (status == 0)
                seq += 1
                # Estado logo depois, para conferir o efeito
                rsp = send_and_receive(ser, seq, CMD_GET_STATUS, timeout_s=args.timeout)
                print(f"<-- GET_STATUS {fmt_get_status(rsp['payload'])}")
            except (ValueError, TimeoutError) as exc:
                print(f"FALHA: {exc}", file=sys.stderr)
                ok = False

        if args.cmd in ("ls", "stat", "get"):
            try:
                if args.dev_key_hex:  # /config e /logs/audit exigem sessao DEV
                    ok = run_auth_flow(ser, seq, bytes.fromhex(args.dev_key_hex), args.timeout, logout=False)
                    seq += 10
                if args.cmd == "ls":
                    ok = ok and run_ls(ser, seq, args.path, args.timeout)
                elif args.cmd == "stat":
                    ok = ok and run_stat(ser, seq, args.path, args.timeout)
                else:
                    out = args.out or args.path.rstrip("/").split("/")[-1] or "download.bin"
                    ok = ok and run_get(ser, seq, args.path, out, args.timeout)
            except (ValueError, TimeoutError) as exc:
                print(f"FALHA: {exc}", file=sys.stderr)
                ok = False

        if args.cmd == "manual":
            if not args.dev_key_hex:
                print("Modo manual exige sessao DEV: passe --dev-key-hex", file=sys.stderr)
                sys.exit(1)
            try:
                ok = run_auth_flow(ser, seq, bytes.fromhex(args.dev_key_hex), args.timeout, logout=False)
                seq += 10
                ok = ok and run_manual(ser, seq, args.pump, parse_valves(args.valves), args.seconds,
                                       not args.no_heartbeat, args.timeout)
            except (ValueError, TimeoutError) as exc:
                print(f"FALHA: {exc}", file=sys.stderr)
                ok = False

        if args.cmd in ("params", "pset", "pcommit", "prevert"):
            if not args.dev_key_hex:
                print("Parametros exigem sessao DEV: passe --dev-key-hex", file=sys.stderr)
                sys.exit(1)
            # --value tem default (setpoint 100); aqui tem que vir explicito
            if args.cmd == "pset" and ((args.name is None) or ("--value" not in sys.argv)):
                print("--cmd pset exige --name e --value", file=sys.stderr)
                sys.exit(1)
            try:
                ok = run_auth_flow(ser, seq, bytes.fromhex(args.dev_key_hex), args.timeout, logout=False)
                seq += 10
                if args.cmd == "params":
                    ok = ok and run_params(ser, seq, args.timeout)
                elif args.cmd == "pset":
                    ok = ok and run_pset(ser, seq, args.name, args.value, args.timeout)
                elif args.cmd == "pcommit":
                    ok = ok and run_pcommit(ser, seq, args.timeout)
                else:
                    ok = ok and run_prevert(ser, seq, args.factory, args.timeout)
            except (ValueError, TimeoutError) as exc:
                print(f"FALHA: {exc}", file=sys.stderr)
                ok = False

        if args.cmd in ("stream", "logtail", "listen"):
            try:
                if args.cmd == "stream":
                    if args.dev_key_hex:  # taxa DEV (> 50 Hz) exige sessao DEV nesta conexao
                        ok = run_auth_flow(ser, seq, bytes.fromhex(args.dev_key_hex), args.timeout, logout=False)
                        seq += 10
                    ok = ok and run_stream(ser, seq, args.rate, args.seconds, args.timeout)
                elif args.cmd == "logtail":
                    ok = run_log_tail(ser, seq, args.count, args.timeout)
                else:
                    ok = run_listen(ser, seq, args.seconds, args.timeout)
            except (ValueError, TimeoutError) as exc:
                print(f"FALHA: {exc}", file=sys.stderr)
                ok = False

        if args.cmd == "auth":
            try:
                ok = run_auth_flow(ser, seq, bytes.fromhex(args.dev_key_hex), args.timeout)
            except (ValueError, TimeoutError) as exc:
                print(f"FALHA: {exc}", file=sys.stderr)
                ok = False

    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
