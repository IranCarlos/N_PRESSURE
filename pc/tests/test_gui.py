"""Roda a janela de verdade, sem tela, contra o dispositivo virtual."""
import os
import struct
import time
from pathlib import Path

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

import pytest
from PySide6.QtCore import QStandardPaths
from PySide6.QtWidgets import QApplication, QMessageBox, QPushButton

from app.main_window import MainWindow
from core import protocol as p
from core import sdfiles as sd
from core.fake_device import FakeDevice


@pytest.fixture(scope="module")
def qapp():
    QStandardPaths.setTestModeEnabled(True)
    yield QApplication.instance() or QApplication([])


def pump(qapp, cond, seconds=8.0):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        qapp.processEvents()
        if cond():
            return True
        time.sleep(0.005)
    return False


@pytest.fixture
def window(qapp, monkeypatch, tmp_path):
    monkeypatch.setattr(QStandardPaths, "writableLocation", staticmethod(lambda *_: str(tmp_path)))
    w = MainWindow(lambda: FakeDevice(time_scale=25.0))
    w.show()
    w._toggle_connection()
    assert pump(qapp, lambda: w.device is not None and w.status is not None)
    yield w
    w.close()


def test_no_control_screens(window):
    labels = [window.tabs.tabText(i) for i in range(window.tabs.count())]
    assert labels == ["Operação", "Ensaios", "Relatórios", "Integridade"]
    texts = " ".join(b.text().lower() for b in window.findChildren(QPushButton))
    for word in ("calibra", "parâmetro", "manual", "válvula", "pid"):
        assert word not in texts


def test_full_cycle_then_history(qapp, window, monkeypatch):
    monkeypatch.setattr(QMessageBox, "question", staticmethod(lambda *a, **k: QMessageBox.StandardButton.Yes))
    assert window.test_button.text() == "Inflar" and window.test_button.isEnabled()
    assert not window.abort_button.isEnabled()
    window._start_test()
    assert pump(qapp, lambda: window.status.state in p.ACTIVE_STATES)
    assert pump(qapp, lambda: window.test_button.text() == "Pronto p/ desinflar")
    assert window.test_button.isEnabled() and window.abort_button.isEnabled()
    assert pump(qapp, lambda: window.status.state == p.ST_IDLE)
    assert pump(qapp, lambda: len(window._p) > 5)
    window._sync()
    assert pump(qapp, lambda: window.table.rowCount() == 1)
    assert window.integrity.verdict == sd.INTACT
    assert window.verdict_label.text() == sd.INTACT
    window.table.selectRow(0)
    qapp.processEvents()
    assert "T00000001" in window.detail_label.text()
    assert "Sem curva" not in window.detail_label.text()


def test_abort_is_available_while_active(qapp, window, monkeypatch):
    monkeypatch.setattr(QMessageBox, "question", staticmethod(lambda *a, **k: QMessageBox.StandardButton.Yes))
    window._start_test()
    assert pump(qapp, lambda: window.abort_button.isEnabled())
    window._abort_test()
    assert pump(qapp, lambda: window.status.state == p.ST_IDLE)


def test_cancel_does_not_start(qapp, window, monkeypatch):
    monkeypatch.setattr(QMessageBox, "question", staticmethod(lambda *a, **k: QMessageBox.StandardButton.Cancel))
    window._start_test()
    pump(qapp, lambda: False, 0.3)
    assert window.status.state == p.ST_IDLE


def test_link_loss_during_test_warns(qapp, window, monkeypatch):
    shown = []
    monkeypatch.setattr(QMessageBox, "critical", staticmethod(lambda *a, **k: shown.append(a[2])))
    window.status = p.Status(p.ST_HOLD, 100.0, 100.0, True, True, 0, 1)
    window._on_lost()
    assert window.device is None
    assert "pedal" in shown[0]
    assert not window.abort_button.isEnabled()


def test_plateau_button_ends_normally_and_abort_cancels(qapp, window, monkeypatch):
    monkeypatch.setattr(QMessageBox, "question", staticmethod(lambda *a, **k: QMessageBox.StandardButton.Yes))
    window.device.request(p.CMD_SET_SETPOINT, struct.pack("<f", 40.0))
    outcomes = []
    for press in (window.test_button.click, window.abort_button.click):
        window._test_button_pressed()
        assert pump(qapp, lambda: window.test_button.text() == "Inflando...")
        assert not window.test_button.isEnabled()
        assert pump(qapp, lambda: window.status.state == p.ST_HOLD)
        press()
        assert pump(qapp, lambda: window.test_button.text() == "Desinflando")
        assert not window.abort_button.isEnabled()
        assert pump(qapp, lambda: window.test_button.text() == "Inflar")
        outcomes.append(True)
    window._sync()
    assert pump(qapp, lambda: window.table.rowCount() == 2)
    assert [r.entry.nc for r in window._rows] == ["NC07", "NC00"]


def test_busy_banner_shows_while_loading(qapp, window):
    window._reload_tests()
    assert window.busy_frame.isVisible()
    assert "Carregando" in window.busy_label.text()
    assert pump(qapp, lambda: not window.busy_frame.isVisible())
