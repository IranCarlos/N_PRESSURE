"""Dispositivo virtual: fala o mesmo protocolo do equipamento, sem hardware.

Simula um ciclo simples por tempo real e grava no cartao virtual os mesmos
arquivos do firmware (index, .rec, .csv e logs encadeados). Comandos DEV sao
recusados como no firmware. Serve para os testes e para abrir o aplicativo sem
equipamento (`python -m app --virtual`).
"""
from __future__ import annotations

import hashlib
import hmac
import os
import queue
import struct
import threading
import time
from datetime import datetime

from . import protocol as p
from .sdfiles import GENESIS

_DEV_ONLY_FIRST = 0x0100
_SERIAL = bytes(range(12))
_PARAMS = [
    # id, inteiro, grupo, min, max, fabrica, nome, unidade
    (0x0101, 0, 0, 0.0, 100.0, 1.0, "pid.kp", ""),
    (0x0201, 0, 1, 0.5, 20.0, 3.0, "estab.banda", "mmHg"),
    (0x0505, 0, 5, 0.0, 50.0, 8.0, "vaz.queda", "mmHg"),
    (0x0506, 1, 5, 1000.0, 60000.0, 5000.0, "vaz.janela", "ms"),
]
_ALLOWED_ROOTS = ("/tests", "/logs/events", "/logs/errors", "/users", "/reports")


def log_line(prev: str, when: datetime, seq: int, level: str, code: str, test: str, state: str,
             pressure: float, message: str) -> tuple[str, str]:
    body = (f"{when:%Y-%m-%dT%H:%M:%S}-03:00;seq={seq:06d};lvl={level};code={code};test={test};"
            f"state={state};p={pressure:.1f};msg={message};prev={prev}")
    digest = hashlib.sha256((prev + body).encode()).hexdigest()
    return f"{body};hash={digest}", digest


class VirtualCard:
    """Cartao SD em memoria."""

    def __init__(self) -> None:
        self.files: dict[str, bytearray] = {}
        self.chain = {"events": GENESIS, "errors": GENESIS}
        self.seq = 0
        self.last_id = 0

    def append(self, path: str, text: str) -> None:
        self.files.setdefault(path, bytearray()).extend(text.encode())

    def log(self, kind: str, when: datetime, level: str, code: str, test: str, state: str,
            pressure: float, message: str) -> None:
        self.seq += 1
        line, self.chain[kind] = log_line(self.chain[kind], when, self.seq, level, code, test, state,
                                          pressure, message)
        self.append(f"/logs/{kind}/{when:%Y-%m}.log", line + "\n")

    def store_test(self, when: datetime, sp: float, peak: float, plateau: float, t_inf: float, t_plat: float,
                   t_def: float, residual: float, nc: str, operator: int, participant: int,
                   curve: list[float]) -> str:
        self.last_id += 1
        tid = f"T{self.last_id:08d}"
        stamp = f"{when:%d/%m/%Y %H:%M:%S}"
        status = "Conforme" if nc == "NC00" else "Nao conforme"
        rec = (f"{tid} | P{participant:03d} | OP{operator:02d} | {stamp} | Ciclo {self.last_id} | "
               f"Alvo {sp:.1f} mmHg | Max {peak:.1f} | Plato medio {plateau:.1f} | "
               f"Faixa {plateau - 0.4:.1f}-{plateau + 0.4:.1f} | Insuflacao {t_inf:.1f} s | Plato {t_plat:.1f} s | "
               f"Descompressao {t_def:.1f} s | Pressao residual {residual:.1f} mmHg | {status} | {nc} | E000 | "
               f"myPRESSURE VIRT | FW virtual | ultima calibracao 01/10/2026 10:00:00")
        folder = f"/tests/{when:%Y/%m}"
        self.append(f"{folder}/{tid}.rec", rec + "\n")
        body = f"{tid};{stamp};OP{operator:02d};{sp:.1f};{peak:.1f};{nc};"
        digest = hashlib.sha256((GENESIS + body).encode()).hexdigest()
        self.append("/tests/index.csv", body + digest + "\n")
        text = "Tempo_s,Pressao_mmHg\n" + "".join(f"{i / 10:.2f},{v:.1f}\n" for i, v in enumerate(curve))
        self.append(f"{folder}/{tid}.csv", text)
        rec_sha = hashlib.sha256(rec.encode()).hexdigest()[:24]
        csv_sha = hashlib.sha256(text.encode()).hexdigest()[:24]
        self.log("events", when, "EVT", "REC", tid, "STANDBY", 0.0, f"rec={rec_sha} csv={csv_sha}")
        return tid

    def listing(self, path: str) -> list[p.FsEntry]:
        prefix = path.rstrip("/") + "/"
        seen: dict[str, p.FsEntry] = {}
        for name, data in self.files.items():
            if not name.startswith(prefix):
                continue
            head, _, rest = name[len(prefix):].partition("/")
            seen[head] = p.FsEntry(head, bool(rest), 0 if rest else len(data), 0, 0)
        return list(seen.values())

    def is_dir(self, path: str) -> bool:
        return any(name.startswith(path.rstrip("/") + "/") for name in self.files)


class FakeDevice:
    """Implementa Transport: `write` recebe do PC, `read` devolve ao PC."""

    def __init__(self, card: VirtualCard | None = None, time_scale: float = 1.0, dev_key: bytes | None = None) -> None:
        self.card = card or VirtualCard()
        self._time_scale = time_scale
        self._rx = p.FrameSplitter()
        self._tx: queue.Queue[bytes] = queue.Queue()
        self._lock = threading.Lock()
        self._alive = True
        self.state = p.ST_IDLE
        self.pressure = 0.0
        self.setpoint = 100.0
        self.operator = 1
        self.participant = 1
        self.error = 0
        self._stream_period = 0.0
        self._next_stream = 0.0
        self._t0 = time.monotonic()
        self._hello_seen = False
        self._cycle: dict | None = None
        self.dev_key = dev_key
        self.role = 0
        self._nonce = b""
        self.param_values = {row[0]: row[5] for row in _PARAMS}
        self.saved_values = dict(self.param_values)
        self.commits = 0
        self.audit: list[str] = []
        self.pump = 0
        self.valves = 0
        self.leak_rate = 0.3  # mmHg/s com tudo fechado
        self.settle_gain = 0.0  # queda apos bombear: ar passando do tubo ao manguito
        self._settle = 0.0
        self._manual_seen = 0.0
        self.cal_points: list[tuple[float, float]] = []
        self.cal_active = False
        self.cal_offset = 0.0
        self._thread = threading.Thread(target=self._run, name="fake-device", daemon=True)
        self._thread.start()

    def read(self) -> bytes:
        try:
            return self._tx.get(timeout=0.05)
        except queue.Empty:
            return b""

    def write(self, data: bytes) -> None:
        for frame in self._rx.feed(data):
            if frame.type == p.TYPE_REQ:
                with self._lock:
                    self._send(p.TYPE_RSP, frame.seq, frame.cmd, self._handle(frame.cmd, frame.payload))

    def close(self) -> None:
        self._alive = False

    def _send(self, kind: int, seq: int, cmd: int, payload: bytes) -> None:
        self._tx.put(p.encode_frame(p.Frame(kind, seq, cmd, payload)))

    def _event(self, cmd: int, payload: bytes) -> None:
        if self._hello_seen:
            self._send(p.TYPE_EVT, 0, cmd, payload)

    def _set_state(self, state: int) -> None:
        self._event(p.EVT_STATE, bytes([self.state, state]))
        self.state = state

    def _handle(self, cmd: int, data: bytes) -> bytes:
        try:
            if cmd in (0x0040, 0x0041, 0x0042):
                return self._auth(cmd, data)
            if cmd >= _DEV_ONLY_FIRST:
                return self._dev(cmd, data) if self.role == 1 else bytes([p.RSP_FORBIDDEN])
            return self._command(cmd, data)
        except struct.error:
            return bytes([3])

    def _auth(self, cmd: int, data: bytes) -> bytes:
        if cmd == 0x0040:
            self._nonce = os.urandom(16)
            return bytes([0]) + self._nonce
        if cmd == 0x0041:
            good = self.dev_key is not None and self._nonce and hmac.compare_digest(
                hmac.new(self.dev_key, self._nonce + _SERIAL, hashlib.sha256).digest(), data)
            self._nonce = b""
            if not good:
                return bytes([8, 0])
            self.role = 1
            return bytes([0, 1])
        self.role = 0
        return bytes([0, 0])

    def _dev(self, cmd: int, data: bytes) -> bytes:
        idle = self.state == p.ST_IDLE
        if cmd == 0x0100:
            start = struct.unpack("<H", data)[0]
            page = _PARAMS[start:start + 2]
            out = bytearray(bytes([0]) + struct.pack("<HBB", len(_PARAMS), self._dirty(), len(page)))
            for pid, kind, group, low, high, factory, name, unit in page:
                out += struct.pack("<HBBffff", pid, kind, group, low, high, factory, self.param_values[pid])
                out += bytes([len(name)]) + name.encode() + bytes([len(unit)]) + unit.encode()
            return bytes(out)
        if cmd == 0x0102:
            pid, value = struct.unpack("<Hf", data)
            row = next((r for r in _PARAMS if r[0] == pid), None)
            if row is None:
                return bytes([3])
            if not idle:
                return bytes([5])
            if not row[3] <= value <= row[4]:
                return bytes([7])
            self.audit.append(f"param {row[6]} {self.param_values[pid]} -> {value}")
            self.param_values[pid] = value
            return bytes([0]) + struct.pack("<f", value)
        if cmd == 0x0103:
            self.saved_values = dict(self.param_values)
            self.commits += 1
            return bytes([0]) + struct.pack("<I", self.commits)
        if cmd == 0x0104:
            self.param_values = {r[0]: r[5] for r in _PARAMS} if data[0] == 1 else dict(self.saved_values)
            return bytes([0])
        if cmd == 0x0110:
            if not idle:
                return bytes([5])
            self._set_state(p.ST_MANUAL)
            self._manual_seen = time.monotonic()
            self.pump, self.valves = 0, 0
            return bytes([0])
        if cmd == 0x0111:
            if self.state == p.ST_MANUAL:
                self._leave_manual()
            return bytes([0])
        if cmd == 0x0112:
            if self.state != p.ST_MANUAL:
                return bytes([5])
            self.pump, self.valves = data[0], data[1]
            self._manual_seen = time.monotonic()
            return bytes([0])
        if cmd == 0x0120:
            if self.state not in (p.ST_IDLE, p.ST_MANUAL):
                return bytes([5])
            self.cal_active, self.cal_points = True, []
            return bytes([0])
        if cmd == 0x0121:
            ref = struct.unpack("<f", data)[0]
            if not self.cal_active:
                return bytes([5])
            if any(abs(ref - r) < 5.0 for r, _ in self.cal_points):
                return bytes([10])
            self.cal_points.append((ref, self.pressure))
            return bytes([0, len(self.cal_points) - 1, len(self.cal_points)]) + \
                struct.pack("<ffff", self.pressure, 0.1, self.pressure, self.pressure - ref)
        if cmd == 0x0122:
            pts = self.cal_points
            n = len(pts)
            span = (max(r for r, _ in pts) - min(r for r, _ in pts)) if pts else 0.0
            fit = 1 if n < 3 else (2 if span < 50.0 else 0)
            offset = sum(r - d for r, d in pts) / n if n else 0.0
            body = bytes([fit, n]) + struct.pack("<ffff", 1.0, offset, 0.2, span)
            if fit:
                return bytes([10]) + body
            self.audit.append(f"cal offset {self.cal_offset} -> {offset}")
            self.cal_offset = offset
            self.cal_active = False
            return bytes([0]) + body
        if cmd == 0x0123:
            self.cal_active = False
            return bytes([0])
        if cmd == 0x0140:
            if not idle:
                return bytes([5])
            return bytes([0, 0]) + struct.pack("<f", self.pressure)
        if cmd == 0x0150:
            return bytes([0]) + struct.pack("<6I", 12000, 144, 0x01420000, 0, 15144, 15144) + bytes([0]) + \
                struct.pack("<4I", 0, 0, 0, 0) + bytes([8]) + struct.pack("<8H", 215, 149, 215, 333, 173, 352, 940, 153) + \
                struct.pack("<2I", 500, 38) + bytes([1])
        if cmd == 0x0160:
            return bytes([0]) if idle else bytes([5])
        return bytes([1])

    def _dirty(self) -> int:
        return 1 if self.param_values != self.saved_values else 0

    def _leave_manual(self) -> None:
        self.pump, self.valves = 0, 7
        self._set_state(p.ST_IDLE)

    def _command(self, cmd: int, data: bytes) -> bytes:
        if cmd == p.CMD_HELLO:
            self._hello_seen = True
            return (bytes([0, 1]) + b"myPRESSURE VIRT".ljust(16, b"\0") + b"virtual".ljust(12, b"\0") +
                    bytes([self.role]) + _SERIAL)
        if cmd == p.CMD_GET_STATUS:
            return bytes([0, self.state]) + struct.pack("<ff", self.pressure, self.setpoint) + \
                bytes([1, 1, self.error]) + struct.pack("<I", self.card.last_id)
        if cmd == p.CMD_SET_SETPOINT:
            value = struct.unpack("<f", data)[0]
            if self.state != p.ST_IDLE:
                return bytes([5])
            if not 20.0 <= value <= 180.0:
                return bytes([7])
            self.setpoint = value
            return bytes([0, 1 if value > 150.0 else 0])
        if cmd == p.CMD_SET_DATETIME:
            struct.unpack("<HBBBBB", data)
            return bytes([0])
        if cmd == p.CMD_SET_OPERATOR:
            self.operator, self.participant = struct.unpack("<HH", data)
            return bytes([0])
        if cmd == p.CMD_START_TEST:
            if self.state != p.ST_IDLE:
                return bytes([5])
            self._start_cycle()
            return bytes([0])
        if cmd == p.CMD_ABORT_TEST:
            if self.state == p.ST_MANUAL:
                self._leave_manual()
            if self._cycle:
                cancel = data == b"\x01"
                self._cycle["aborted"] = cancel or self.state in (p.ST_INFLATING, p.ST_STABILIZING)
                self._begin_deflate()
            return bytes([0])
        if cmd == p.CMD_STREAM_START:
            rate = struct.unpack("<H", data)[0]
            if rate > (1000 if self.role == 1 else p.USER_STREAM_MAX_HZ):
                return bytes([p.RSP_FORBIDDEN])
            self._stream_period = 1.0 / max(1, rate)
            return bytes([0]) + struct.pack("<H", rate)
        if cmd == p.CMD_STREAM_STOP:
            self._stream_period = 0.0
            return bytes([0])
        if cmd == p.CMD_GET_LOG_TAIL:
            return bytes([0, 0])
        if cmd in (p.CMD_FS_LIST, p.CMD_FS_STAT, p.CMD_FS_READ, p.CMD_FS_CRC):
            return self._fs(cmd, data)
        return bytes([1])

    def _fs(self, cmd: int, data: bytes) -> bytes:
        skip = {p.CMD_FS_LIST: 2, p.CMD_FS_READ: 6}.get(cmd, 0)
        path = data[skip:].decode()
        roots = _ALLOWED_ROOTS + (("/logs/audit",) if self.role == 1 else ())
        if not any(path == r or path.startswith(r + "/") for r in roots):
            return bytes([p.RSP_FORBIDDEN])
        card = self.card
        if cmd == p.CMD_FS_LIST:
            start = struct.unpack("<H", data[:2])[0]
            if not card.is_dir(path):
                return bytes([p.RSP_NOT_FOUND])
            entries = card.listing(path)
            page = entries[start:start + 12]
            out = bytearray([0, len(page), 1 if start + 12 < len(entries) else 0])
            for e in page:
                name = e.name.encode()
                out += bytes([e.is_dir]) + struct.pack("<IHH", e.size, 0, 0) + bytes([len(name)]) + name
            return bytes(out)
        content = card.files.get(path)
        if content is None:
            return bytes([p.RSP_NOT_FOUND])
        if cmd == p.CMD_FS_STAT:
            return bytes([0, 0]) + struct.pack("<IHH", len(content), 0, 0)
        if cmd == p.CMD_FS_READ:
            offset, length = struct.unpack("<IH", data[:6])
            chunk = bytes(content[offset:offset + length])
            return bytes([0]) + struct.pack("<IH", offset, len(chunk)) + chunk
        blob = bytes(content)
        return bytes([0]) + struct.pack("<II", len(blob), p.crc32(blob)) + hashlib.sha256(blob).digest()

    def _start_cycle(self) -> None:
        self._cycle = {"t": 0.0, "phase_t": 0.0, "curve": [], "peak": 0.0, "aborted": False, "next_curve": 0.0,
                       "t_inf": 0.0, "t_plat": 0.0, "t_def": 0.0}
        self._set_state(p.ST_INFLATING)

    def _begin_deflate(self) -> None:
        c = self._cycle
        if c and self.state != p.ST_DEFLATING:
            c["t_plat"] = c["phase_t"] if self.state == p.ST_HOLD else 0.0
            c["phase_t"] = 0.0
            self._set_state(p.ST_DEFLATING)

    def _finish(self) -> None:
        c = self._cycle
        c["t_def"] = c["phase_t"]
        nc = "NC07" if c["aborted"] else ("NC05" if c["t_def"] > 3.5 else "NC00")
        self.card.store_test(datetime.now(), self.setpoint, c["peak"], self.setpoint, c["t_inf"], c["t_plat"],
                             c["t_def"], self.pressure, nc, self.operator, self.participant, c["curve"])
        self._event(p.EVT_TEST_DONE, struct.pack("<fffIII", self.setpoint, c["peak"], self.pressure,
                                                 int(c["t_inf"] * 1000), int(c["t_plat"] * 1000), int(c["t_def"] * 1000)) +
                    bytes([c["aborted"], 0, int(nc[2:])]))
        self._cycle = None
        self._set_state(p.ST_IDLE)

    def _step(self, dt: float) -> None:
        c = self._cycle
        if self.state == p.ST_MANUAL:
            if time.monotonic() - self._manual_seen > 0.3:
                self.error = 10
                self._leave_manual()
                return
            self.pressure += (self.pump * 0.3 - self.leak_rate) * dt
            self._settle += (self.settle_gain * self.pump - self._settle) * min(1.0, dt / 0.5) if self.pump else                 -self._settle * min(1.0, dt / 2.0)
            self.pressure -= self._settle * dt
            if self.valves & 2:
                self.pressure -= 40.0 * dt
            if self.valves & 1:
                self.pressure -= 80.0 * dt
            self.pressure = max(0.0, self.pressure)
            return
        if c is None:
            self.pressure = max(0.0, self.pressure - 40.0 * dt)
            return
        c["phase_t"] += dt
        c["t"] += dt
        if self.state == p.ST_INFLATING:
            self.pressure = min(self.setpoint, self.pressure + 40.0 * dt)
            if self.pressure >= self.setpoint:
                c["t_inf"] = c["t"]
                c["phase_t"] = 0.0
                self._set_state(p.ST_HOLD)
        elif self.state == p.ST_HOLD:
            if c["phase_t"] >= 8.0:
                self._begin_deflate()
        elif self.state == p.ST_DEFLATING:
            self.pressure = max(0.0, self.pressure - 80.0 * dt)
            if self.pressure <= 3.0 and c["phase_t"] >= 1.0:
                self._finish()
                return
        c["peak"] = max(c["peak"], self.pressure)
        if c["t"] >= c["next_curve"]:
            c["curve"].append(self.pressure)
            c["next_curve"] += 0.1

    def _run(self) -> None:
        last = time.monotonic()
        while self._alive:
            time.sleep(0.005)
            now = time.monotonic()
            with self._lock:
                self._step((now - last) * self._time_scale)
                if self._stream_period and now >= self._next_stream:
                    self._next_stream = now + self._stream_period
                    sample = struct.pack("<Iff", int((now - self._t0) * 1000), self.pressure, self.setpoint) + \
                        bytes([self.state, 100 if self.state == p.ST_INFLATING else 0, 0, self.error])
                    self._send(p.TYPE_STREAM, 0, 0, struct.pack("<HI", 1, 0) + sample)
            last = now
