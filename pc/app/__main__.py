"""nPressure, aplicativo de PC.

    python -m app              equipamento pela USB
    python -m app --virtual    dispositivo virtual, sem hardware
"""
import argparse
import sys

from PySide6.QtWidgets import QApplication

from app.main_window import MainWindow
from core.fake_device import FakeDevice


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--virtual", action="store_true", help="usa o dispositivo virtual")
    args = parser.parse_args()
    app = QApplication(sys.argv)
    app.setApplicationName("nPressure")
    app.setStyle("Fusion")
    window = MainWindow(FakeDevice if args.virtual else None)
    window.show()
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
