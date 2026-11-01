"""Telas do nivel DEV. So' existem depois do login; somem no logout ou quando a sessao expira.

Tudo que aciona bomba ou valvulas pede a confirmacao de que o manguito esta' fora de
uma pessoa ou que o operador esta' presente, e tem um botao de parar sempre ativo.
"""
from __future__ import annotations

import csv
import threading
from collections import deque
from pathlib import Path

import pyqtgraph as pg
from PySide6.QtCore import Qt, Signal
from PySide6.QtWidgets import (QAbstractItemView, QCheckBox, QDoubleSpinBox, QFileDialog, QFormLayout, QGroupBox,
                               QHBoxLayout, QHeaderView, QLabel, QMessageBox, QPlainTextEdit, QPushButton, QSlider,
                               QSpinBox, QTableWidget, QTableWidgetItem, QTabWidget, QVBoxLayout, QWidget)

from core import dev as d
from core import procedures as proc
from core import protocol as p
from core.device import DeviceError

BAD, OK, WARN, MUTED = "#B3261E", "#1E7B4F", "#9A6700", "#5B6875"
SCOPE_RATES = (50, 100, 200, 500, 1000)
SCOPE_POINTS = 10000
CAL_ZERO_MAX_MMHG = 3.0


def stop_button(text: str = "Parar") -> QPushButton:
    button = QPushButton(text)
    button.setMinimumHeight(40)
    button.setStyleSheet(f"QPushButton{{background:{BAD};color:white;font-weight:600;border:0;border-radius:4px}}"
                         "QPushButton:disabled{background:#C9D1D9}")
    return button


def confirm_actuation(parent: QWidget, text: str) -> bool:
    answer = QMessageBox.question(parent, "Acionar bomba e válvulas",
                                  f"{text}\n\nConfirme que o manguito está em bancada, ou em uma pessoa ciente, com o operador presente.",
                                  QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.Cancel,
                                  QMessageBox.StandardButton.Cancel)
    return answer == QMessageBox.StandardButton.Yes


class DevPanel(QTabWidget):
    tell = Signal(str)
    procedure_done = Signal(object)

    def __init__(self, window, session: d.DevSession) -> None:
        super().__init__()
        self.window_ = window
        self.session = session
        self.device = session.device
        self._cancel = threading.Event()
        self._manual: d.ManualLink | None = None
        self._params: list[d.Param] = []
        self._running = False
        self._scope = deque(maxlen=SCOPE_POINTS)
        self._scope_dirty = False
        self.addTab(self._params_tab(), "Parâmetros")
        self.addTab(self._manual_tab(), "Manual")
        self.addTab(self._scope_tab(), "Osciloscópio")
        self.addTab(self._calibration_tab(), "Calibração")
        self.addTab(self._leak_tab(), "Vazamento")
        self.addTab(self._diag_tab(), "Diagnóstico")
        self.tell.connect(self._show_tell)
        self.procedure_done.connect(self._procedure_finished)
        self._load_params()
        window.bridge.samples.connect(self._on_samples)

    def shutdown(self) -> None:
        """Encerra o que estiver acionando antes de a porta fechar."""
        self._cancel.set()
        for name in ("_manual", "_cal_link"):
            link = getattr(self, name)
            setattr(self, name, None)
            if link:
                link.__exit__(None, None, None)

    def _run(self, fn, done=None) -> None:
        self.window_.run(fn, done, self._failed)

    def _failed(self, exc: Exception) -> None:
        self.window_._show_error(exc)

    def _show_tell(self, text: str) -> None:
        self.window_.statusBar().showMessage(text, 8000)

    def _idle(self) -> bool:
        status = self.window_.status
        if status and status.state == p.ST_IDLE and not self._running and not self._manual:
            return True
        QMessageBox.information(self, "Equipamento ocupado", "O equipamento precisa estar parado para isto.")
        return False

    # --- Parametros ----------------------------------------------------

    def _params_tab(self) -> QWidget:
        tab = QWidget()
        layout = QVBoxLayout(tab)
        bar = QHBoxLayout()
        self.dirty_label = QLabel()
        buttons = (("Recarregar", self._load_params), ("Salvar na EEPROM", self._commit_params),
                   ("Descartar não salvos", lambda: self._revert_params(False)),
                   ("Valores de fábrica", lambda: self._revert_params(True)),
                   ("Exportar JSON", self._export_params), ("Importar JSON", self._import_params))
        bar.addWidget(self.dirty_label)
        bar.addStretch(1)
        for text, handler in buttons:
            button = QPushButton(text)
            button.clicked.connect(handler)
            bar.addWidget(button)
        layout.addLayout(bar)
        self.param_table = QTableWidget(0, 7)
        self.param_table.setHorizontalHeaderLabels(["Grupo", "Nome", "Valor", "Unidade", "Mínimo", "Máximo", "Fábrica"])
        self.param_table.verticalHeader().hide()
        self.param_table.horizontalHeader().setSectionResizeMode(1, QHeaderView.ResizeMode.Stretch)
        self.param_table.itemChanged.connect(self._param_edited)
        layout.addWidget(self.param_table)
        return tab

    def _load_params(self) -> None:
        self._run(self.session.params, self._show_params)

    def _show_params(self, result: tuple[list[d.Param], bool]) -> None:
        self._params, dirty = result
        self.dirty_label.setText("Há alterações não salvas" if dirty else "Tudo salvo")
        self.dirty_label.setStyleSheet(f"color:{WARN if dirty else OK};font-weight:600")
        table = self.param_table
        table.blockSignals(True)
        table.setRowCount(len(self._params))
        for row, param in enumerate(self._params):
            values = [d.GROUP_NAMES.get(param.group, str(param.group)), param.name, param.text(), param.unit,
                      param.text(param.minimum), param.text(param.maximum), param.text(param.factory)]
            for col, value in enumerate(values):
                item = QTableWidgetItem(value)
                if col != 2:
                    item.setFlags(item.flags() & ~Qt.ItemFlag.ItemIsEditable)
                table.setItem(row, col, item)
        table.resizeColumnsToContents()
        table.blockSignals(False)

    def _param_edited(self, item: QTableWidgetItem) -> None:
        if item.column() != 2:
            return
        param = self._params[item.row()]
        try:
            value = float(item.text().replace(",", "."))
        except ValueError:
            self._load_params()
            return
        if not param.minimum <= value <= param.maximum:
            QMessageBox.warning(self, "Parâmetro", f"{param.name}: fora da faixa {param.text(param.minimum)} a {param.text(param.maximum)}.")
            self._load_params()
            return

        def failed(exc: Exception) -> None:
            self._load_params()
            self._failed(exc)

        self.window_.run(lambda: self.session.param_set(param.id, value), lambda _: self._load_params(), failed)

    def _commit_params(self) -> None:
        self._run(self.session.param_commit, lambda seq: (self.tell.emit(f"Gravado na EEPROM (seq {seq})."), self._load_params()))

    def _revert_params(self, factory: bool) -> None:
        if factory and QMessageBox.question(self, "Valores de fábrica", "Voltar todos os parâmetros aos valores de fábrica? Só grava ao salvar.") \
                != QMessageBox.StandardButton.Yes:
            return
        self._run(lambda: self.session.param_revert(factory), lambda _: self._load_params())

    def _export_params(self) -> None:
        name, _ = QFileDialog.getSaveFileName(self, "Exportar parâmetros", "parametros.json", "JSON (*.json)")
        if name:
            d.export_params(self._params, Path(name))

    def _import_params(self) -> None:
        name, _ = QFileDialog.getOpenFileName(self, "Importar parâmetros", "", "JSON (*.json)")
        if not name:
            return
        try:
            changes = d.diff_params(self._params, Path(name))
        except (ValueError, OSError) as exc:
            QMessageBox.warning(self, "Importar", str(exc))
            return
        if not changes:
            QMessageBox.information(self, "Importar", "O arquivo é igual ao equipamento.")
            return
        lines = "\n".join(f"{q.name}: {q.text()} -> {q.text(v)}" for q, v in changes)
        if QMessageBox.question(self, "Importar", f"Aplicar {len(changes)} alterações?\n\n{lines}\n\nFicam só na RAM até salvar.") \
                != QMessageBox.StandardButton.Yes:
            return

        def apply() -> None:
            for param, value in changes:
                self.session.param_set(param.id, value)

        self._run(apply, lambda _: self._load_params())

    # --- Manual --------------------------------------------------------

    def _manual_tab(self) -> QWidget:
        tab = QWidget()
        layout = QVBoxLayout(tab)
        self.manual_button = QPushButton("Entrar no modo manual")
        self.manual_button.setMinimumHeight(40)
        self.manual_button.clicked.connect(self._toggle_manual)
        self.manual_state = QLabel("Fora do modo manual")
        layout.addWidget(self.manual_button)
        layout.addWidget(self.manual_state)
        box = QGroupBox("Comandos")
        form = QFormLayout(box)
        self.pump_slider = QSlider(Qt.Orientation.Horizontal)
        self.pump_slider.setRange(0, 100)
        self.pump_label = QLabel("0 %")
        self.pump_slider.valueChanged.connect(self._manual_changed)
        row = QHBoxLayout()
        row.addWidget(self.pump_slider, 1)
        row.addWidget(self.pump_label)
        form.addRow("Bomba", row)
        self.valve_boxes = [QCheckBox(f"V{i + 1} aberta") for i in range(3)]
        for box_ in self.valve_boxes:
            box_.toggled.connect(self._manual_changed)
            form.addRow(box_)
        layout.addWidget(box)
        self.manual_stop = stop_button("Parar e abrir válvulas")
        self.manual_stop.clicked.connect(self._stop_manual)
        layout.addWidget(self.manual_stop)
        layout.addStretch(1)
        self._manual_controls(False)
        return tab

    def _manual_controls(self, on: bool) -> None:
        self.pump_slider.setEnabled(on)
        for box in self.valve_boxes:
            box.setEnabled(on)
        self.manual_stop.setEnabled(on)
        self.manual_button.setText("Sair do modo manual" if on else "Entrar no modo manual")
        self.manual_state.setText("Em modo manual" if on else "Fora do modo manual")

    def _toggle_manual(self) -> None:
        if self._manual:
            self._stop_manual()
            return
        if not self._idle() or not confirm_actuation(self, "A bomba e as válvulas passam a obedecer esta tela."):
            return
        link = d.ManualLink(self.session)

        def entered(_) -> None:
            self._manual = link
            self._reset_manual_controls()
            self._manual_controls(True)

        self._run(link.__enter__, entered)

    def _reset_manual_controls(self) -> None:
        for widget in [self.pump_slider] + self.valve_boxes:
            widget.blockSignals(True)
        self.pump_slider.setValue(0)
        self.pump_label.setText("0 %")
        for box in self.valve_boxes:
            box.setChecked(False)
        for widget in [self.pump_slider] + self.valve_boxes:
            widget.blockSignals(False)

    def _manual_changed(self) -> None:
        pump = self.pump_slider.value()
        self.pump_label.setText(f"{pump} %")
        link = self._manual
        if not link:
            return
        mask = sum(1 << i for i, box in enumerate(self.valve_boxes) if box.isChecked())

        def failed(exc: Exception) -> None:
            self._stop_manual()
            self._failed(exc)

        self.window_.run(lambda: link.set(pump, mask), failed=failed)

    def _stop_manual(self) -> None:
        link, self._manual = self._manual, None
        self._reset_manual_controls()
        self._manual_controls(False)
        if link:
            self._run(lambda: link.__exit__(None, None, None))

    # --- Osciloscopio --------------------------------------------------

    def _scope_tab(self) -> QWidget:
        tab = QWidget()
        layout = QVBoxLayout(tab)
        bar = QHBoxLayout()
        self.rate_box = QSpinBox()
        self.rate_box.setRange(1, 1000)
        self.rate_box.setValue(200)
        self.rate_box.setSuffix(" Hz")
        apply_rate = QPushButton("Aplicar taxa")
        apply_rate.clicked.connect(self._apply_rate)
        save = QPushButton("Salvar CSV")
        save.clicked.connect(self._save_scope)
        clear = QPushButton("Limpar")
        clear.clicked.connect(self._scope.clear)
        self.drop_label = QLabel()
        for widget in (QLabel("Taxa"), self.rate_box, apply_rate, save, clear, self.drop_label):
            bar.addWidget(widget)
        bar.addStretch(1)
        layout.addLayout(bar)
        self.scope_p = pg.PlotWidget()
        self.scope_p.setLabel("left", "Pressão (mmHg)")
        self.scope_a = pg.PlotWidget()
        self.scope_a.setLabel("left", "Bomba (%)")
        self.scope_a.setLabel("bottom", "Tempo (s)")
        self.scope_a.setXLink(self.scope_p)
        self.scope_a.setMaximumHeight(200)
        self.curve_p = self.scope_p.plot(pen=pg.mkPen("#1F6FB2", width=1.5))
        self.curve_sp = self.scope_p.plot(pen=pg.mkPen("#8A94A0", style=Qt.PenStyle.DashLine))
        self.curve_pump = self.scope_a.plot(pen=pg.mkPen("#1E7B4F", width=1.5))
        self.curve_v = [self.scope_a.plot(pen=pg.mkPen(c)) for c in ("#B3261E", "#9A6700", "#6B4FBB")]
        layout.addWidget(self.scope_p, 1)
        layout.addWidget(self.scope_a)
        self._scope_timer = self.startTimer(100)
        return tab

    def _apply_rate(self) -> None:
        rate = self.rate_box.value()
        self._run(lambda: self.device.stream_start(rate), lambda eff: self.tell.emit(f"Telemetria a {eff} Hz."))

    def _on_samples(self, samples: list[p.Sample]) -> None:
        self._scope.extend(samples)
        self._scope_dirty = True

    def timerEvent(self, _event) -> None:
        if not self._scope_dirty or self.currentIndex() != 2 or not self.isVisible():
            return
        self._scope_dirty = False
        rows = list(self._scope)
        if not rows:
            return
        t0 = rows[-1].t_ms / 1000.0
        step = max(1, len(rows) // 3000)  # o desenho nao precisa de todas as amostras
        rows = rows[::step]
        xs = [r.t_ms / 1000.0 - t0 for r in rows]
        self.curve_p.setData(xs, [r.pressure for r in rows])
        self.curve_sp.setData(xs, [r.setpoint for r in rows])
        self.curve_pump.setData(xs, [r.pump for r in rows])
        for i, curve in enumerate(self.curve_v):
            curve.setData(xs, [10.0 * (i + 1) if r.valves & (1 << i) else 0.0 for r in rows])

    def _save_scope(self) -> None:
        name, _ = QFileDialog.getSaveFileName(self, "Salvar", "osciloscopio.csv", "CSV (*.csv)")
        if name:
            with open(name, "w", newline="", encoding="ascii") as f:
                writer = csv.writer(f)
                writer.writerow(["t_ms", "pressao", "alvo", "estado", "bomba", "valvulas", "erro"])
                writer.writerows((s.t_ms, f"{s.pressure:.2f}", f"{s.setpoint:.1f}", s.state, s.pump, s.valves, s.error)
                                 for s in self._scope)

    # --- Rotinas automaticas -------------------------------------------

    def _start_procedure(self, body, finished) -> None:
        """Roda `body(ctx)` em modo manual. O manual sempre termina, com erro ou nao."""
        self._running = True
        self._cancel.clear()
        self._finished = finished
        session, device, cancel = self.session, self.device, self._cancel

        def work():
            with d.ManualLink(session) as link:
                return body(proc.Context(device, link, cancel, self.tell.emit))

        self.window_.run(work, lambda r: self.procedure_done.emit((True, r)), lambda e: self.procedure_done.emit((False, e)))

    def _procedure_finished(self, outcome: tuple[bool, object]) -> None:
        self._running = False
        self._finished(*outcome)

    # --- Calibracao ----------------------------------------------------

    def _calibration_tab(self) -> QWidget:
        tab = QWidget()
        layout = QVBoxLayout(tab)
        intro = QLabel("Ligue o manômetro de referência na V3. O sistema vai a cada pressão e estabiliza: "
                       "leia o manômetro e informe o valor. São necessários ao menos 3 pontos, cobrindo 50 mmHg.")
        intro.setWordWrap(True)
        layout.addWidget(intro)
        form = QFormLayout()
        self.cal_target = QDoubleSpinBox()
        self.cal_target.setRange(0.0, proc.MAX_TARGET_MMHG)
        self.cal_target.setDecimals(0)
        self.cal_target.setValue(50.0)
        self.cal_target.setSuffix(" mmHg")
        self.cal_ref = QDoubleSpinBox()
        self.cal_ref.setRange(0.0, 300.0)
        self.cal_ref.setDecimals(1)
        self.cal_ref.setSuffix(" mmHg")
        form.addRow("Pressão do ponto", self.cal_target)
        form.addRow("Leitura do manômetro", self.cal_ref)
        layout.addLayout(form)
        row = QHBoxLayout()
        self.cal_buttons = {}
        for key, text, handler in (("start", "Iniciar", self._cal_start), ("go", "Ir para a pressão", self._cal_go),
                                   ("capture", "Capturar ponto", self._cal_capture), ("apply", "Calcular e gravar", self._cal_apply)):
            button = QPushButton(text)
            button.clicked.connect(handler)
            self.cal_buttons[key] = button
            row.addWidget(button)
        self.cal_stop = stop_button("Cancelar calibração")
        self.cal_stop.clicked.connect(self._cal_cancel)
        row.addWidget(self.cal_stop)
        layout.addLayout(row)
        self.cal_log = QPlainTextEdit()
        self.cal_log.setReadOnly(True)
        layout.addWidget(self.cal_log, 1)
        self._cal_link: d.ManualLink | None = None
        self._cal_refs: list[float] = []
        self._cal_update(False)
        return tab

    def _cal_update(self, active: bool, busy: bool = False) -> None:
        self.cal_buttons["start"].setEnabled(not active)
        for key in ("go", "capture", "apply"):
            self.cal_buttons[key].setEnabled(active and not busy)
        self.cal_stop.setEnabled(active)

    def _cal_start(self) -> None:
        if not self._idle() or not confirm_actuation(self, "A calibração aciona bomba e válvulas e substitui os coeficientes do sensor."):
            return
        link = d.ManualLink(self.session)

        def start() -> None:
            link.__enter__()
            try:
                self.session.cal_start()
                link.set(0, d.ALL_VALVES)  # V3 aberta para o manometro, comeca do zero
            except Exception:
                link.__exit__(None, None, None)
                raise

        def started(_) -> None:
            self._cal_link = link
            self._cal_refs = []
            self._cancel.clear()
            self.cal_log.setPlainText("Calibração iniciada. Válvulas abertas: o primeiro ponto pode ser o zero.")
            self._cal_update(True)

        self._run(start, started)

    def _cal_go(self) -> None:
        link, target = self._cal_link, self.cal_target.value()
        if not link:
            return
        self._cal_update(True, busy=True)
        ctx = proc.Context(self.device, link, self._cancel, self.tell.emit)

        def go() -> float:
            if target < 10.0:
                link.set(0, d.ALL_VALVES)
                ctx.wait(5.0)
                return ctx.check()
            link.set(0, d.V3)
            return proc.go_to_pressure(ctx, target, keep_valves=d.V3)

        def done(pressure: float) -> None:
            self._cal_update(True)
            self.cal_log.appendPlainText(f"Estabilizado em {pressure:.1f} mmHg. Leia o manômetro e capture.")

        def failed(exc: Exception) -> None:
            self._cal_update(self._cal_link is not None)
            if not isinstance(exc, proc.Cancelled):
                self.cal_log.appendPlainText(f"Falhou: {exc}")

        self.window_.run(go, done, failed)

    def _cal_capture(self) -> None:
        reference = self.cal_ref.value()

        def done(point: d.CalPoint) -> None:
            self._cal_refs.append(reference)
            self.cal_log.appendPlainText(f"Ponto {point.count}: referência {reference:.1f}, sensor {point.reading:.1f} mmHg, "
                                         f"erro atual {point.error:+.2f} mmHg")

        def failed(exc: Exception) -> None:
            self.cal_log.appendPlainText(f"Não capturado: {exc}")

        self.window_.run(lambda: self.session.cal_point(reference), done, failed)

    def _cal_apply(self) -> None:
        # Sem ponto em zero a reta so' vale na faixa medida: a primeira calibracao real
        # ficou boa de 50 a 150 mmHg e gravou +5,2 mmHg com o circuito vazio.
        if not any(reference <= CAL_ZERO_MAX_MMHG for reference in self._cal_refs):
            self.cal_log.appendPlainText("Falta o ponto em zero: com as válvulas abertas, capture a leitura 0. Nada foi gravado.")
            return

        def done(result: d.CalResult) -> None:
            text = (f"{result.count} pontos, faixa {result.span:.0f} mmHg: escala {result.scale:.5f}, "
                    f"offset {result.offset:+.2f} mmHg, resíduo máximo {result.residual:.2f} mmHg.")
            if result.applied:
                self.cal_log.appendPlainText(f"{text}\nAPLICADA E GRAVADA.")
                self._cal_finish(False)
            else:
                self.cal_log.appendPlainText(f"{text}\nREPROVADA: {result.reason}. Nada foi alterado.")

        self._run(self.session.cal_apply, done)

    def _cal_cancel(self) -> None:
        self._cancel.set()
        self.cal_log.appendPlainText("Calibração cancelada. Nada foi alterado.")
        self._cal_finish(True)

    def _cal_finish(self, abort: bool) -> None:
        link, self._cal_link = self._cal_link, None
        self._cal_update(False)

        def finish() -> None:
            if abort:
                self.session.cal_abort()
            if link:
                link.__exit__(None, None, None)

        self._run(finish)

    # --- Vazamento -----------------------------------------------------

    def _leak_tab(self) -> QWidget:
        tab = QWidget()
        layout = QVBoxLayout(tab)
        intro = QLabel("Sobe até a pressão, estabiliza, desliga a bomba e observa. Aprova só se a queda não passar do limite.")
        intro.setWordWrap(True)
        layout.addWidget(intro)
        form = QFormLayout()
        self.leak_target = QDoubleSpinBox()
        self.leak_target.setRange(20.0, proc.MAX_TARGET_MMHG)
        self.leak_target.setDecimals(0)
        self.leak_target.setValue(100.0)
        self.leak_target.setSuffix(" mmHg")
        self.leak_hold = QSpinBox()
        self.leak_hold.setRange(5, 300)
        self.leak_hold.setValue(10)
        self.leak_hold.setSuffix(" s")
        self.leak_limit = QDoubleSpinBox()
        self.leak_limit.setRange(0.5, 50.0)
        self.leak_limit.setValue(8.0)
        self.leak_limit.setSuffix(" mmHg")
        form.addRow("Pressão", self.leak_target)
        form.addRow("Tempo de observação", self.leak_hold)
        form.addRow("Queda máxima", self.leak_limit)
        layout.addLayout(form)
        self.leak_start = QPushButton("Iniciar teste")
        self.leak_start.setMinimumHeight(40)
        self.leak_start.clicked.connect(self._leak_begin)
        self.leak_stop = stop_button("Parar")
        self.leak_stop.setEnabled(False)
        self.leak_stop.clicked.connect(self._cancel.set)
        layout.addWidget(self.leak_start)
        layout.addWidget(self.leak_stop)
        self.leak_result = QLabel("Sem resultado")
        self.leak_result.setStyleSheet("font-size:22px;font-weight:600")
        self.leak_detail = QLabel()
        layout.addWidget(self.leak_result)
        layout.addWidget(self.leak_detail)
        layout.addStretch(1)
        return tab

    def _leak_begin(self) -> None:
        if not self._idle() or not confirm_actuation(self, "O teste infla o manguito e mantém a pressão com a bomba desligada."):
            return
        target, hold, limit = self.leak_target.value(), self.leak_hold.value(), self.leak_limit.value()
        self.leak_start.setEnabled(False)
        self.leak_stop.setEnabled(True)
        self.leak_result.setText("Em andamento")
        self.leak_result.setStyleSheet(f"font-size:22px;font-weight:600;color:{MUTED}")
        self.leak_detail.clear()
        self._start_procedure(lambda ctx: proc.leak_test(ctx, target, hold, limit), self._leak_finished)

    def _leak_finished(self, ok: bool, value: object) -> None:
        self.leak_start.setEnabled(True)
        self.leak_stop.setEnabled(False)
        if ok:
            result: proc.LeakResult = value
            self.leak_result.setText("APROVADO" if result.passed else "REPROVADO")
            self.leak_result.setStyleSheet(f"font-size:22px;font-weight:600;color:{OK if result.passed else BAD}")
            self.leak_detail.setText(
                f"{result.start:.1f} para {result.end:.1f} mmHg em {result.hold_s:.0f} s: queda {result.drop:.1f} "
                f"(limite {result.limit:.1f})." if result.held else
                f"A pressão não se manteve em {result.target:.0f} mmHg (ficou em {result.end:.1f}).")
            return
        cancelled = isinstance(value, proc.Cancelled)
        self.leak_result.setText("CANCELADO" if cancelled else "SEM RESULTADO")
        self.leak_result.setStyleSheet(f"font-size:22px;font-weight:600;color:{WARN}")
        self.leak_detail.setText("" if cancelled else f"O teste não terminou: {value}")

    # --- Diagnostico ---------------------------------------------------

    def _diag_tab(self) -> QWidget:
        tab = QWidget()
        layout = QVBoxLayout(tab)
        row = QHBoxLayout()
        for text, handler in (("Atualizar", self._load_diag), ("Autoteste", self._selftest), ("Log de auditoria", self._audit_log),
                              ("Medir atraso da bomba", self._tlag), ("Reiniciar equipamento", self._reboot)):
            button = QPushButton(text)
            button.clicked.connect(handler)
            row.addWidget(button)
        row.addStretch(1)
        layout.addLayout(row)
        self.diag_text = QPlainTextEdit()
        self.diag_text.setReadOnly(True)
        layout.addWidget(self.diag_text, 1)
        return tab

    def _load_diag(self) -> None:
        def show(diag: d.Diagnostics) -> None:
            stacks = ", ".join(f"{k} {v}" for k, v in diag.stacks.items())
            state = p.STATE_NAMES.get(diag.jitter_state, str(diag.jitter_state))
            self.diag_text.setPlainText(
                f"Em funcionamento há {diag.uptime_s:.0f} s\n"
                f"Jitter máximo {diag.jitter_us} us, aos {diag.jitter_at_s:.1f} s, estado {state}\n"
                f"Execução máxima do controle {diag.exec_us} us\n"
                f"Reset RCC_RSR {diag.reset_flags:#010x}, IWDG {'sim' if diag.iwdg_reset else 'não'}\n"
                f"Hard fault {'sim' if diag.hard_fault else 'não'}\n"
                f"Telemetria descartada {diag.dropped}\n"
                f"Heap livre {diag.heap_free} B, mínimo {diag.heap_min} B\n"
                f"Pilha livre (palavras): {stacks}")

        self._run(self.session.diagnostics, show)

    def _selftest(self) -> None:
        def show(result: tuple[list[str], float]) -> None:
            fails, pressure = result
            verdict = "APROVADO" if not fails else f"REPROVADO: {', '.join(fails)}"
            self.diag_text.setPlainText(f"Autoteste {verdict}\nPressão {pressure:.1f} mmHg")

        self._run(self.session.selftest, show)

    def _audit_log(self) -> None:
        def load() -> str:
            names = [e.name for e in self.device.fs_list("/logs/audit") if e.name.endswith(".log")]
            return self.device.fs_read(f"/logs/audit/{sorted(names)[-1]}").decode("ascii", "replace") if names else ""

        def show(text: str) -> None:
            lines = [ln.split(";prev=")[0] for ln in text.splitlines()]
            self.diag_text.setPlainText("\n".join(lines[-200:]) or "Log de auditoria vazio.")

        self._run(load, show)

    def _tlag(self) -> None:
        if not self._idle() or not confirm_actuation(self, "A bomba sobe a 100% até 60 mmHg e para."):
            return

        def finished(ok: bool, value: object) -> None:
            self.diag_text.setPlainText(f"Atraso da bomba: {value:.2f} s" if ok else f"Não medido: {value}")

        self._start_procedure(lambda ctx: proc.identify_tlag(ctx, 60.0), finished)

    def _reboot(self) -> None:
        if QMessageBox.question(self, "Reiniciar", "Reiniciar o equipamento agora?") == QMessageBox.StandardButton.Yes:
            self._run(self.session.reboot, lambda _: self.tell.emit("Equipamento reiniciando."))
