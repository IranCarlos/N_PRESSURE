#!/usr/bin/env python3
"""Gera build/installer/nPressure-Setup-<versao>.exe.

    python installer/build.py      (com o ambiente do pc/README.md)

Precisa de PyInstaller e Inno Setup 6 so nesta maquina. A pasta de trabalho fica
fora do OneDrive porque os caminhos do Qt passam do limite do Windows.
"""
import os
import shutil
import subprocess
import sys
from pathlib import Path

from PySide6.QtCore import QRectF, Qt
from PySide6.QtGui import QColor, QGuiApplication, QImage, QPainter, QPen

PC = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PC))
from app import __version__  # noqa: E402

WORK = Path(os.environ["LOCALAPPDATA"]) / "np-build"
OUT = PC.parent / "build" / "installer"
ISCC = [Path(os.environ["LOCALAPPDATA"]) / "Programs/Inno Setup 6/ISCC.exe",
        Path(r"C:\Program Files (x86)\Inno Setup 6\ISCC.exe")]


def make_icon(path: Path) -> None:
    """Mostrador de pressao simples."""
    image = QImage(256, 256, QImage.Format.Format_ARGB32)
    image.fill(Qt.GlobalColor.transparent)
    p = QPainter(image)
    p.setRenderHint(QPainter.RenderHint.Antialiasing)
    p.setBrush(QColor("#1F6FB2"))
    p.setPen(Qt.PenStyle.NoPen)
    p.drawEllipse(QRectF(8, 8, 240, 240))
    p.setBrush(Qt.BrushStyle.NoBrush)
    p.setPen(QPen(QColor("white"), 26))
    p.drawArc(QRectF(52, 52, 152, 152), -30 * 16, 240 * 16)
    p.setPen(QPen(QColor("white"), 18))
    p.drawLine(128, 128, 178, 84)
    p.end()
    if not image.save(str(path), "ICO"):
        raise SystemExit("falha ao gerar o icone")


def main() -> None:
    app = QGuiApplication([])  # noqa: F841 - QPainter exige a aplicacao
    shutil.rmtree(WORK, ignore_errors=True)
    WORK.mkdir(parents=True)
    OUT.mkdir(parents=True, exist_ok=True)
    make_icon(WORK / "nPressure.ico")
    env = dict(os.environ, NP_BUILD_DIR=str(WORK))
    subprocess.run([sys.executable, "-m", "PyInstaller", "--noconfirm", "--log-level", "WARN",
                    "--distpath", str(WORK / "dist"), "--workpath", str(WORK / "work"),
                    str(PC / "installer" / "nPressure.spec")], check=True, env=env)
    iscc = next((p for p in ISCC if p.exists()), None)
    if iscc is None:
        raise SystemExit("Inno Setup 6 nao encontrado")
    subprocess.run([str(iscc), "/Q", f"/DAppVersion={__version__}", f"/DOutDir={OUT}", f"/DBuildDir={WORK}",
                    str(PC / "installer" / "nPressure.iss")], check=True)
    print(OUT / f"nPressure-Setup-{__version__}.exe")


if __name__ == "__main__":
    main()
