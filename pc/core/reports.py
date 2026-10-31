"""Exportacao de ensaios (CSV, XLSX, PDF) e relatorios de uso e de erros.

CSV da trilha e da curva conforme Docs/dev/formato-logs.md, secao 5.
"""
from __future__ import annotations

import csv
from collections import Counter
from collections.abc import Iterable
from dataclasses import dataclass
from datetime import date
from pathlib import Path

from .sdfiles import IndexEntry, LogRecord, TestRecord

NC_NAMES = {
    "NC00": "Conforme", "NC01": "Pressão-alvo não atingida", "NC02": "Instabilidade no platô",
    "NC03": "Insuflação fora da faixa", "NC04": "Platô fora da faixa", "NC05": "Descompressão fora da faixa",
    "NC06": "Pressão residual acima do limite", "NC07": "Interrompido pelo operador", "NC08": "Falha técnica",
}
TRAIL_COLUMNS = ["ID", "Participante", "DataHora", "Ciclo", "PressaoAlvo", "PressaoMaxima", "PlatoMedio",
                 "PlatoMinimo", "PlatoMaximo", "Insuflacao", "Plato", "Descompressao", "PressaoResidual",
                 "Status", "CodigoNC", "Dispositivo", "Firmware", "UltimaCalibracao", "Integridade"]


@dataclass(frozen=True)
class TestRow:
    entry: IndexEntry
    record: TestRecord | None
    integrity: str

    def values(self) -> list[object]:
        e, r = self.entry, self.record
        head = [e.test_id, r.participant if r else "", f"{e.when:%d/%m/%Y %H:%M:%S}", f"C{r.cycle}" if r else "",
                e.setpoint, e.peak]
        if r is None:
            return head + [""] * 7 + ["Conforme" if e.conforming else "Nao conforme", e.nc, "", "", "", self.integrity]
        return head + [r.plateau_mean, r.plateau_min, r.plateau_max, r.inflate_s, r.plateau_s, r.deflate_s, r.residual,
                       r.status, e.nc, r.device, r.firmware, r.calibration, self.integrity]


def export_csv(rows: Iterable[TestRow], path: Path) -> None:
    with open(path, "w", newline="", encoding="utf-8-sig") as f:
        writer = csv.writer(f)
        writer.writerow(TRAIL_COLUMNS)
        writer.writerows(r.values() for r in rows)


def export_curve_csv(points: list[tuple[float, float]], path: Path) -> None:
    with open(path, "w", newline="", encoding="ascii") as f:
        writer = csv.writer(f)
        writer.writerow(["Tempo_s", "Pressao_mmHg"])
        writer.writerows((f"{t:.2f}", f"{v:.1f}") for t, v in points)


def export_xlsx(rows: Iterable[TestRow], path: Path) -> None:
    from openpyxl import Workbook

    book = Workbook(write_only=True)
    sheet = book.create_sheet("Ensaios")
    sheet.append(TRAIL_COLUMNS)
    for row in rows:
        sheet.append(row.values())
    book.save(path)


def _pdf(path: Path, title: str, story_builder) -> None:
    from reportlab.lib import colors
    from reportlab.lib.pagesizes import A4, landscape
    from reportlab.lib.styles import getSampleStyleSheet
    from reportlab.lib.units import mm
    from reportlab.platypus import Paragraph, SimpleDocTemplate, Spacer, Table, TableStyle

    styles = getSampleStyleSheet()

    def table(data: list[list[object]], widths: list[float] | None = None) -> Table:
        t = Table(data, colWidths=widths, repeatRows=1)
        t.setStyle(TableStyle([
            ("FONTSIZE", (0, 0), (-1, -1), 8),
            ("BACKGROUND", (0, 0), (-1, 0), colors.HexColor("#E7EDF3")),
            ("FONTNAME", (0, 0), (-1, 0), "Helvetica-Bold"),
            ("GRID", (0, 0), (-1, -1), 0.25, colors.HexColor("#B8C2CC")),
            ("VALIGN", (0, 0), (-1, -1), "MIDDLE"),
        ]))
        return t

    doc = SimpleDocTemplate(str(path), pagesize=landscape(A4), leftMargin=12 * mm, rightMargin=12 * mm,
                            topMargin=12 * mm, bottomMargin=12 * mm, title=title)
    story = [Paragraph(title, styles["Title"]), Spacer(1, 4 * mm)]
    story += story_builder(styles, table, Paragraph, Spacer, mm)
    doc.build(story)


def _curve_drawing(points: list[tuple[float, float]], setpoint: float):
    from reportlab.graphics.charts.lineplots import LinePlot
    from reportlab.graphics.shapes import Drawing
    from reportlab.lib import colors

    drawing = Drawing(720, 200)
    plot = LinePlot()
    plot.x, plot.y, plot.width, plot.height = 40, 25, 660, 165
    plot.data = [points, [(points[0][0], setpoint), (points[-1][0], setpoint)]]
    plot.lines[0].strokeColor = colors.HexColor("#1F6FB2")
    plot.lines[0].strokeWidth = 1.2
    plot.lines[1].strokeColor = colors.HexColor("#8A94A0")
    plot.lines[1].strokeDashArray = (3, 3)
    plot.yValueAxis.valueMin = 0
    plot.xValueAxis.valueMin = 0
    drawing.add(plot)
    return drawing


def export_test_pdf(row: TestRow, points: list[tuple[float, float]], path: Path) -> None:
    e, r = row.entry, row.record

    def build(styles, table, Paragraph, Spacer, mm):
        data = [["Campo", "Valor"], ["Ensaio", e.test_id], ["Data e hora", f"{e.when:%d/%m/%Y %H:%M:%S}"],
                ["Operador", e.operator], ["Pressão-alvo", f"{e.setpoint:.1f} mmHg"],
                ["Pressão máxima", f"{e.peak:.1f} mmHg"],
                ["Resultado", f"{e.nc}: {NC_NAMES.get(e.nc, '')}"], ["Integridade", row.integrity]]
        if r:
            data[3:3] = [["Participante", r.participant], ["Ciclo", f"C{r.cycle}"]]
            data += [["Platô médio", f"{r.plateau_mean:.1f} mmHg"], ["Faixa do platô", f"{r.plateau_min:.1f} a {r.plateau_max:.1f} mmHg"],
                     ["Insuflação", f"{r.inflate_s:.1f} s"], ["Platô", f"{r.plateau_s:.1f} s"],
                     ["Descompressão", f"{r.deflate_s:.1f} s"], ["Pressão residual", f"{r.residual:.1f} mmHg"],
                     ["Dispositivo", r.device], ["Firmware", r.firmware], ["Última calibração", r.calibration or "não informada"]]
        story = [table(data, [50 * mm, 120 * mm]), Spacer(1, 5 * mm)]
        if points:
            story.append(_curve_drawing(points, e.setpoint))
        else:
            story.append(Paragraph("Curva de pressão não disponível para este ensaio.", styles["Normal"]))
        return story

    _pdf(path, f"Ensaio {e.test_id}", build)


@dataclass(frozen=True)
class UsageSummary:
    start: date
    end: date
    total: int
    conforming: int
    by_nc: dict[str, int]
    by_operator: dict[str, int]
    by_day: dict[date, int]


def summarize(entries: Iterable[IndexEntry], start: date, end: date) -> UsageSummary:
    chosen = [e for e in entries if start <= e.when.date() <= end]
    return UsageSummary(start, end, len(chosen), sum(e.conforming for e in chosen),
                        dict(Counter(e.nc for e in chosen)), dict(Counter(e.operator for e in chosen)),
                        dict(sorted(Counter(e.when.date() for e in chosen).items())))


def export_usage_pdf(summary: UsageSummary, path: Path) -> None:
    def build(styles, table, Paragraph, Spacer, mm):
        rate = (100.0 * summary.conforming / summary.total) if summary.total else 0.0
        story = [Paragraph(f"Período: {summary.start:%d/%m/%Y} a {summary.end:%d/%m/%Y}. "
                           f"{summary.total} ensaios, {summary.conforming} conformes ({rate:.1f}%).", styles["Normal"]),
                 Spacer(1, 4 * mm)]
        story.append(table([["Código", "Descrição", "Ensaios"]] +
                           [[k, NC_NAMES.get(k, ""), v] for k, v in sorted(summary.by_nc.items())]))
        story.append(Spacer(1, 4 * mm))
        story.append(table([["Operador", "Ensaios"]] + [[k, v] for k, v in sorted(summary.by_operator.items())]))
        story.append(Spacer(1, 4 * mm))
        story.append(table([["Data", "Ensaios"]] + [[f"{k:%d/%m/%Y}", v] for k, v in summary.by_day.items()]))
        return story

    _pdf(path, "Relatório de uso", build)


def errors_in_period(records: Iterable[LogRecord], start: date, end: date) -> list[LogRecord]:
    return [r for r in records if start.isoformat() <= r.when[:10] <= end.isoformat()]


def export_errors_pdf(records: list[LogRecord], start: date, end: date, path: Path) -> None:
    def build(styles, table, Paragraph, Spacer, mm):
        counts = Counter(r.code for r in records)
        story = [Paragraph(f"Período: {start:%d/%m/%Y} a {end:%d/%m/%Y}. {len(records)} erros registrados.",
                           styles["Normal"]), Spacer(1, 4 * mm)]
        if counts:
            story.append(table([["Código", "Ocorrências"]] + [[k, v] for k, v in sorted(counts.items())]))
            story.append(Spacer(1, 4 * mm))
            story.append(table([["Data e hora", "Código", "Estado", "Pressão", "Mensagem"]] +
                               [[r.when[:19].replace("T", " "), r.code, r.state, f"{r.pressure:.1f}", r.message]
                                for r in records]))
        return story

    _pdf(path, "Relatório de erros", build)
