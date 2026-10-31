"""Nivel DEV: sessao autenticada, parametros, modo manual, calibracao e diagnostico.

Fica fora de core.device de proposito: o nivel de usuario nem importa este modulo.
A chave de 32 bytes vive so' em memoria, durante o login; nunca e' gravada nem
mostrada. O firmware impoe as permissoes por conta propria (usb_proto_policy).
"""
from __future__ import annotations

import hashlib
import hmac
import json
import re
import struct
import threading
from dataclasses import dataclass
from pathlib import Path

from . import protocol as p
from .device import Device, DeviceError, LinkLost

CMD_AUTH_CHALLENGE, CMD_AUTH_RESPONSE, CMD_AUTH_LOGOUT = 0x0040, 0x0041, 0x0042
CMD_PARAM_LIST, CMD_PARAM_GET, CMD_PARAM_SET, CMD_PARAM_COMMIT, CMD_PARAM_REVERT = 0x0100, 0x0101, 0x0102, 0x0103, 0x0104
CMD_MANUAL_ENTER, CMD_MANUAL_EXIT, CMD_MANUAL_SET = 0x0110, 0x0111, 0x0112
CMD_CAL_START, CMD_CAL_POINT, CMD_CAL_APPLY, CMD_CAL_ABORT = 0x0120, 0x0121, 0x0122, 0x0123
CMD_SELFTEST, CMD_GET_DIAG, CMD_REBOOT = 0x0140, 0x0150, 0x0160

DEV_STREAM_MAX_HZ = 1000
RSP_BUSY, RSP_INVALID_STATE, RSP_CONFLICT = 4, 5, 10
V1, V2, V3 = 1, 2, 4
ALL_VALVES = V1 | V2 | V3
KEY_LEN = 32
MANUAL_PERIOD_S = 0.1  # o firmware encerra o manual apos 300 ms sem sinal
TASKS = ("control", "safety", "input", "app", "usbProto", "storage", "ui", "audio")
SELFTEST_FAILS = ("Cartão SD", "Relógio", "EEPROM", "Sensor", "Zero do sensor")
FIT_NAMES = {0: "OK", 1: "poucos pontos (mínimo 3)", 2: "faixa pequena (mínimo 50 mmHg)", 3: "pontos degenerados",
             4: "escala implausível", 5: "offset implausível", 6: "não linear (resíduo acima do limite)"}
GROUP_NAMES = {0: "PID", 1: "Estabilização", 2: "Válvula de alívio", 3: "Aproximação", 4: "Segurança", 5: "Vazamento",
               6: "Limites", 7: "Calibração"}


def parse_key(text: str) -> bytes:
    """Aceita o arquivo dev_key_secret.h, a lista "0x12, 0x34, ..." ou 64 hexadecimais."""
    body = text[text.index("DEV_KEY_PROVISION"):] if "DEV_KEY_PROVISION" in text else text
    pairs = re.findall(r"0[xX]([0-9a-fA-F]{2})\b", body)
    if pairs:
        key = bytes(int(x, 16) for x in pairs)
    else:
        cleaned = re.sub(r"[\s,;:{}]", "", text)
        key = bytes.fromhex(cleaned) if re.fullmatch(r"(?:[0-9a-fA-F]{2})*", cleaned) else b""
    if len(key) != KEY_LEN:
        raise ValueError(f"a chave deve ter 32 bytes; foram encontrados {len(key)}")
    return key


@dataclass(frozen=True)
class Param:
    id: int
    is_int: bool
    group: int
    minimum: float
    maximum: float
    factory: float
    value: float
    name: str
    unit: str

    def text(self, value: float | None = None) -> str:
        value = self.value if value is None else value
        return f"{int(value)}" if self.is_int else f"{value:.6g}"


def parse_param_page(rsp: bytes) -> tuple[list[Param], int, bool]:
    total, dirty, count = struct.unpack("<HBB", rsp[1:5])
    params = []
    off = 5
    for _ in range(count):
        pid, kind, group, low, high, factory, value = struct.unpack("<HBBffff", rsp[off:off + 20])
        off += 20
        name_len = rsp[off]
        name = rsp[off + 1:off + 1 + name_len].decode("ascii", "replace")
        off += 1 + name_len
        unit_len = rsp[off]
        unit = rsp[off + 1:off + 1 + unit_len].decode("ascii", "replace")
        off += 1 + unit_len
        params.append(Param(pid, kind == 1, group, low, high, factory, value, name, unit))
    return params, total, bool(dirty)


@dataclass(frozen=True)
class CalPoint:
    index: int
    count: int
    raw: float
    deviation: float
    reading: float
    error: float


@dataclass(frozen=True)
class CalResult:
    applied: bool
    fit: int
    count: int
    scale: float
    offset: float
    residual: float
    span: float

    @property
    def reason(self) -> str:
        return FIT_NAMES.get(self.fit, str(self.fit))


class NotStable(Exception):
    """A pressao ainda varia; esperar e capturar de novo."""

    def __init__(self, deviation: float) -> None:
        super().__init__(f"pressão ainda variando ({deviation:.2f} mmHg)")
        self.deviation = deviation


@dataclass(frozen=True)
class Diagnostics:
    uptime_s: float
    jitter_us: int
    jitter_at_s: float
    jitter_state: int
    exec_us: int
    reset_flags: int
    dropped: int
    heap_free: int
    heap_min: int
    hard_fault: bool
    iwdg_reset: bool
    fault: tuple[int, int, int, int]
    stacks: dict[str, int]


def parse_diag(r: bytes) -> Diagnostics:
    up, jit, rsr, drop, heap, heap_min = struct.unpack("<6I", r[1:25])
    flags = r[25]
    fault = struct.unpack("<4I", r[26:42])
    stacks = struct.unpack(f"<{r[42]}H", r[43:43 + 2 * r[42]])
    at_ms, exec_us = struct.unpack("<2I", r[59:67])
    return Diagnostics(up / 1000.0, jit, at_ms / 1000.0, r[67], exec_us, rsr, drop, heap, heap_min, bool(flags & 1),
                       bool(flags & 2), fault, dict(zip(TASKS, stacks)))


class DevSession:
    def __init__(self, device: Device) -> None:
        self.device = device

    @property
    def _req(self):
        return self.device.request

    @classmethod
    def login(cls, device: Device, key: bytes) -> "DevSession":
        serial = bytes.fromhex(device.hello().serial)
        nonce = device.request(CMD_AUTH_CHALLENGE)[1:17]
        rsp = device.request(CMD_AUTH_RESPONSE, hmac.new(key, nonce + serial, hashlib.sha256).digest())
        if rsp[1] != 1:
            raise DeviceError(8)
        return cls(device)

    def logout(self) -> None:
        self._req(CMD_AUTH_LOGOUT)

    # --- Parametros ----------------------------------------------------

    def params(self) -> tuple[list[Param], bool]:
        params: list[Param] = []
        while True:
            page, total, dirty = parse_param_page(self._req(CMD_PARAM_LIST, struct.pack("<H", len(params))))
            params += page
            if not page or len(params) >= total:
                return params, dirty

    def param_set(self, pid: int, value: float) -> float:
        return struct.unpack("<f", self._req(CMD_PARAM_SET, struct.pack("<Hf", pid, value))[1:5])[0]

    def param_commit(self) -> int:
        return struct.unpack("<I", self._req(CMD_PARAM_COMMIT)[1:5])[0]

    def param_revert(self, factory: bool = False) -> None:
        self._req(CMD_PARAM_REVERT, bytes([1 if factory else 0]))

    # --- Modo manual ---------------------------------------------------

    def manual_enter(self) -> None:
        self._req(CMD_MANUAL_ENTER)

    def manual_exit(self) -> None:
        self._req(CMD_MANUAL_EXIT)

    def manual_set(self, pump_pct: int, valves: int) -> None:
        self._req(CMD_MANUAL_SET, bytes([pump_pct, valves]))

    # --- Calibracao ----------------------------------------------------

    def cal_start(self) -> None:
        self._req(CMD_CAL_START)

    def cal_abort(self) -> None:
        self._req(CMD_CAL_ABORT)

    def cal_point(self, reference: float) -> CalPoint:
        try:
            r = self._req(CMD_CAL_POINT, struct.pack("<f", reference))
        except DeviceError as exc:
            if exc.status == RSP_BUSY:
                raise NotStable(exc.payload_f32()) from exc
            raise
        return CalPoint(r[1], r[2], *struct.unpack("<ffff", r[3:19]))

    def cal_apply(self) -> CalResult:
        try:
            r, applied = self._req(CMD_CAL_APPLY), True
        except DeviceError as exc:
            if exc.status != RSP_CONFLICT:
                raise
            r, applied = exc.payload, False
        return CalResult(applied, r[1], r[2], *struct.unpack("<ffff", r[3:19]))

    # --- Diagnostico ---------------------------------------------------

    def diagnostics(self) -> Diagnostics:
        return parse_diag(self._req(CMD_GET_DIAG))

    def selftest(self) -> tuple[list[str], float]:
        r = self._req(CMD_SELFTEST)
        return [n for i, n in enumerate(SELFTEST_FAILS) if r[1] & (1 << i)], struct.unpack("<f", r[2:6])[0]

    def reboot(self) -> None:
        self._req(CMD_REBOOT)


def export_params(params: list[Param], path: Path) -> None:
    data = {p_.name: p_.value for p_ in params}
    path.write_text(json.dumps(data, indent=2, sort_keys=True), encoding="utf-8")


def diff_params(params: list[Param], path: Path) -> list[tuple[Param, float]]:
    """Parametros cujo valor no arquivo difere do equipamento. Nome desconhecido e' erro."""
    data = json.loads(path.read_text(encoding="utf-8"))
    by_name = {p_.name: p_ for p_ in params}
    unknown = sorted(set(data) - set(by_name))
    if unknown:
        raise ValueError(f"parâmetros desconhecidos: {', '.join(unknown)}")
    changes = []
    for name, value in data.items():
        param = by_name[name]
        if not param.minimum <= float(value) <= param.maximum:
            raise ValueError(f"{name}: {value} fora da faixa")
        if abs(float(value) - param.value) > 1e-6 * max(1.0, abs(param.value)):
            changes.append((param, float(value)))
    return changes


class ManualLink:
    """Mantem o modo manual vivo: reenvia o comando a 10 Hz numa thread.

    Ao sair (fim normal, erro ou excecao) a bomba para e o MANUAL_EXIT devolve o
    estado seguro. Se o equipamento encerrar o manual por conta propria (pedal,
    sobrepressao), `ended` fica verdadeiro e os comandos seguintes falham.
    """

    def __init__(self, session: DevSession) -> None:
        self._session = session
        self._lock = threading.Lock()
        self._pump = 0
        self._valves = 0
        self._stop = threading.Event()
        self.ended = threading.Event()
        self._thread: threading.Thread | None = None

    def __enter__(self) -> "ManualLink":
        self._session.manual_enter()
        self._session.manual_set(0, 0)
        self._thread = threading.Thread(target=self._keep_alive, name="manual-link", daemon=True)
        self._thread.start()
        return self

    def __exit__(self, *_exc) -> None:
        self._stop.set()
        if self._thread:
            self._thread.join(timeout=1.0)
        try:
            self._session.manual_exit()  # bomba off, valvulas abertas
        except (DeviceError, LinkLost):
            pass  # sem resposta o firmware faz o mesmo sozinho em 300 ms

    def set(self, pump_pct: int, valves: int) -> None:
        with self._lock:
            self._pump, self._valves = pump_pct, valves
        self._send()

    def _send(self) -> None:
        if self.ended.is_set():
            raise DeviceError(RSP_INVALID_STATE)
        try:
            with self._lock:
                command = (self._pump, self._valves)
            self._session.manual_set(*command)
        except DeviceError:
            self.ended.set()
            raise

    def _keep_alive(self) -> None:
        while not self._stop.wait(MANUAL_PERIOD_S):
            try:
                self._send()
            except Exception:
                self.ended.set()
                return
