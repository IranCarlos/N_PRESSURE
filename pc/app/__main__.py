"""nPressure, aplicativo de PC.

    python -m app              equipamento pela USB
    python -m app --virtual    dispositivo virtual, sem hardware
"""
import argparse
import sys

from PySide6.QtWidgets import QApplication

from app.main_window import MainWindow
from core.fake_device import FakeDevice


VIRTUAL_KEY = bytes(32)  # so' do dispositivo virtual: 64 zeros; nao abre o equipamento


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--virtual", action="store_true", help="usa o dispositivo virtual")
    args = parser.parse_args()
    app = QApplication(sys.argv)
    app.setApplicationName("nPressure")
    app.setStyle("Fusion")
    window = MainWindow((lambda: FakeDevice(dev_key=VIRTUAL_KEY)) if args.virtual else None)
    window.show()
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
