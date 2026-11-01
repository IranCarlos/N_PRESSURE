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


KEY = bytes(range(32))


@pytest.fixture
def dev_window(qapp, monkeypatch, tmp_path):
    monkeypatch.setattr(QStandardPaths, 'writableLocation', staticmethod(lambda *_: str(tmp_path)))
    fakes = []

    def make():
        fakes.append(FakeDevice(dev_key=KEY, time_scale=2.0))
        return fakes[-1]

    w = MainWindow(make)
    w.show()
    w._toggle_connection()
    assert pump(qapp, lambda: w.device is not None and w.status is not None)
    w.fake = fakes[0]
    yield w
    w.close()


def test_dev_tabs_only_after_login_and_gone_after_logout(qapp, dev_window):
    w = dev_window
    assert w.tabs.count() == 4
    w._login(KEY.hex())
    assert pump(qapp, lambda: w.dev_panel is not None)
    assert w.tabs.count() == 5
    assert pump(qapp, lambda: w.dev_panel.param_table.rowCount() == 4)
    w._toggle_dev()
    assert w.dev_panel is None and w.tabs.count() == 4
    assert pump(qapp, lambda: w.fake.role == 0)


def test_wrong_key_opens_nothing(qapp, dev_window, monkeypatch):
    w = dev_window
    shown = []
    monkeypatch.setattr(QMessageBox, 'warning', staticmethod(lambda *a, **k: shown.append(a[2])))
    w._login(('00' * 32))
    assert pump(qapp, lambda: shown)
    assert w.dev_panel is None and w.fake.role == 0


def test_session_expiry_closes_dev(qapp, dev_window, monkeypatch):
    w = dev_window
    monkeypatch.setattr(QMessageBox, 'information', staticmethod(lambda *a, **k: None))
    w._login(KEY.hex())
    assert pump(qapp, lambda: w.dev_panel is not None)
    w._on_event(p.EVT_AUTH_EXPIRED, b'')
    assert w.dev_panel is None and w.tabs.count() == 4


def test_manual_tab_drives_pump_and_stops_safe(qapp, dev_window, monkeypatch):
    w = dev_window
    monkeypatch.setattr(QMessageBox, 'question', staticmethod(lambda *a, **k: QMessageBox.StandardButton.Yes))
    w._login(KEY.hex())
    assert pump(qapp, lambda: w.dev_panel is not None)
    panel = w.dev_panel
    panel.manual_button.click()
    assert pump(qapp, lambda: panel._manual is not None)
    panel.pump_slider.setValue(40)
    panel.valve_boxes[2].setChecked(True)
    assert pump(qapp, lambda: w.fake.pump == 40 and w.fake.valves == 4)
    pump(qapp, lambda: False, 0.5)
    assert w.fake.state == p.ST_MANUAL
    panel.manual_stop.click()
    assert pump(qapp, lambda: w.fake.state == p.ST_IDLE and w.fake.pump == 0)


def test_declined_confirmation_never_enters_manual(qapp, dev_window, monkeypatch):
    w = dev_window
    monkeypatch.setattr(QMessageBox, 'question', staticmethod(lambda *a, **k: QMessageBox.StandardButton.Cancel))
    w._login(KEY.hex())
    assert pump(qapp, lambda: w.dev_panel is not None)
    w.dev_panel.manual_button.click()
    pump(qapp, lambda: False, 0.3)
    assert w.fake.state == p.ST_IDLE


def test_leak_tab_gives_verdict(qapp, dev_window, monkeypatch):
    w = dev_window
    monkeypatch.setattr(QMessageBox, 'question', staticmethod(lambda *a, **k: QMessageBox.StandardButton.Yes))
    w._login(KEY.hex())
    assert pump(qapp, lambda: w.dev_panel is not None)
    panel = w.dev_panel
    panel.leak_target.setValue(40.0)
    panel.leak_hold.setValue(5)
    panel.leak_start.click()
    assert pump(qapp, lambda: panel.leak_result.text() == 'APROVADO', 45.0), panel.leak_detail.text()
    assert pump(qapp, lambda: w.fake.state == p.ST_IDLE)


def test_disconnect_with_manual_on_leaves_device_safe(qapp, dev_window, monkeypatch):
    w = dev_window
    fake = w.fake
    monkeypatch.setattr(QMessageBox, 'question', staticmethod(lambda *a, **k: QMessageBox.StandardButton.Yes))
    w._login(KEY.hex())
    assert pump(qapp, lambda: w.dev_panel is not None)
    w.dev_panel.manual_button.click()
    assert pump(qapp, lambda: w.dev_panel._manual is not None)
    w.dev_panel.pump_slider.setValue(30)
    assert pump(qapp, lambda: fake.pump == 30)
    w._disconnect()
    assert pump(qapp, lambda: fake.state == p.ST_IDLE and fake.pump == 0)


def test_calibration_needs_zero_point(qapp, dev_window):
    w = dev_window
    w._login(KEY.hex())
    assert pump(qapp, lambda: w.dev_panel is not None)
    panel = w.dev_panel
    panel._cal_refs = [50.0, 100.0, 150.0]
    panel._cal_apply()
    pump(qapp, lambda: False, 0.3)
    assert 'Falta o ponto em zero' in panel.cal_log.toPlainText()
    assert w.fake.cal_offset == 0.0
