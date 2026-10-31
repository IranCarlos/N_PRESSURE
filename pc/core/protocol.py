"""Protocolo USB do equipamento: frames, COBS, CRC32 e codecs dos comandos.

Espelha Core/Services/UsbProto (cobs.c, usb_crc32.c, usb_proto_dispatch.h). So'
contem o que o nivel USER usa; os comandos DEV entram com a Fase 9.
"""
from __future__ import annotations

import struct
from dataclasses import dataclass

VERSION = 0x01
TYPE_REQ, TYPE_RSP, TYPE_EVT, TYPE_STREAM = 0x01, 0x02, 0x03, 0x04

CMD_HELLO = 0x0001
CMD_GET_STATUS = 0x0002
CMD_SET_SETPOINT = 0x0003
CMD_SET_DATETIME = 0x0004
CMD_START_TEST = 0x0005
CMD_ABORT_TEST = 0x0006
CMD_SET_OPERATOR = 0x0007
CMD_STREAM_START = 0x0010
CMD_STREAM_STOP = 0x0011
CMD_FS_LIST = 0x0020
CMD_FS_STAT = 0x0021
CMD_FS_READ = 0x0022
CMD_FS_CRC = 0x0023
CMD_GET_LOG_TAIL = 0x0030

EVT_STATE, EVT_TEST_DONE, EVT_ERROR, EVT_SD, EVT_AUTH_EXPIRED = 0x0301, 0x0302, 0x0303, 0x0304, 0x0305

RSP_OK, RSP_FORBIDDEN, RSP_NOT_FOUND = 0, 2, 9
RSP_NAMES = {
    0: "OK", 1: "Comando desconhecido", 2: "Sem permissao", 3: "Parametro invalido",
    4: "Equipamento ocupado", 5: "Comando invalido neste estado", 6: "Erro no cartao SD",
    7: "Valor fora da faixa", 8: "Nao autenticado", 9: "Nao encontrado", 10: "Conflito",
}

ST_BOOT, ST_IDLE, ST_INFLATING, ST_STABILIZING, ST_HOLD, ST_DEFLATING = 0, 1, 2, 3, 4, 5
ST_LEAK, ST_CONFIG, ST_FAULT, ST_MANUAL = 6, 7, 8, 9
ACTIVE_STATES = (ST_INFLATING, ST_STABILIZING, ST_HOLD, ST_DEFLATING)
STATE_NAMES = {
    ST_BOOT: "Iniciando", ST_IDLE: "Pronto", ST_INFLATING: "Inflando",
    ST_STABILIZING: "Estabilizando", ST_HOLD: "Em platô", ST_DEFLATING: "Desinflando",
    ST_LEAK: "Teste de vazamento", ST_CONFIG: "Ajustes", ST_FAULT: "Erro", ST_MANUAL: "Manual (PC)",
}
ERROR_NAMES = {  # Apendice D
    1: "Sobrepressão", 2: "Falha no sensor de pressão", 3: "Não atingiu o alvo a tempo",
    4: "Tempo de desinflar excedido", 10: "Sinal de vida do PC perdido", 13: "Vazamento detectado",
}
LOG_LEVELS = {0: "EVT", 1: "ERR", 2: "AUDIT"}

FS_READ_MAX = 1016
USER_STREAM_MAX_HZ = 50
USB_VID, USB_PID = 0x0483, 0x5740
DEVICE_ID_PREFIX = "myPRESSURE"

_DEVICE_ID_LEN, _FW_LEN, _SERIAL_LEN = 16, 12, 12
_CRC_POLY, _MASK32 = 0x04C11DB7, 0xFFFFFFFF


def _crc_table() -> list[int]:
    table = []
    for i in range(256):
        crc = i << 24
        for _ in range(8):
            crc = ((crc << 1) ^ _CRC_POLY) & _MASK32 if crc & 0x80000000 else (crc << 1) & _MASK32
        table.append(crc)
    return table


_CRC_TABLE = _crc_table()


def crc32(data: bytes) -> int:
    """MSB-first, init 0xFFFFFFFF, sem reflexao e sem XOR final (usb_crc32.c)."""
    crc = _MASK32
    for byte in data:
        crc = ((crc << 8) & _MASK32) ^ _CRC_TABLE[((crc >> 24) ^ byte) & 0xFF]
    return crc


def cobs_encode(data: bytes) -> bytes:
    out = bytearray(b"\x00")
    code_idx = 0
    code = 1
    for b in data:
        if b == 0:
            out[code_idx] = code
            code = 1
            code_idx = len(out)
            out.append(0)
            continue
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
    out = bytearray()
    i = 0
    n = len(data)
    while i < n:
        code = data[i]
        if code == 0 or (i + code > n and code != 1):
            raise ValueError("COBS malformado")
        i += 1
        out += data[i:i + code - 1]
        i += code - 1
        if code != 0xFF and i != n:
            out.append(0)
    return bytes(out)


@dataclass(frozen=True)
class Frame:
    type: int
    seq: int
    cmd: int
    payload: bytes


def encode_frame(frame: Frame) -> bytes:
    """VER TYPE SEQ(2) CMD(2) LEN(2) PAYLOAD CRC32(4), em COBS, terminado em 0x00."""
    body = struct.pack("<BBHHH", VERSION, frame.type, frame.seq, frame.cmd, len(frame.payload)) + frame.payload
    return cobs_encode(body + struct.pack("<I", crc32(body))) + b"\x00"


def decode_frame(raw: bytes) -> Frame:
    data = cobs_decode(raw)
    if len(data) < 12:
        raise ValueError("frame curto")
    ver, typ, seq, cmd, length = struct.unpack("<BBHHH", data[:8])
    if ver != VERSION or len(data) != 12 + length:
        raise ValueError("cabecalho invalido")
    if struct.unpack("<I", data[8 + length:])[0] != crc32(data[:8 + length]):
        raise ValueError("CRC invalido")
    return Frame(typ, seq, cmd, data[8:8 + length])


class FrameSplitter:
    """Separa os frames do fluxo de bytes; descarta o que estiver corrompido."""

    def __init__(self) -> None:
        self._buf = bytearray()

    def feed(self, chunk: bytes) -> list[Frame]:
        self._buf += chunk
        frames = []
        while True:
            end = self._buf.find(0)
            if end < 0:
                return frames
            raw = bytes(self._buf[:end])
            del self._buf[:end + 1]
            if raw:
                try:
                    frames.append(decode_frame(raw))
                except ValueError:
                    pass


@dataclass(frozen=True)
class Hello:
    protocol: int
    device_id: str
    firmware: str
    role: int
    serial: str


@dataclass(frozen=True)
class Status:
    state: int
    pressure: float
    setpoint: float
    rtc_valid: bool
    sd_mounted: bool
    error: int
    cycle: int


@dataclass(frozen=True)
class Sample:
    t_ms: int
    pressure: float
    setpoint: float
    state: int
    pump: int
    valves: int
    error: int


@dataclass(frozen=True)
class TestDone:
    setpoint: float
    peak: float
    residual: float
    inflate_ms: int
    plateau_ms: int
    deflate_ms: int
    aborted: bool
    fault: bool
    nc: int

    @property
    def nc_code(self) -> str:
        return f"NC{self.nc:02d}"


@dataclass(frozen=True)
class LogLine:
    seq: int
    datetime: str
    level: str
    code: str
    message: str


@dataclass(frozen=True)
class FsEntry:
    name: str
    is_dir: bool
    size: int
    fdate: int
    ftime: int


def _text(raw: bytes) -> str:
    return raw.split(b"\x00", 1)[0].decode("ascii", "replace")


def parse_hello(p: bytes) -> Hello:
    role_at = 2 + _DEVICE_ID_LEN + _FW_LEN
    return Hello(p[1], _text(p[2:2 + _DEVICE_ID_LEN]), _text(p[2 + _DEVICE_ID_LEN:role_at]), p[role_at],
                 p[role_at + 1:role_at + 1 + _SERIAL_LEN].hex())


def parse_status(p: bytes) -> Status:
    pressure, setpoint = struct.unpack("<ff", p[2:10])
    return Status(p[1], pressure, setpoint, bool(p[10]), bool(p[11]), p[12], struct.unpack("<I", p[13:17])[0])


def parse_stream(p: bytes) -> tuple[int, list[Sample]]:
    count, dropped = struct.unpack("<HI", p[:6])
    samples = []
    for i in range(count):
        off = 6 + 16 * i
        t_ms, pressure, setpoint = struct.unpack("<Iff", p[off:off + 12])
        samples.append(Sample(t_ms, pressure, setpoint, *p[off + 12:off + 16]))
    return dropped, samples


def parse_test_done(p: bytes) -> TestDone:
    sp, peak, residual, t_inf, t_plat, t_def = struct.unpack("<fffIII", p[:24])
    return TestDone(sp, peak, residual, t_inf, t_plat, t_def, bool(p[24]), bool(p[25]), p[26])


def parse_log_tail(p: bytes) -> list[LogLine]:
    lines = []
    off = 2
    for _ in range(p[1]):
        seq, year = struct.unpack("<IH", p[off:off + 6])
        mo, d, h, mi, s, level = p[off + 6:off + 12]
        code = _text(p[off + 12:off + 17])
        msg_len = p[off + 17]
        msg = p[off + 18:off + 18 + msg_len].decode("ascii", "replace")
        off += 18 + msg_len
        lines.append(LogLine(seq, f"{d:02d}/{mo:02d}/{year} {h:02d}:{mi:02d}:{s:02d}",
                             LOG_LEVELS.get(level, str(level)), code, msg))
    return lines


def parse_fs_list(p: bytes) -> tuple[list[FsEntry], bool]:
    count, more = p[1], bool(p[2])
    entries = []
    off = 3
    for _ in range(count):
        is_dir = bool(p[off])
        size, fdate, ftime = struct.unpack("<IHH", p[off + 1:off + 9])
        name_len = p[off + 9]
        name = p[off + 10:off + 10 + name_len].decode("ascii", "replace")
        off += 10 + name_len
        entries.append(FsEntry(name, is_dir, size, fdate, ftime))
    return entries, more
