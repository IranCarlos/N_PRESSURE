"""Autoverificacao do programa instalado: `nPressure --check <arquivo>`.

Abre a janela de verdade contra o dispositivo virtual e exercita USB, PDF e XLSX.
O executavel nao tem console, entao o resultado vai para o arquivo.
"""
import tempfile
import time
import traceback
from datetime import date
from pathlib import Path

from PySide6.QtWidgets import QApplication
from serial.tools import list_ports

from app import __version__
from app.main_window import MainWindow
from core import reports
from core.fake_device import FakeDevice


def _checks(app: QApplication) -> list[str]:
    done = []
    window = MainWindow(FakeDevice)
    window.show()
    end = time.monotonic() + 1.0
    while time.monotonic() < end:
        app.processEvents()
        time.sleep(0.01)
    assert window.isVisible()
    window.close()
    done.append(f"janela ok ({app.platformName()})")
    done.append(f"portas USB/serial: {[p.device for p in list_ports.comports()]}")
    with tempfile.TemporaryDirectory() as tmp:
        folder = Path(tmp)
        today = date.today()
        reports.export_xlsx([], folder / "t.xlsx")
        reports.export_usage_pdf(reports.summarize([], today, today), folder / "u.pdf")
        reports.export_errors_pdf([], today, today, folder / "e.pdf")
        assert all((folder / n).stat().st_size > 0 for n in ("t.xlsx", "u.pdf", "e.pdf"))
    done.append("exportacao PDF e XLSX ok")
    return done


def run(app: QApplication, report: str) -> int:
    lines = [f"nPressure {__version__}"]
    code = 0
    try:
        lines += _checks(app)
        lines.append("RESULTADO OK")
    except Exception:  # o relatorio precisa sair mesmo se a verificacao quebrar
        lines += [traceback.format_exc(), "RESULTADO FALHOU"]
        code = 1
    Path(report).write_text("\n".join(lines) + "\n", encoding="utf-8")
    return code
