"""Janela principal do nivel de usuario: operacao, ensaios, relatorios e integridade.

Nao ha' tela de parametros, calibracao ou modo manual: isso e' do nivel DEV (Fase 9).
Chamadas ao equipamento bloqueiam, entao rodam fora da thread da interface.
"""
from __future__ import annotations

from collections import deque
from dataclasses import replace
from datetime import date, datetime
from pathlib import Path

import pyqtgraph as pg
from PySide6.QtCore import QDate, QObject, QRunnable, QStandardPaths, Qt, QThreadPool, QTimer, Signal
from PySide6.QtGui import QColor
from PySide6.QtWidgets import (QAbstractItemView, QComboBox, QDateEdit, QDoubleSpinBox, QFileDialog, QFormLayout,
                               QFrame, QGridLayout, QGroupBox, QHBoxLayout, QHeaderView, QLabel, QMainWindow,
                               QMessageBox, QProgressBar, QPushButton, QSpinBox, QSplitter, QTableWidget,
                               QTableWidgetItem, QTabWidget, QVBoxLayout, QWidget)

from core import protocol as p
from core import reports, sdfiles as sd
from core.archive import Archive, IntegrityResult
from core.device import Device, DeviceError, LinkLost, SerialTransport, find_ports

INK, MUTED, OK, BAD, WARN = "#1B2733", "#5B6875", "#1E7B4F", "#B3261E", "#9A6700"
BLUE, GRAY = "#1F6FB2", "#7A8794"
# Botao principal: texto e cor por estado, nomes iguais aos do display.
TEST_BUTTON = {
    p.ST_IDLE: ("Inflar", OK),
    p.ST_INFLATING: ("Inflando...", BLUE),
    p.ST_STABILIZING: ("Estabilizando", BLUE),
    p.ST_HOLD: ("Pronto p/ desinflar", WARN),
    p.ST_DEFLATING: ("Desinflando", GRAY),
}
VERDICT_COLORS = {sd.INTACT: OK, sd.TAMPERED: BAD, sd.UNVERIFIABLE: WARN}
PLOT_SECONDS = 60.0

pg.setConfigOption("background", "#FFFFFF")
pg.setConfigOption("foreground", MUTED)
pg.setConfigOption("antialias", True)


class _Signals(QObject):
    done = Signal(object)
    failed = Signal(object)


class _Job(QRunnable):
    def __init__(self, fn) -> None:
        super().__init__()
        self.fn = fn
        self.signals = _Signals()

    def run(self) -> None:
        try:
            result = self.fn()
        except Exception as exc:  # o erro volta para a interface decidir
            self.signals.failed.emit(exc)
        else:
            self.signals.done.emit(result)


class Bridge(QObject):
    """Leva os callbacks da thread de recepcao para a thread da interface."""

    samples = Signal(object)
    event = Signal(int, bytes)
    lost = Signal()
    progress = Signal(str)


def paint_button(button: QPushButton, color: str) -> None:
    # desabilitado mantem a cor: o botao tambem mostra em que fase o ensaio esta'
    button.setStyleSheet(f"QPushButton,QPushButton:disabled{{background:{color};color:white;font-weight:600;"
                         "border:0;border-radius:4px}")


def big_button(text: str, color: str) -> QPushButton:
    button = QPushButton(text)
    button.setMinimumHeight(44)
    paint_button(button, color)
    return button


class MainWindow(QMainWindow):
    def __init__(self, transport_factory=None) -> None:
        super().__init__()
        self.setWindowTitle("nPressure")
        self.resize(1120, 720)
        self.device: Device | None = None
        self.hello: p.Hello | None = None
        self.archive: Archive | None = None
        self.integrity: IntegrityResult | None = None
        self.status: p.Status | None = None
        self._transport_factory = transport_factory
        self._jobs: set[_Job] = set()
        self._t = deque(maxlen=3000)
        self._p = deque(maxlen=3000)
        self._sp = deque(maxlen=3000)
        self._dirty = False
        self._polling = False
        self._rows: list[reports.TestRow] = []
        self._busy = 0
        self.bridge = Bridge()
        self.bridge.samples.connect(self._on_samples)
        self.bridge.event.connect(self._on_event)
        self.bridge.lost.connect(self._on_lost)
        self.bridge.progress.connect(self._busy_text)

        root = QWidget()
        layout = QVBoxLayout(root)
        layout.addLayout(self._connection_bar())
        layout.addWidget(self._busy_banner())
        self.tabs = QTabWidget()
        self.tabs.addTab(self._operation_tab(), "Operação")
        self.tabs.addTab(self._tests_tab(), "Ensaios")
        self.tabs.addTab(self._reports_tab(), "Relatórios")
        self.tabs.addTab(self._integrity_tab(), "Integridade")
        layout.addWidget(self.tabs)
        self.setCentralWidget(root)

        self._poll = QTimer(self)
        self._poll.setInterval(1000)
        self._poll.timeout.connect(self._poll_status)
        self._plot_timer = QTimer(self)
        self._plot_timer.setInterval(100)
        self._plot_timer.timeout.connect(self._refresh_plot)
        self._plot_timer.start()
        self._refresh_ports()
        self._apply_state()

    # --- Infra ---------------------------------------------------------

    def run(self, fn, done=None, failed=None) -> None:
        job = _Job(fn)
        job.setAutoDelete(False)
        self._jobs.add(job)

        def finish(handler, value) -> None:
            self._jobs.discard(job)
            if handler:
                handler(value)

        job.signals.done.connect(lambda v: finish(done, v))
        job.signals.failed.connect(lambda e: finish(failed or self._show_error, e))
        QThreadPool.globalInstance().start(job)

    def _busy_banner(self) -> QFrame:
        self.busy_frame = QFrame()
        self.busy_frame.setStyleSheet("QFrame{background:#E7F1FB;border-radius:4px}")
        row = QHBoxLayout(self.busy_frame)
        self.busy_label = QLabel()
        self.busy_label.setStyleSheet(f"color:{INK};font-weight:600")
        bar = QProgressBar()
        bar.setRange(0, 0)  # em movimento enquanto nao ha' percentual
        bar.setTextVisible(False)
        bar.setFixedHeight(10)
        row.addWidget(self.busy_label)
        row.addWidget(bar, 1)
        self.busy_frame.hide()
        return self.busy_frame

    def _busy_text(self, text: str) -> None:
        self.busy_label.setText(text)

    def _begin_busy(self, text: str) -> None:
        self._busy += 1
        self.busy_label.setText(text)
        self.busy_frame.show()

    def _end_busy(self) -> None:
        self._busy = max(0, self._busy - 1)
        self.busy_frame.setVisible(self._busy > 0)

    def _show_error(self, exc: Exception) -> None:
        if isinstance(exc, LinkLost):
            self._on_lost()
            return
        QMessageBox.warning(self, "Comando recusado", str(exc))

    # --- Conexao -------------------------------------------------------

    def _connection_bar(self) -> QHBoxLayout:
        bar = QHBoxLayout()
        self.port_box = QComboBox()
        self.port_box.setMinimumWidth(120)
        refresh = QPushButton("Atualizar")
        refresh.clicked.connect(self._refresh_ports)
        self.connect_button = QPushButton("Conectar")
        self.connect_button.clicked.connect(self._toggle_connection)
        self.device_label = QLabel("Desconectado")
        self.device_label.setStyleSheet(f"color:{MUTED}")
        for widget in (QLabel("Porta"), self.port_box, refresh, self.connect_button, self.device_label):
            bar.addWidget(widget)
        bar.addStretch(1)
        return bar

    def _refresh_ports(self) -> None:
        self.port_box.clear()
        if self._transport_factory:
            self.port_box.addItem("Virtual")
            return
        self.port_box.addItems(find_ports())

    def _toggle_connection(self) -> None:
        if self.device:
            self._disconnect()
            return
        factory = self._transport_factory
        port = self.port_box.currentText()
        if not factory and not port:
            QMessageBox.information(self, "Conexão", "Equipamento não encontrado. Confira o cabo USB.")
            return

        def connect() -> tuple[Device, p.Hello]:
            device = Device(factory() if factory else SerialTransport(port))
            try:
                hello = device.hello()
                if not hello.device_id.startswith(p.DEVICE_ID_PREFIX):
                    raise LinkLost("este equipamento não é reconhecido")
                device.stream_start(p.USER_STREAM_MAX_HZ)
            except Exception:
                device.close()
                raise
            return device, hello

        self.connect_button.setEnabled(False)
        self._begin_busy("Conectando ao equipamento")

        def failed(exc: Exception) -> None:
            self._end_busy()
            self.connect_button.setEnabled(True)
            QMessageBox.warning(self, "Conexão", f"Não foi possível conectar: {exc}")

        def done(result: tuple[Device, p.Hello]) -> None:
            self._end_busy()
            self._on_connected(result)

        self.run(connect, done, failed)

    def _on_connected(self, result: tuple[Device, p.Hello]) -> None:
        self.device, self.hello = result
        self.device.on_samples = lambda s, _d: self.bridge.samples.emit(s)
        self.device.on_event = lambda c, b: self.bridge.event.emit(c, b)
        self.device.on_lost = self.bridge.lost.emit
        self.connect_button.setEnabled(True)
        self.connect_button.setText("Desconectar")
        self.device_label.setText(f"{self.hello.device_id}  ·  FW {self.hello.firmware}  ·  série {self.hello.serial}")
        base = Path(QStandardPaths.writableLocation(QStandardPaths.StandardLocation.AppDataLocation))
        self.archive = Archive(base / self.hello.serial)
        self._t.clear(), self._p.clear(), self._sp.clear()
        self._poll.start()
        self._poll_status()
        self._reload_tests()
        self._apply_state()

    def _disconnect(self) -> None:
        device, self.device = self.device, None
        self._poll.stop()
        self.status = None
        if device:
            device.on_lost = None
            device.close()
        self.connect_button.setText("Conectar")
        self.device_label.setText("Desconectado")
        self._apply_state()

    def _on_lost(self) -> None:
        if not self.device:
            return
        active = self.status is not None and self.status.state in p.ACTIVE_STATES
        self._disconnect()
        text = "A conexão com o equipamento foi perdida."
        if active:
            text += "\n\nHavia um ensaio em andamento. Use o pedal ou o botão do equipamento para encerrá-lo."
        QMessageBox.critical(self, "Conexão perdida", text)

    def closeEvent(self, event) -> None:
        if self.device:
            self._disconnect()
        super().closeEvent(event)

    # --- Operacao ------------------------------------------------------

    def _operation_tab(self) -> QWidget:
        tab = QWidget()
        layout = QHBoxLayout(tab)
        side = QVBoxLayout()

        live = QGroupBox("Agora")
        grid = QGridLayout(live)
        self.pressure_label = QLabel("--")
        self.pressure_label.setStyleSheet(f"font-size:44px;font-weight:600;color:{INK}")
        self.state_label = QLabel("--")
        self.state_label.setStyleSheet("font-size:16px;font-weight:600")
        self.target_label, self.clock_label, self.sd_label, self.error_label = (QLabel("--") for _ in range(4))
        grid.addWidget(self.pressure_label, 0, 0, 1, 2)
        grid.addWidget(QLabel("mmHg"), 1, 0, 1, 2)
        grid.addWidget(self.state_label, 2, 0, 1, 2)
        for row, (name, label) in enumerate((("Alvo", self.target_label), ("Relógio", self.clock_label),
                                             ("Cartão SD", self.sd_label), ("Erro", self.error_label)), 3):
            grid.addWidget(QLabel(name), row, 0)
            grid.addWidget(label, row, 1)
        side.addWidget(live)

        test = QGroupBox("Ensaio")
        form = QFormLayout(test)
        self.setpoint_spin = QDoubleSpinBox()
        self.setpoint_spin.setRange(20.0, 180.0)
        self.setpoint_spin.setDecimals(0)
        self.setpoint_spin.setSuffix(" mmHg")
        self.setpoint_spin.setValue(100.0)
        self.operator_spin = QSpinBox()
        self.operator_spin.setRange(1, 9999)
        self.participant_spin = QSpinBox()
        self.participant_spin.setRange(1, 9999)
        form.addRow("Pressão-alvo", self.setpoint_spin)
        form.addRow("Operador", self.operator_spin)
        form.addRow("Participante", self.participant_spin)
        self.apply_button = QPushButton("Aplicar")
        self.apply_button.clicked.connect(self._apply_settings)
        form.addRow(self.apply_button)
        self.test_button = big_button("Inflar", GRAY)
        self.test_button.clicked.connect(self._test_button_pressed)
        self.abort_button = big_button("Abortar", BAD)
        self.abort_button.clicked.connect(self._abort_test)
        form.addRow(self.test_button)
        form.addRow(self.abort_button)
        side.addWidget(test)

        self.clock_button = QPushButton("Acertar relógio pelo PC")
        self.clock_button.clicked.connect(self._set_clock)
        side.addWidget(self.clock_button)
        side.addStretch(1)

        self.plot = pg.PlotWidget()
        self.plot.setLabel("left", "Pressão (mmHg)")
        self.plot.setLabel("bottom", "Tempo (s)")
        self.plot.showGrid(x=True, y=True, alpha=0.15)
        self.plot.setYRange(0, 190)
        self.plot.setMouseEnabled(False, False)
        self.plot.setMenuEnabled(False)
        self.curve = self.plot.plot(pen=pg.mkPen("#1F6FB2", width=2))
        self.target_curve = self.plot.plot(pen=pg.mkPen("#8A94A0", width=1, style=Qt.PenStyle.DashLine))
        layout.addLayout(side, 0)
        layout.addWidget(self.plot, 1)
        return tab

    def _poll_status(self) -> None:
        if not self.device or self._polling:
            return
        self._polling = True
        device = self.device

        def done(status: p.Status) -> None:
            self._polling = False
            self._show_status(status)

        def failed(exc: Exception) -> None:
            self._polling = False
            self._show_error(exc)

        self.run(device.status, done, failed)

    def _show_status(self, status: p.Status) -> None:
        self.status = status
        self.pressure_label.setText(f"{status.pressure:.1f}")
        self.state_label.setText(p.STATE_NAMES.get(status.state, str(status.state)))
        self.target_label.setText(f"{status.setpoint:.0f} mmHg")
        self.clock_label.setText("ok" if status.rtc_valid else "precisa acertar")
        self.clock_label.setStyleSheet(f"color:{OK if status.rtc_valid else WARN}")
        self.sd_label.setText("ok" if status.sd_mounted else "ausente")
        self.sd_label.setStyleSheet(f"color:{OK if status.sd_mounted else BAD}")
        name = p.ERROR_NAMES.get(status.error, "")
        self.error_label.setText(f"E{status.error:03d} {name}" if status.error else "nenhum")
        self.error_label.setStyleSheet(f"color:{BAD if status.error else OK}")
        self._apply_state()

    def _apply_state(self) -> None:
        connected = self.device is not None
        state = self.status.state if self.status else None
        idle = connected and state == p.ST_IDLE
        active = connected and state in p.ACTIVE_STATES
        text, color = TEST_BUTTON.get(state, ("Inflar", GRAY))
        self.test_button.setText(text)
        paint_button(self.test_button, color if connected and state in TEST_BUTTON else GRAY)
        self.test_button.setEnabled(idle or state == p.ST_HOLD)
        self.abort_button.setEnabled(active and state != p.ST_DEFLATING)
        paint_button(self.abort_button, BAD if self.abort_button.isEnabled() else GRAY)
        for widget in (self.apply_button, self.clock_button, self.setpoint_spin, self.operator_spin, self.participant_spin):
            widget.setEnabled(idle)
        self.sync_button.setEnabled(idle)
        if not connected:
            for label in (self.pressure_label, self.state_label, self.target_label, self.clock_label, self.sd_label,
                          self.error_label):
                label.setText("--")
                label.setStyleSheet(label.styleSheet().split("color:")[0])

    def _on_samples(self, samples: list[p.Sample]) -> None:
        for s in samples:
            self._t.append(s.t_ms / 1000.0)
            self._p.append(s.pressure)
            self._sp.append(s.setpoint)
        if samples:
            self._dirty = True
            # a telemetria chega antes da consulta de 1 s: o botao muda na hora
            if self.status and samples[-1].state != self.status.state:
                self.status = replace(self.status, state=samples[-1].state)
                self.state_label.setText(p.STATE_NAMES.get(self.status.state, ""))
                self._apply_state()

    def _refresh_plot(self) -> None:
        if not self._dirty:
            return
        self._dirty = False
        now = self._t[-1]
        keep = [i for i, t in enumerate(self._t) if t >= now - PLOT_SECONDS]
        first = keep[0] if keep else 0
        xs = [t - now for t in list(self._t)[first:]]
        self.curve.setData(xs, list(self._p)[first:])
        self.target_curve.setData(xs, list(self._sp)[first:])
        self.pressure_label.setText(f"{self._p[-1]:.1f}")

    def _on_event(self, cmd: int, payload: bytes) -> None:
        if cmd == p.EVT_STATE:
            self._poll_status()
        elif cmd == p.EVT_TEST_DONE:
            done = p.parse_test_done(payload)
            text = f"{done.nc_code}: {reports.NC_NAMES.get(done.nc_code, '')}. Pico {done.peak:.1f} mmHg."
            self.statusBar().showMessage(f"Ensaio concluído. {text}", 20000)
        elif cmd == p.EVT_ERROR and payload and payload[0]:
            name = p.ERROR_NAMES.get(payload[0], "Erro do equipamento")
            self.statusBar().showMessage(f"E{payload[0]:03d}: {name}", 20000)
            self._poll_status()

    def _apply_settings(self) -> None:
        device = self.device
        value, operator, participant = self.setpoint_spin.value(), self.operator_spin.value(), self.participant_spin.value()

        def apply() -> bool:
            warn = device.set_setpoint(value)
            device.set_operator(operator, participant)
            return warn

        def done(warn: bool) -> None:
            self.statusBar().showMessage("Ajustes aplicados.", 5000)
            if warn:
                QMessageBox.warning(self, "Pressão-alvo", "Alvo acima do limite recomendado: a margem de sobrepressão fica reduzida.")
            self._poll_status()

        self.run(apply, done)

    def _test_button_pressed(self) -> None:
        if self.status and self.status.state == p.ST_HOLD:
            if self.device:
                self.run(self.device.end_test, lambda _: self._poll_status())
        else:
            self._start_test()

    def _start_test(self) -> None:
        target = self.status.setpoint if self.status else 0.0
        answer = QMessageBox.question(
            self, "Iniciar ensaio",
            f"O manguito vai inflar até {target:.0f} mmHg.\n\nConfirme que o manguito está posicionado e que a pessoa foi avisada.",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.Cancel, QMessageBox.StandardButton.Cancel)
        if answer == QMessageBox.StandardButton.Yes and self.device:
            self.run(self.device.start_test, lambda _: self._poll_status())
            self.statusBar().clearMessage()

    def _abort_test(self) -> None:
        if self.device:
            self.run(self.device.abort_test, lambda _: self._poll_status())

    def _set_clock(self) -> None:
        if self.device:
            device = self.device
            self.run(lambda: device.set_datetime(datetime.now()),
                     lambda _: self.statusBar().showMessage("Relógio acertado.", 5000))

    # --- Ensaios -------------------------------------------------------

    def _tests_tab(self) -> QWidget:
        tab = QWidget()
        layout = QVBoxLayout(tab)
        bar = QHBoxLayout()
        self.sync_button = QPushButton("Atualizar do equipamento")
        self.sync_button.clicked.connect(self._sync)
        self.start_date = QDateEdit(calendarPopup=True)
        self.end_date = QDateEdit(calendarPopup=True)
        today = QDate.currentDate()
        self.start_date.setDate(today.addYears(-1))
        self.end_date.setDate(today)
        self.start_date.dateChanged.connect(self._fill_table)
        self.end_date.dateChanged.connect(self._fill_table)
        self.export_box = QComboBox()
        self.export_box.addItems(["CSV", "XLSX", "PDF"])
        export_selected = QPushButton("Exportar selecionados")
        export_selected.clicked.connect(lambda: self._export(True))
        export_all = QPushButton("Exportar período")
        export_all.clicked.connect(lambda: self._export(False))
        for widget in (self.sync_button, QLabel("De"), self.start_date, QLabel("até"), self.end_date):
            bar.addWidget(widget)
        bar.addStretch(1)
        for widget in (QLabel("Formato"), self.export_box, export_selected, export_all):
            bar.addWidget(widget)
        layout.addLayout(bar)

        split = QSplitter(Qt.Orientation.Vertical)
        self.table = QTableWidget(0, 8)
        self.table.setHorizontalHeaderLabels(["Ensaio", "Data e hora", "Operador", "Alvo", "Pico", "Resultado", "Descrição", "Integridade"])
        self.table.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
        self.table.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
        self.table.verticalHeader().hide()
        self.table.horizontalHeader().setSectionResizeMode(6, QHeaderView.ResizeMode.Stretch)
        self.table.horizontalHeader().setResizeContentsPrecision(50)
        self.table.itemSelectionChanged.connect(self._show_selected)
        detail = QWidget()
        row = QHBoxLayout(detail)
        self.detail_label = QLabel("Selecione um ensaio.")
        self.detail_label.setAlignment(Qt.AlignmentFlag.AlignTop)
        self.detail_label.setMinimumWidth(300)
        self.detail_label.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
        self.test_plot = pg.PlotWidget()
        self.test_plot.setLabel("left", "Pressão (mmHg)")
        self.test_plot.setLabel("bottom", "Tempo (s)")
        self.test_plot.showGrid(x=True, y=True, alpha=0.15)
        self.test_curve = self.test_plot.plot(pen=pg.mkPen("#1F6FB2", width=2))
        row.addWidget(self.detail_label, 0)
        row.addWidget(self.test_plot, 1)
        split.addWidget(self.table)
        split.addWidget(detail)
        split.setSizes([360, 280])
        layout.addWidget(split)
        return tab

    def _sync(self) -> None:
        device, archive = self.device, self.archive
        if not device or not archive:
            return
        self.sync_button.setEnabled(False)
        self._begin_busy("Lendo registros do equipamento")

        def done(_) -> None:
            self._end_busy()
            self.statusBar().showMessage("Registros atualizados.", 5000)
            self._apply_state()
            self._reload_tests()

        def failed(exc: Exception) -> None:
            self._end_busy()
            self._apply_state()
            self._show_error(exc)

        self.run(lambda: archive.sync(device, self.bridge.progress.emit), done, failed)

    def _reload_tests(self) -> None:
        archive = self.archive
        if not archive:
            return

        def load() -> tuple[IntegrityResult, list[reports.TestRow]]:
            result = archive.verify()
            return result, [reports.TestRow(e, archive.record(e), v) for e, v in reversed(list(result.tests.items()))]

        def done(loaded: tuple[IntegrityResult, list[reports.TestRow]]) -> None:
            self.integrity, self._rows = loaded
            self._fill_table()
            self._show_integrity(self.integrity)
            self._end_busy()

        def failed(exc: Exception) -> None:
            self._end_busy()
            self._show_error(exc)

        self._begin_busy("Carregando ensaios e verificando integridade")
        self.run(load, done, failed)

    def _period(self) -> tuple[date, date]:
        return self.start_date.date().toPython(), self.end_date.date().toPython()

    def _visible_rows(self) -> list[reports.TestRow]:
        start, end = self._period()
        return [r for r in self._rows if start <= r.entry.when.date() <= end]

    def _fill_table(self) -> None:
        rows = self._visible_rows()
        self.table.setUpdatesEnabled(False)
        self.table.setRowCount(len(rows))
        for i, row in enumerate(rows):
            e = row.entry
            values = [e.test_id, f"{e.when:%d/%m/%Y %H:%M:%S}", e.operator, f"{e.setpoint:.0f}", f"{e.peak:.1f}", e.nc,
                      reports.NC_NAMES.get(e.nc, ""), row.integrity]
            for col, value in enumerate(values):
                item = QTableWidgetItem(value)
                if col == 5:
                    item.setForeground(QColor(OK if e.conforming else BAD))
                if col == 7:
                    item.setForeground(QColor(VERDICT_COLORS[row.integrity]))
                self.table.setItem(i, col, item)
        self.table.resizeColumnsToContents()
        self.table.setUpdatesEnabled(True)

    def _selected_rows(self) -> list[reports.TestRow]:
        rows = self._visible_rows()
        return [rows[i.row()] for i in self.table.selectionModel().selectedRows() if i.row() < len(rows)]

    def _show_selected(self) -> None:
        selected = self._selected_rows()
        if not selected:
            return
        row = selected[0]
        e, r = row.entry, row.record
        lines = [f"{e.test_id}  ·  {e.when:%d/%m/%Y %H:%M:%S}", f"{e.nc}: {reports.NC_NAMES.get(e.nc, '')}"]
        if r:
            lines += [f"Participante {r.participant}, ciclo C{r.cycle}", f"Alvo {r.setpoint:.1f}, pico {r.peak:.1f} mmHg",
                      f"Platô médio {r.plateau_mean:.1f} ({r.plateau_min:.1f} a {r.plateau_max:.1f}) mmHg",
                      f"Insuflação {r.inflate_s:.1f} s, platô {r.plateau_s:.1f} s", f"Descompressão {r.deflate_s:.1f} s",
                      f"Pressão residual {r.residual:.1f} mmHg"]
        lines.append(f"Integridade: {row.integrity}")
        points = self.archive.curve(e) if self.archive else []
        if not points:
            lines.append("Sem curva gravada para este ensaio.")
        self.detail_label.setText("\n".join(lines))
        self.test_curve.setData([t for t, _ in points], [v for _, v in points])

    def _export(self, selected_only: bool) -> None:
        rows = self._selected_rows() if selected_only else self._visible_rows()
        if not rows:
            QMessageBox.information(self, "Exportar", "Nenhum ensaio para exportar.")
            return
        kind = self.export_box.currentText()
        archive = self.archive
        if kind == "PDF":  # um arquivo por ensaio
            folder = QFileDialog.getExistingDirectory(self, "Pasta para os PDFs")
            if not folder:
                return

            def write() -> str:
                for row in rows:
                    reports.export_test_pdf(row, archive.curve(row.entry), Path(folder) / f"{row.entry.test_id}_{row.entry.when:%Y%m%d_%H%M%S}.pdf")
                return folder
        else:
            suffix = kind.lower()
            name, _ = QFileDialog.getSaveFileName(self, "Exportar", f"ensaios.{suffix}", f"{kind} (*.{suffix})")
            if not name:
                return

            def write() -> str:
                (reports.export_csv if kind == "CSV" else reports.export_xlsx)(rows, Path(name))
                return name

        self.run(write, lambda where: self.statusBar().showMessage(f"{len(rows)} ensaios exportados: {where}", 10000))

    # --- Relatorios ----------------------------------------------------

    def _reports_tab(self) -> QWidget:
        tab = QWidget()
        layout = QVBoxLayout(tab)
        box = QGroupBox("Período")
        form = QFormLayout(box)
        self.report_start = QDateEdit(calendarPopup=True)
        self.report_end = QDateEdit(calendarPopup=True)
        today = QDate.currentDate()
        self.report_start.setDate(today.addMonths(-1))
        self.report_end.setDate(today)
        form.addRow("De", self.report_start)
        form.addRow("Até", self.report_end)
        usage = QPushButton("Gerar relatório de uso (PDF)")
        usage.clicked.connect(self._usage_report)
        errors = QPushButton("Gerar relatório de erros (PDF)")
        errors.clicked.connect(self._errors_report)
        form.addRow(usage)
        form.addRow(errors)
        layout.addWidget(box)
        layout.addStretch(1)
        return tab

    def _report_period(self) -> tuple[date, date]:
        return self.report_start.date().toPython(), self.report_end.date().toPython()

    def _usage_report(self) -> None:
        if not self.integrity:
            QMessageBox.information(self, "Relatório", "Conecte e atualize os registros primeiro.")
            return
        name, _ = QFileDialog.getSaveFileName(self, "Relatório de uso", "relatorio_uso.pdf", "PDF (*.pdf)")
        if name:
            summary = reports.summarize(self.integrity.tests.keys(), *self._report_period())
            self.run(lambda: reports.export_usage_pdf(summary, Path(name)),
                     lambda _: self.statusBar().showMessage(f"Relatório gerado: {name}", 10000))

    def _errors_report(self) -> None:
        if not self.integrity:
            QMessageBox.information(self, "Relatório", "Conecte e atualize os registros primeiro.")
            return
        name, _ = QFileDialog.getSaveFileName(self, "Relatório de erros", "relatorio_erros.pdf", "PDF (*.pdf)")
        if name:
            start, end = self._report_period()
            picked = reports.errors_in_period(self.integrity.errors, start, end)
            self.run(lambda: reports.export_errors_pdf(picked, start, end, Path(name)),
                     lambda _: self.statusBar().showMessage(f"Relatório gerado: {name}", 10000))

    # --- Integridade ---------------------------------------------------

    def _integrity_tab(self) -> QWidget:
        tab = QWidget()
        layout = QVBoxLayout(tab)
        self.verdict_label = QLabel("Sem dados")
        self.verdict_label.setStyleSheet("font-size:26px;font-weight:600")
        self.verdict_detail = QLabel("Atualize os registros na aba Ensaios.")
        self.verdict_detail.setWordWrap(True)
        self.logs_table = QTableWidget(0, 5)
        self.logs_table.setHorizontalHeaderLabels(["Arquivo", "Linhas", "Verificadas", "Não verificáveis", "Resultado"])
        self.logs_table.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
        self.logs_table.verticalHeader().hide()
        self.logs_table.horizontalHeader().setSectionResizeMode(0, QHeaderView.ResizeMode.Stretch)
        self.problems_label = QLabel()
        self.problems_label.setWordWrap(True)
        self.problems_label.setStyleSheet(f"color:{BAD}")
        for widget in (self.verdict_label, self.verdict_detail, self.logs_table, self.problems_label):
            layout.addWidget(widget)
        return tab

    def _show_integrity(self, result: IntegrityResult) -> None:
        verdict = result.verdict
        self.verdict_label.setText(verdict)
        self.verdict_label.setStyleSheet(f"font-size:26px;font-weight:600;color:{VERDICT_COLORS[verdict]}")
        counts = {v: sum(1 for x in result.tests.values() if x == v) for v in VERDICT_COLORS}
        text = (f"{len(result.tests)} ensaios: {counts[sd.INTACT]} íntegros, {counts[sd.TAMPERED]} adulterados, "
                f"{counts[sd.UNVERIFIABLE]} não verificáveis.")
        if verdict == sd.UNVERIFIABLE:
            text += " Registros de firmware antigo não têm hash completo e não podem ser confirmados."
        self.verdict_detail.setText(text)
        self.logs_table.setRowCount(len(result.logs))
        for i, report in enumerate(result.logs):
            values = [report.name, str(report.lines), str(report.verified), str(report.unverifiable), report.verdict]
            for col, value in enumerate(values):
                item = QTableWidgetItem(value)
                if col == 4:
                    item.setForeground(QColor(VERDICT_COLORS[report.verdict]))
                self.logs_table.setItem(i, col, item)
        problems = [f"{r.name}, {x}" for r in result.logs for x in r.problems[:5]]
        self.problems_label.setText("\n".join(problems[:10]))
