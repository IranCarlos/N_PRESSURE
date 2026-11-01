"""Leitura dos arquivos do cartao SD e verificacao de integridade.

Formatos em Core/Services/Audit_Trail/Src/storage_format.c. Linhas gravadas por
firmware antigo sairam cortadas em 255 bytes: nao tem hash completo e por isso
contam como nao verificaveis, nunca como integras.
"""
from __future__ import annotations

import csv
import hashlib
import io
import re
from dataclasses import dataclass, field
from datetime import datetime

GENESIS = "0" * 64
_LOG_RE = re.compile(r"^(.*;prev=([0-9a-f]{64}));hash=([0-9a-f]{64})$")
_REC_RE = re.compile(r"rec=([0-9a-f]{24}) csv=([0-9a-f]{24}|-)")
_NUMBER = r"(-?\d+(?:\.\d+)?)"

INTACT, TAMPERED, UNVERIFIABLE = "ÍNTEGRO", "ADULTERADO", "NÃO VERIFICÁVEL"


@dataclass(frozen=True)
class IndexEntry:
    test_id: str
    when: datetime
    operator: str
    setpoint: float
    peak: float
    nc: str
    hash: str
    line: str

    @property
    def conforming(self) -> bool:
        return self.nc == "NC00"

    @property
    def rec_path(self) -> str:
        return f"/tests/{self.when:%Y/%m}/{self.test_id}.rec"

    @property
    def csv_path(self) -> str:
        return f"/tests/{self.when:%Y/%m}/{self.test_id}.csv"

    @property
    def hash_ok(self) -> bool:
        body = self.line[:-len(self.hash)]
        return hashlib.sha256((GENESIS + body).encode("ascii", "replace")).hexdigest() == self.hash


def parse_index(text: str) -> list[IndexEntry]:
    entries = []
    for line in text.splitlines():
        parts = line.split(";")
        if len(parts) != 7:
            continue
        try:
            when = datetime.strptime(parts[1], "%d/%m/%Y %H:%M:%S")
            entries.append(IndexEntry(parts[0], when, parts[2], float(parts[3]), float(parts[4]),
                                      parts[5], parts[6], line))
        except ValueError:
            continue
    return entries


@dataclass(frozen=True)
class TestRecord:
    test_id: str
    participant: str
    operator: str
    when: datetime
    cycle: int
    setpoint: float
    peak: float
    plateau_mean: float
    plateau_min: float
    plateau_max: float
    inflate_s: float
    plateau_s: float
    deflate_s: float
    residual: float
    status: str
    nc: str
    error: str
    device: str
    firmware: str
    calibration: str
    complete: bool
    line: str


def parse_rec_line(line: str) -> TestRecord | None:
    """None se o trecho das metricas nao veio inteiro."""
    f = [x.strip() for x in line.split(" | ")]
    if len(f) < 16:
        return None

    def number(item: str, pattern: str, group: int = 1) -> float:
        match = re.fullmatch(pattern, item)
        if not match:
            raise ValueError(item)
        return float(match.group(group))

    try:
        low_high = re.fullmatch(rf"Faixa {_NUMBER}-{_NUMBER}", f[8])
        if not low_high:
            raise ValueError(f[8])
        complete = len(f) >= 19 and f[18].startswith("ultima calibracao ") and \
            re.search(r"\d\d:\d\d:\d\d$", f[18]) is not None
        return TestRecord(
            f[0], f[1], f[2], datetime.strptime(f[3], "%d/%m/%Y %H:%M:%S"),
            int(number(f[4], r"Ciclo (\d+)")), number(f[5], rf"Alvo {_NUMBER} mmHg"),
            number(f[6], rf"Max {_NUMBER}"), number(f[7], rf"Plato medio {_NUMBER}"),
            float(low_high.group(1)), float(low_high.group(2)),
            number(f[9], rf"Insuflacao {_NUMBER} s"), number(f[10], rf"Plato {_NUMBER} s"),
            number(f[11], rf"Descompressao {_NUMBER} s"), number(f[12], rf"Pressao residual {_NUMBER} mmHg"),
            f[13], f[14], f[15], f[16] if len(f) > 16 else "", f[17] if len(f) > 17 else "",
            f[18].removeprefix("ultima calibracao ") if complete else "", complete, line)
    except ValueError:
        return None


def parse_rec(text: str) -> list[TestRecord]:
    return [r for r in map(parse_rec_line, text.splitlines()) if r is not None]


def parse_curve(text: str) -> list[tuple[float, float]]:
    points = []
    for row in csv.reader(io.StringIO(text)):
        try:
            points.append((float(row[0]), float(row[1])))
        except (IndexError, ValueError):
            continue
    return points


@dataclass(frozen=True)
class Issue:
    """Uma ocorrencia para mostrar ao usuario: onde, quando, o que e o trecho."""

    source: str
    line: int
    when: str
    kind: str
    detail: str
    tampered: bool


@dataclass
class ChainReport:
    """Resultado da verificacao de um arquivo de log."""

    name: str
    lines: int = 0
    verified: int = 0
    unverifiable: int = 0
    problems: list[str] = field(default_factory=list)
    issues: list[Issue] = field(default_factory=list)
    last_hash: str = ""

    @property
    def verdict(self) -> str:
        if self.problems:
            return TAMPERED
        return UNVERIFIABLE if self.unverifiable or not self.verified else INTACT


@dataclass(frozen=True)
class LogRecord:
    when: str
    level: str
    code: str
    test: str
    state: str
    pressure: float
    message: str
    verified: bool


def _log_fields(line: str) -> dict[str, str]:
    head, *rest = line.split(";")
    fields = {"when": head}
    for item in rest:
        key, _, value = item.partition("=")
        fields[key] = value
    return fields


def verify_log(name: str, text: str, previous_hash: str | None = None) -> tuple[ChainReport, list[LogRecord]]:
    """Confere o hash de cada linha e o elo com a linha anterior."""
    report = ChainReport(name)
    records = []
    expected = previous_hash
    seen: set[str] = set()

    def note(number: int, fields: dict[str, str], kind: str, tampered: bool, line: str) -> None:
        detail = fields.get("msg") or line[:80]
        report.issues.append(Issue(name, number, fields["when"][:19].replace("T", " "), kind, detail, tampered))

    for number, line in enumerate(text.splitlines(), 1):
        if not line:
            continue
        report.lines += 1
        match = _LOG_RE.match(line)
        fields = _log_fields(line)
        if not match:
            # cortada: sem hash nao ha' como conferir, e o proximo elo fica sem referencia
            report.unverifiable += 1
            expected = None
            note(number, fields, "Linha incompleta", False, line)
        else:
            body, prev, digest = match.groups()
            if hashlib.sha256((prev + body).encode("ascii", "replace")).hexdigest() != digest:
                report.problems.append(f"linha {number}: conteúdo alterado")
                note(number, fields, "Conteúdo alterado", True, line)
            elif expected is not None and prev == GENESIS:
                # firmware antigo recomecava a corrente a cada boot: nao prova nem nega nada
                report.unverifiable += 1
                note(number, fields, "Corrente reiniciada", False, line)
            elif expected is not None and prev != expected and prev in seen:
                # O firmware retomou de uma linha que continua no arquivo: nada sumiu, mas
                # este elo nao comprova a sequencia (retomada errada ate' 2026-10-31).
                report.unverifiable += 1
                note(number, fields, "Retomada após reinício", False, line)
            elif expected is not None and prev != expected:
                report.problems.append(f"linha {number}: elo quebrado (linha removida ou inserida)")
                note(number, fields, "Linha removida ou inserida", True, line)
            else:
                report.verified += 1
            expected = digest
            seen.add(digest)
            report.last_hash = digest
        try:
            pressure = float(fields.get("p", "0") or 0)
        except ValueError:
            pressure = 0.0
        records.append(LogRecord(fields["when"], fields.get("lvl", ""), fields.get("code", ""),
                                 fields.get("test", ""), fields.get("state", ""), pressure,
                                 fields.get("msg", ""), match is not None))
    return report, records


def test_hashes(records: list[LogRecord]) -> dict[str, tuple[str, str]]:
    """Hashes de .rec e .csv registrados na corrente, por ID de ensaio."""
    found = {}
    for rec in records:
        if rec.code == "REC":
            match = _REC_RE.search(rec.message)
            if match:
                found[rec.test] = match.groups()
    return found


def verify_test(test_id: str, record: TestRecord | None, curve_bytes: bytes | None,
                hashes: dict[str, tuple[str, str]]) -> tuple[str, str]:
    """Compara um ensaio com os hashes guardados na corrente de eventos: (veredito, motivo)."""
    known = hashes.get(test_id)
    if known is None:
        return UNVERIFIABLE, "Sem hash do ensaio no log"
    if record is None:
        return UNVERIFIABLE, "Registro do ensaio não encontrado"
    if hashlib.sha256(record.line.encode("ascii", "replace")).hexdigest()[:24] != known[0]:
        return TAMPERED, "Registro (.rec) não confere com o log"
    if known[1] != "-":
        if curve_bytes is None:
            return TAMPERED, "Curva (.csv) ausente"
        if hashlib.sha256(curve_bytes).hexdigest()[:24] != known[1]:
            return TAMPERED, "Curva (.csv) não confere com o log"
    return INTACT, ""
