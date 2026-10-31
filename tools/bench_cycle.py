#!/usr/bin/env python3
"""Roda ensaios automaticos pela USB e mede a qualidade do controle.

    python tools/bench_cycle.py --name grande --setpoints 60,100,150 --set veloc.tempo=1.5

Para cada setpoint: inicia, espera STABILIZED, mantem por --hold segundos e
desinfla. Grava CSV e imprime tempo ate' o alvo, pico, faixa no plato, pulsos
de valvula e partidas de bomba. --set muda parametros so' em RAM (somem no
reset; nao ha' PARAM_COMMIT aqui).

Em pessoa, so' com o consentimento explicito dela, presente e com o pedal ou o
botao ao alcance: o plato nao tem limite de tempo no firmware, entao se o PC ou
a USB cairem so' a parada local desinfla. Use --rest para dar descanso ao membro.
Aborta o ensaio acima de setpoint + --margin, em erro do equipamento ou se a
telemetria parar.
"""
import argparse
import csv
import struct
import sys
import time
from pathlib import Path

import serial

import bench_usb_hello as b
from bench_characterize import load_key

ST_IDLE, ST_INFL, ST_STAB, ST_HOLD, ST_DEFL, ST_FAULT = 1, 2, 3, 4, 5, 8


class Link:
    def __init__(self, ser):
        self.ser = ser
        self.seq = 200
        self.buf = bytearray()
        self.rows = []
        self.verdict = ""
        self.last_rx = time.perf_counter()

    def poll(self):
        n = self.ser.in_waiting
        if n:
            self.buf += self.ser.read(n)
        while True:
            i = self.buf.find(0)
            if i < 0:
                return
            raw = bytes(self.buf[:i])
            del self.buf[:i + 1]
            if not raw:
                continue
            try:
                frame = b.parse_response(b.cobs_decode(raw))
            except ValueError:
                continue
            if frame["type"] == b.TYPE_EVT and frame["cmd"] == 0x0302:
                self.verdict = b.fmt_event(frame)
            if frame["type"] == b.TYPE_STREAM:
                self.rows.extend(b.parse_stream(frame["payload"])[1])
                self.last_rx = time.perf_counter()

    def send(self, cmd, payload=b""):
        self.seq = (self.seq + 1) & 0xFFFF
        self.ser.write(b.cobs_encode(b.build_request(self.seq, cmd, payload)) + b"\x00")

    def wait(self, seconds, stop=None):
        end = time.perf_counter() + seconds
        while time.perf_counter() < end:
            self.poll()
            if time.perf_counter() - self.last_rx > 1.0:
                raise RuntimeError("telemetria parou")
            if self.rows and stop is not None and stop(self.rows[-1]):
                return True
            time.sleep(0.002)
        return False


def analyze(rows, sp, band):
    t0 = rows[0][0]
    reach = next((r[0] - t0 for r in rows if r[1] >= sp - band), None)
    hold = [r for r in rows if r[3] == ST_HOLD]
    t_hold = (hold[0][0] - t0) / 1000.0 if hold else None
    active = [r for r in rows if r[3] in (ST_INFL, ST_STAB, ST_HOLD)]
    pulses = sum(1 for p, q in zip(active, active[1:]) if not (p[5] & 3) and (q[5] & 3))
    v1 = sum(1 for p, q in zip(active, active[1:]) if not (p[5] & 1) and (q[5] & 1))
    starts = sum(1 for p, q in zip(active, active[1:]) if p[4] == 0 and q[4] > 0)
    peak = max(r[1] for r in active) if active else 0.0
    # ignora o primeiro 1 s do plato: leitura ainda assentando
    plate = [r[1] for r in hold if r[0] >= hold[0][0] + 1000] if hold else []
    p_min = min(plate) if plate else float("nan")
    p_max = max(plate) if plate else float("nan")
    out = sum(1 for p, q in zip(rows, rows[1:]) if p[3] == ST_HOLD and q[3] in (ST_INFL, ST_STAB))
    err = max((r[6] for r in rows if r[0] >= (active[0][0] if active else 0)), default=0)
    return (f"SP {sp:5.1f}: alvo em {reach / 1000.0 if reach is not None else -1:5.1f} s, "
            f"estavel em {t_hold if t_hold is not None else -1:5.1f} s, pico {peak:6.1f} "
            f"(+{peak - sp:4.1f}), plato {p_min:6.1f}..{p_max:6.1f}, reentradas {out}, "
            f"pulsos {pulses} (V1 {v1}), partidas {starts}, erro {err}")


def set_param(ser, seq, key, value):
    if not b.run_pset(ser, seq, key, value, 2.0):
        raise SystemExit(f"falha ao ajustar {key}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--name", required=True)
    ap.add_argument("--setpoints", default="100")
    ap.add_argument("--hold", type=float, default=15.0)
    ap.add_argument("--repeat", type=int, default=1)
    ap.add_argument("--timeout", type=float, default=60.0, help="tempo maximo ate' estabilizar (s)")
    ap.add_argument("--margin", type=float, default=20.0)
    ap.add_argument("--band", type=float, default=3.0)
    ap.add_argument("--rest", type=float, default=1.0, help="pausa entre ciclos, ja' desinflado (s)")
    ap.add_argument("--set", action="append", default=[], metavar="nome=valor")
    ap.add_argument("--port", default="COM3")
    args = ap.parse_args()

    ser = serial.Serial(args.port, 115200, timeout=0.05)
    link = Link(ser)
    out = Path("build/bench")
    out.mkdir(parents=True, exist_ok=True)
    stamp = time.strftime("%Y%m%d_%H%M%S")
    active = False
    try:
        if not b.run_auth_flow(ser, 1, load_key(), 2.0, logout=False):
            raise SystemExit("falha na autenticacao DEV")
        for i, item in enumerate(args.set):
            key, value = item.split("=")
            set_param(ser, 300 + 40 * i, key, float(value))
        ser.reset_input_buffer()
        link.send(b.CMD_STREAM_START, struct.pack("<H", 200))
        for rep in range(args.repeat):
            for sp in (float(x) for x in args.setpoints.split(",")):
                if sp > 170.0:
                    raise SystemExit("setpoint acima de 170 recusado neste script")
                link.wait(0.5)
                link.rows.clear()
                link.verdict = ""
                link.send(b.CMD_SET_SETPOINT, struct.pack("<f", sp))
                link.wait(0.2)
                link.send(b.CMD_START_TEST)
                active = True
                limit = sp + args.margin
                link.wait(0.3)  # o erro do ciclo anterior so' some depois do inicio
                bad = lambda r: r[1] > limit or r[3] in (ST_FAULT, ST_IDLE, ST_DEFL)
                t0 = time.perf_counter()
                got = None
                while got is None:
                    link.wait(0.05)
                    if not link.rows:
                        continue
                    r = link.rows[-1]
                    if r[3] == ST_HOLD:
                        got = True
                    elif bad(r) or time.perf_counter() - t0 > args.timeout:
                        got = False
                if got:
                    link.wait(args.hold, stop=bad)
                link.send(b.CMD_ABORT_TEST)
                link.wait(0.5)
                link.wait(40.0, stop=lambda r: r[3] == ST_IDLE)
                active = False
                link.wait(args.rest)
                rows = list(link.rows)
                print(analyze(rows, sp, args.band), flush=True)
                print("   equipamento: " + link.verdict.split("ENSAIO_CONCLUIDO:")[-1].strip(), flush=True)
                path = out / f"{args.name}_{stamp}_sp{sp:.0f}_{rep}.csv"
                with open(path, "w", newline="") as f:
                    w = csv.writer(f)
                    w.writerow(["t_ms", "p", "sp", "estado", "bomba", "valvulas", "erro"])
                    w.writerows(rows)
    finally:
        try:
            if active:
                link.send(b.CMD_ABORT_TEST)
                link.wait(3.0)
            link.send(b.CMD_STREAM_STOP)
            link.send(b.CMD_AUTH_LOGOUT)
            time.sleep(0.2)
        except Exception as e:
            print(f"aviso ao encerrar: {e}", file=sys.stderr)
        ser.close()


if __name__ == "__main__":
    main()
