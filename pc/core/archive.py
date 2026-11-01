"""Copia local dos registros do cartao, separada da interface.

Os arquivos ficam num diretorio local por numero de serie. Ensaios sao imutaveis
e so' sao baixados uma vez; index e logs crescem e sao atualizados do ponto em
que pararam, para sincronizar 1000 ensaios sem reler tudo.
"""
from __future__ import annotations

import hashlib
import shutil
from collections.abc import Callable
from dataclasses import dataclass, field
from pathlib import Path

from . import sdfiles as sd
from .device import Device, DeviceError
from . import protocol as p

Progress = Callable[[str], None]


def _safe(path: str) -> Path:
    return Path(*path.strip("/").split("/"))


class Archive:
    def __init__(self, root: Path) -> None:
        self.root = root

    def local(self, path: str) -> Path:
        return self.root / _safe(path)

    def _grow(self, device: Device, path: str) -> None:
        """Atualiza um arquivo que so' cresce."""
        target = self.local(path)
        have = target.stat().st_size if target.exists() else 0
        size = device.fs_size(path)
        if size < have:  # cartao trocado ou apagado: os IDs recomecam
            target.unlink()
            have = 0
            if path == "/tests/index.csv":
                shutil.rmtree(target.parent, ignore_errors=True)
        if size > have:
            target.parent.mkdir(parents=True, exist_ok=True)
            with open(target, "ab") as f:
                f.write(device.fs_read(path, offset=have))
        # a copia tem que ser igual ao cartao: e' ele que a verificacao avalia
        if hashlib.sha256(target.read_bytes()).digest() != device.fs_hash(path).sha256:
            target.write_bytes(device.fs_read_verified(path))

    def _fetch_once(self, device: Device, path: str) -> None:
        target = self.local(path)
        if target.exists():
            return
        try:
            data = device.fs_read_verified(path)
        except DeviceError as exc:
            if exc.status == p.RSP_NOT_FOUND:
                return  # ensaio antigo sem curva
            raise
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)

    def sync(self, device: Device, progress: Progress | None = None) -> None:
        def tell(text: str) -> None:
            if progress:
                progress(text)

        tell("Lendo índice de ensaios")
        try:
            self._grow(device, "/tests/index.csv")
        except DeviceError as exc:
            if exc.status != p.RSP_NOT_FOUND:
                raise
            shutil.rmtree(self.local("/tests"), ignore_errors=True)  # cartao sem ensaios
        for kind in ("events", "errors"):
            folder = f"/logs/{kind}"
            try:
                names = [e.name for e in device.fs_list(folder) if e.name.endswith(".log")]
            except DeviceError:
                continue
            for name in names:
                tell(f"Lendo log {name}")
                self._grow(device, f"{folder}/{name}")
            for stale in self.logs(kind):
                if stale.name not in names:
                    stale.unlink()
        entries = self.index()
        for i, entry in enumerate(entries, 1):
            if not self.local(entry.rec_path).exists():
                tell(f"Baixando ensaio {i} de {len(entries)}")
            self._fetch_once(device, entry.rec_path)
            self._fetch_once(device, entry.csv_path)

    def index(self) -> list[sd.IndexEntry]:
        path = self.local("/tests/index.csv")
        if not path.exists():
            return []
        return sd.parse_index(path.read_text("ascii", "replace"))

    def record(self, entry: sd.IndexEntry) -> sd.TestRecord | None:
        path = self.local(entry.rec_path)
        if not path.exists():
            return None
        records = sd.parse_rec(path.read_text("ascii", "replace"))
        return next((r for r in records if r.when == entry.when), None)

    def curve_bytes(self, entry: sd.IndexEntry) -> bytes | None:
        path = self.local(entry.csv_path)
        return path.read_bytes() if path.exists() else None

    def curve(self, entry: sd.IndexEntry) -> list[tuple[float, float]]:
        data = self.curve_bytes(entry)
        return sd.parse_curve(data.decode("ascii", "replace")) if data else []

    def logs(self, kind: str) -> list[Path]:
        return sorted(self.local(f"/logs/{kind}").glob("*.log"))

    def verify(self) -> "IntegrityResult":
        result = IntegrityResult()
        for kind in ("events", "errors"):
            previous = None
            for path in self.logs(kind):
                report, records = sd.verify_log(f"{kind}/{path.name}", path.read_text("ascii", "replace"), previous)
                previous = report.last_hash or None
                result.logs.append(report)
                result.issues += report.issues
                if kind == "events":
                    result.records += records
                else:
                    result.errors += records
        hashes = sd.test_hashes(result.records)
        for entry in self.index():
            if entry.hash_ok:
                verdict, reason = sd.verify_test(entry.test_id, self.record(entry), self.curve_bytes(entry), hashes)
            else:
                verdict, reason = sd.TAMPERED, "Linha do índice não confere"
            result.tests[entry] = verdict
            if verdict != sd.INTACT:
                result.issues.append(sd.Issue(entry.test_id, 0, f"{entry.when:%Y-%m-%d %H:%M:%S}", reason, entry.nc,
                                              verdict == sd.TAMPERED))
        return result


@dataclass
class IntegrityResult:
    logs: list[sd.ChainReport] = field(default_factory=list)
    records: list[sd.LogRecord] = field(default_factory=list)
    errors: list[sd.LogRecord] = field(default_factory=list)
    tests: dict[sd.IndexEntry, str] = field(default_factory=dict)
    issues: list[sd.Issue] = field(default_factory=list)

    @property
    def verdict(self) -> str:
        states = [r.verdict for r in self.logs] + list(self.tests.values())
        if sd.TAMPERED in states:
            return sd.TAMPERED
        return sd.UNVERIFIABLE if sd.UNVERIFIABLE in states or not states else sd.INTACT
