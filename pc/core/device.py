"""Cliente do equipamento: envia comandos, recebe eventos e telemetria.

Uma thread le a porta e entrega respostas, eventos e amostras. Os callbacks rodam
nessa thread; quem usa Qt deve repassar por sinal. So' expoe comandos do nivel
USER: nada de parametros, calibracao ou modo manual.
"""
from __future__ import annotations

import hashlib
import struct
import threading
from dataclasses import dataclass
from datetime import datetime
from typing import Callable, Protocol

from . import protocol as p


class DeviceError(Exception):
    """Comando recusado pelo equipamento."""

    def __init__(self, status: int) -> None:
        super().__init__(p.RSP_NAMES.get(status, f"status {status}"))
        self.status = status


class LinkLost(Exception):
    """Sem resposta, ou a porta fechou."""


class Transport(Protocol):
    def read(self) -> bytes:
        """Bloqueia por pouco tempo; devolve b"" se nada chegou."""

    def write(self, data: bytes) -> None: ...

    def close(self) -> None: ...


class SerialTransport:
    def __init__(self, port: str) -> None:
        import serial

        self._ser = serial.Serial(port, 115200, timeout=0.05)

    def read(self) -> bytes:
        return self._ser.read(max(1, self._ser.in_waiting))

    def write(self, data: bytes) -> None:
        self._ser.write(data)

    def close(self) -> None:
        self._ser.close()


def find_ports() -> list[str]:
    """Portas com o VID/PID do equipamento."""
    from serial.tools import list_ports

    return [i.device for i in list_ports.comports() if (i.vid, i.pid) == (p.USB_VID, p.USB_PID)]


@dataclass(frozen=True)
class FileHash:
    size: int
    crc32: int
    sha256: bytes


class Device:
    def __init__(self, transport: Transport, timeout_s: float = 3.0) -> None:
        self._transport = transport
        self._timeout_s = timeout_s
        self._seq = 0
        self._lock = threading.Lock()  # um comando por vez
        self._pending: dict[int, tuple[threading.Event, list[bytes]]] = {}
        self._pending_lock = threading.Lock()
        self._alive = True
        self.on_samples: Callable[[list[p.Sample], int], None] | None = None
        self.on_event: Callable[[int, bytes], None] | None = None
        self.on_lost: Callable[[], None] | None = None
        self._thread = threading.Thread(target=self._reader, name="device-rx", daemon=True)
        self._thread.start()

    def close(self) -> None:
        self._alive = False
        self._thread.join(timeout=1.0)
        self._transport.close()

    def _reader(self) -> None:
        splitter = p.FrameSplitter()
        while self._alive:
            try:
                chunk = self._transport.read()
            except Exception:
                if self._alive and self.on_lost:
                    self.on_lost()
                self._alive = False
                return
            if chunk:
                for frame in splitter.feed(chunk):
                    self._dispatch(frame)

    def _dispatch(self, frame: p.Frame) -> None:
        if frame.type == p.TYPE_RSP:
            with self._pending_lock:
                waiter = self._pending.pop(frame.seq, None)
            if waiter:
                waiter[1].append(frame.payload)
                waiter[0].set()
        elif frame.type == p.TYPE_STREAM and self.on_samples:
            dropped, samples = p.parse_stream(frame.payload)
            self.on_samples(samples, dropped)
        elif frame.type == p.TYPE_EVT and self.on_event:
            self.on_event(frame.cmd, frame.payload)

    def request(self, cmd: int, payload: bytes = b"", timeout_s: float | None = None) -> bytes:
        """Devolve a resposta com status OK; recusa vira DeviceError."""
        with self._lock:
            self._seq = (self._seq % 0xFFFF) + 1
            seq = self._seq
            done = threading.Event()
            box: list[bytes] = []
            with self._pending_lock:
                self._pending[seq] = (done, box)
            try:
                self._transport.write(p.encode_frame(p.Frame(p.TYPE_REQ, seq, cmd, payload)))
            except Exception as exc:
                raise LinkLost(str(exc)) from exc
            if not done.wait(timeout_s or self._timeout_s):
                with self._pending_lock:
                    self._pending.pop(seq, None)
                raise LinkLost("sem resposta do equipamento")
        rsp = box[0]
        if rsp[0] != p.RSP_OK:
            raise DeviceError(rsp[0])
        return rsp

    def hello(self) -> p.Hello:
        return p.parse_hello(self.request(p.CMD_HELLO))

    def status(self) -> p.Status:
        return p.parse_status(self.request(p.CMD_GET_STATUS))

    def set_setpoint(self, mmhg: float) -> bool:
        """Devolve True se ficou acima do limite recomendado."""
        rsp = self.request(p.CMD_SET_SETPOINT, struct.pack("<f", mmhg))
        return len(rsp) >= 2 and rsp[1] == 1

    def set_datetime(self, when: datetime) -> None:
        self.request(p.CMD_SET_DATETIME, struct.pack("<HBBBBB", when.year, when.month, when.day,
                                                     when.hour, when.minute, when.second))

    def set_operator(self, operator_id: int, participant_id: int) -> None:
        self.request(p.CMD_SET_OPERATOR, struct.pack("<HH", operator_id, participant_id))

    def start_test(self) -> None:
        self.request(p.CMD_START_TEST)

    def end_test(self) -> None:
        """Desinfla: no plato e' o fim normal do ensaio."""
        self.request(p.CMD_ABORT_TEST)

    def abort_test(self) -> None:
        """Cancela o ensaio em qualquer fase: desinfla e registra NC07."""
        self.request(p.CMD_ABORT_TEST, b"\x01")

    def stream_start(self, rate_hz: int = p.USER_STREAM_MAX_HZ) -> int:
        return struct.unpack("<H", self.request(p.CMD_STREAM_START, struct.pack("<H", rate_hz))[1:3])[0]

    def stream_stop(self) -> None:
        self.request(p.CMD_STREAM_STOP)

    def log_tail(self, count: int = 24) -> list[p.LogLine]:
        return p.parse_log_tail(self.request(p.CMD_GET_LOG_TAIL, bytes([count, 0])))

    def fs_list(self, path: str) -> list[p.FsEntry]:
        entries: list[p.FsEntry] = []
        while True:
            page, more = p.parse_fs_list(self.request(p.CMD_FS_LIST, struct.pack("<H", len(entries)) + path.encode()))
            entries += page
            if not more or not page:
                return entries

    def fs_size(self, path: str) -> int:
        return struct.unpack("<I", self.request(p.CMD_FS_STAT, path.encode())[2:6])[0]

    def fs_read(self, path: str, offset: int = 0,
                progress: Callable[[int], None] | None = None) -> bytes:
        """Le de `offset` ate' o fim do arquivo."""
        data = bytearray()
        while True:
            req = struct.pack("<IH", offset + len(data), p.FS_READ_MAX) + path.encode()
            rsp = self.request(p.CMD_FS_READ, req)
            count = struct.unpack("<H", rsp[5:7])[0]
            data += rsp[7:7 + count]
            if progress:
                progress(len(data))
            if count < p.FS_READ_MAX:
                return bytes(data)

    def fs_hash(self, path: str) -> FileHash:
        rsp = self.request(p.CMD_FS_CRC, path.encode(), timeout_s=30.0)
        size, crc = struct.unpack("<II", rsp[1:9])
        return FileHash(size, crc, rsp[9:41])

    def fs_read_verified(self, path: str, progress: Callable[[int], None] | None = None) -> bytes:
        """Le o arquivo inteiro e confere com o SHA-256 calculado no equipamento."""
        data = self.fs_read(path, progress=progress)
        remote = self.fs_hash(path)
        if remote.size != len(data) or remote.sha256 != hashlib.sha256(data).digest():
            raise LinkLost(f"copia divergente do cartao: {path}")
        return data
