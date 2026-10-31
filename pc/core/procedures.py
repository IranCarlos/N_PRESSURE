"""Rotinas automaticas do nivel DEV sobre o modo manual.

O sistema leva o manguito a uma pressao conhecida e estabiliza sozinho: e' o que
a calibracao e o teste de vazamento usam. O firmware continua supervisionando
sobrepressao e sinal de vida; os limites daqui sao uma camada a mais.
"""
from __future__ import annotations

import threading
import time
from collections.abc import Callable
from dataclasses import dataclass

from .dev import ManualLink, V2
from .device import Device

MAX_TARGET_MMHG = 170.0
OVER_MARGIN_MMHG = 20.0
BAND_MMHG = 2.0
SETTLE_S = 1.5
SLOPE_WINDOW_S = 2.0
STABLE_SLOPE_MMHG_S = 0.5  # o ar passa do tubo para o manguito por varios segundos
STEADY_SLOPE_RATIO = 0.8  # assentar desacelera; vazamento segue reto e nao e' assentamento
BURST_MAX_S = 1.0
BURST_GAIN_START = 33.0  # mmHg/s
BURST_GAIN_MIN = 5.0
BURST_MARGIN_MMHG = 8.0
GO_TIMEOUT_S = 90.0
POLL_S = 0.02
PULSE_S = 0.06


class ProcedureError(Exception):
    pass


class Unstable(ProcedureError):
    """A pressao chegou perto do alvo mas nao se manteve."""

    def __init__(self, target: float, pressure: float) -> None:
        super().__init__(f"não estabilizou em {target:.0f} mmHg (ficou em {pressure:.1f})")
        self.pressure = pressure


class Cancelled(Exception):
    pass


Tell = Callable[[str], None]


def _nothing(_text: str) -> None:
    pass


@dataclass
class Context:
    device: Device
    link: ManualLink
    cancel: threading.Event
    tell: Tell = _nothing
    limit: float = MAX_TARGET_MMHG + OVER_MARGIN_MMHG
    go_timeout_s: float = GO_TIMEOUT_S

    @property
    def pressure(self) -> float:
        sample = self.device.last_sample
        if sample is None or time.monotonic() - self.device.last_sample_at > 1.0:
            raise ProcedureError("telemetria parada")
        return sample.pressure

    def check(self) -> float:
        if self.cancel.is_set():
            raise Cancelled()
        if self.link.ended.is_set():
            raise ProcedureError("o equipamento encerrou o modo manual (pedal, sobrepressão ou sinal de vida)")
        pressure = self.pressure
        if pressure > self.limit:
            raise ProcedureError(f"pressão {pressure:.1f} mmHg acima do limite {self.limit:.0f}")
        return pressure

    def wait(self, seconds: float) -> None:
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            self.check()
            time.sleep(POLL_S)


def go_to_pressure(ctx: Context, target: float, keep_valves: int = 0) -> float:
    """Sobe ate' `target`, acomoda e corrige. Devolve a pressao estabilizada."""
    if not 10.0 <= target <= MAX_TARGET_MMHG:
        raise ProcedureError(f"alvo {target:.0f} mmHg fora de 10 a {MAX_TARGET_MMHG:.0f}")
    ctx.limit = target + OVER_MARGIN_MMHG
    deadline = time.monotonic() + ctx.go_timeout_s
    burst_gain = BURST_GAIN_START
    ctx.tell(f"Subindo até {target:.0f} mmHg")
    if ctx.check() < target - BAND_MMHG:
        ctx.link.set(100, keep_valves)
        last_p, last_t = ctx.check(), time.monotonic()
        rate = 0.0
        while True:
            pressure = ctx.check()
            now = time.monotonic()
            if now - last_t >= 0.1:
                rate += ((pressure - last_p) / (now - last_t) - rate) * 0.5
                last_p, last_t = pressure, now
            if pressure + max(0.0, rate) * 0.3 >= target - BAND_MMHG:  # inercia da bomba e redistribuicao
                break
            if now > deadline:
                ctx.link.set(0, keep_valves)
                raise ProcedureError(f"não atingiu {target:.0f} mmHg a tempo")
            time.sleep(POLL_S)
        ctx.link.set(0, keep_valves)

    while True:
        ctx.tell("Estabilizando")
        ctx.wait(SETTLE_S)
        first = ctx.check()
        ctx.wait(SLOPE_WINDOW_S)
        middle = ctx.check()
        ctx.wait(SLOPE_WINDOW_S)
        pressure = ctx.check()
        early = (first - middle) / SLOPE_WINDOW_S
        slope = (middle - pressure) / SLOPE_WINDOW_S
        error = target - pressure
        # estar na faixa nao basta: caindo cada vez mais devagar, ainda esta' assentando.
        # A faixa vale no inicio da janela: um vazamento constante sai dela durante a
        # propria medida, e reabastecer impediria de medi-lo.
        settled = abs(slope) <= STABLE_SLOPE_MMHG_S or slope >= early * STEADY_SLOPE_RATIO
        if abs(target - first) <= BAND_MMHG and settled:
            return pressure
        if time.monotonic() > deadline:
            raise Unstable(target, pressure)
        if error > BAND_MMHG:  # na faixa e assentando: rajada so' reiniciaria o assentamento
            # A duracao vem do ganho medido na rajada anterior: com ganho fixo a
            # reposicao empatava com a perda e a pressao nao chegava a' faixa.
            ctx.link.set(60, keep_valves)  # a bomba so' parte a partir de 45%
            started = time.monotonic()
            end = started + max(0.1, min(BURST_MAX_S, (target + BAND_MMHG - pressure) / burst_gain))
            while time.monotonic() < end and ctx.check() < target + BURST_MARGIN_MMHG:
                time.sleep(POLL_S)
            ctx.link.set(0, keep_valves)
            risen = ctx.check() - pressure
            if risen > 0.5:
                burst_gain = max(BURST_GAIN_MIN, risen / max(0.1, time.monotonic() - started))
        elif error < -BAND_MMHG:
            ctx.link.set(0, keep_valves | V2)
            ctx.wait(PULSE_S)
            ctx.link.set(0, keep_valves)


@dataclass(frozen=True)
class LeakResult:
    target: float
    start: float
    end: float
    hold_s: float
    limit: float
    held: bool = True  # False: nao conseguiu nem manter a pressao

    @property
    def drop(self) -> float:
        return self.start - self.end

    @property
    def passed(self) -> bool:
        return self.held and self.drop <= self.limit


def leak_test(ctx: Context, target: float, hold_s: float, max_drop: float, keep_valves: int = 0) -> LeakResult:
    """Aprova so' se (pressao estabilizada - pressao apos o tempo) nao passar do limite.

    Falha ou cancelamento vira excecao: nunca existe aprovacao sem medida. Nao manter
    a pressao e' reprovacao, nao falta de resultado.
    """
    try:
        start = go_to_pressure(ctx, target, keep_valves)
    except Unstable as exc:
        return LeakResult(target, target, exc.pressure, 0.0, max_drop, held=False)
    ctx.link.set(0, keep_valves)
    end_time = time.monotonic() + hold_s
    while time.monotonic() < end_time:
        ctx.check()
        ctx.tell(f"Observando: faltam {end_time - time.monotonic():.0f} s")
        time.sleep(0.2)
    return LeakResult(target, start, ctx.check(), hold_s, max_drop)


def estimate_tlag(times: list[float], pressures: list[float], stop_at: float) -> float:
    """Tempo entre cortar a bomba e a leitura parar de subir."""
    after = [(t, v) for t, v in zip(times, pressures) if t >= stop_at]
    if not after:
        raise ProcedureError("sem amostras depois do corte da bomba")
    peak_t = max(after, key=lambda item: item[1])[0]
    return max(0.0, peak_t - stop_at)


def identify_tlag(ctx: Context, target: float) -> float:
    """Pulso de bomba ate' `target` com medida do atraso apos cortar."""
    if not 10.0 <= target <= MAX_TARGET_MMHG:
        raise ProcedureError(f"alvo {target:.0f} mmHg fora de 10 a {MAX_TARGET_MMHG:.0f}")
    ctx.limit = target + OVER_MARGIN_MMHG
    ctx.tell(f"Subindo até {target:.0f} mmHg")
    ctx.link.set(100, 0)
    deadline = time.monotonic() + GO_TIMEOUT_S
    while ctx.check() < target:
        if time.monotonic() > deadline:
            raise ProcedureError(f"não atingiu {target:.0f} mmHg a tempo")
        time.sleep(POLL_S)
    ctx.link.set(0, 0)
    stop_at = time.monotonic()
    times, pressures = [], []
    ctx.tell("Medindo o atraso")
    while time.monotonic() < stop_at + 1.5:
        pressures.append(ctx.check())
        times.append(time.monotonic())
        time.sleep(POLL_S)
    return estimate_tlag(times, pressures, stop_at)
