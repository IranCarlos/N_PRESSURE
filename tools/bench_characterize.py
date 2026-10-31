#!/usr/bin/env python3
"""Mede a resposta pneumatica de um manguito pelo modo manual (USB, sessao DEV).

Grava um CSV com a telemetria e imprime um resumo: subida a 100%, inercia apos
desligar a bomba, vazamento, queda por pulso de V1/V2/V3, potencia minima da
bomba e ganho de pressao por rajada.

    python tools/bench_characterize.py --name manguito_grande

Usar SOMENTE com o manguito fora de uma pessoa. A chave DEV e' lida de
dev_key_secret.h (nao versionado) e fica so' em memoria.

Protecao: acima de --limit, se o equipamento sair do manual ou no fim (ate' com
erro ou Ctrl+C), bomba desligada e todas as valvulas abertas. Se o script parar
de responder, o sinal de vida do firmware (300 ms) leva ao mesmo estado.
"""
import argparse
import csv
import re
import struct
import sys
import time
from pathlib import Path

import serial

import bench_usb_hello as b

V1, V2, V3 = 1, 2, 4
ALL = V1 | V2 | V3
KEY_FILE = Path(__file__).parent.parent / "Core/Services/UsbProto/Inc/dev_key_secret.h"


class Abort(Exception):
    pass


def load_key() -> bytes:
    text = KEY_FILE.read_text(encoding="utf-8")
    body = text[text.index("DEV_KEY_PROVISION"):]
    key = bytes(int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]{2})", body))
    if len(key) != 32:
        raise SystemExit("chave DEV invalida em dev_key_secret.h")
    return key


class Bench:
    def __init__(self, ser, limit):
        self.ser = ser
        self.limit = limit
        self.seq = 100
        self.buf = bytearray()
        self.samples = []
        self.marks = []
        self.p = 0.0
        self.state = 0
        self.t_ms = 0
        self.guard = False
        self.dropped = 0

    def pump_frames(self):
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
            if frame["type"] != b.TYPE_STREAM:
                continue
            self.dropped, rows = b.parse_stream(frame["payload"])
            self.samples.extend(rows)
            if rows:
                self.t_ms, self.p, _, self.state, _, _, _ = rows[-1]

    def check(self):
        if not self.guard:
            return
        if self.p > self.limit:
            raise Abort(f"pressao {self.p:.1f} acima do limite {self.limit}")
        if self.state != b.STATE_MANUAL:
            raise Abort(f"equipamento saiu do manual (estado {self.state})")

    def set(self, pump, valves):
        self.seq = (self.seq + 1) & 0xFFFF
        req = b.build_request(self.seq, b.CMD_MANUAL_SET, bytes([pump, valves]))
        self.ser.write(b.cobs_encode(req) + b"\x00")

    def run(self, seconds, pump, valves, until=None):
        """Mantem o comando por `seconds`; para antes se until(p) for verdadeiro."""
        end = time.perf_counter() + seconds
        next_set = 0.0
        while True:
            now = time.perf_counter()
            if now >= end:
                return False
            if now >= next_set:
                self.set(pump, valves)
                next_set = now + 0.05
            self.pump_frames()
            self.check()
            if until is not None and until(self.p):
                return True
            time.sleep(0.001)

    def pulse(self, ms, pump, valves, rest_valves=0, rest_pump=0):
        self.run(ms / 1000.0, pump, valves)
        self.set(rest_pump, rest_valves)

    def mark(self, text):
        self.pump_frames()
        self.marks.append((self.t_ms, text))
        print(f"[{self.t_ms / 1000.0:8.2f} s] p={self.p:6.1f}  {text}", flush=True)

    def settled(self, seconds):
        self.run(seconds, 0, 0)
        tail = [s[1] for s in self.samples[-50:]]
        return sum(tail) / max(1, len(tail))


def characterize(x, target):
    results = []

    x.mark("subida a 100%, valvulas fechadas")
    t0 = time.perf_counter()
    reached = x.run(120.0, 100, 0, until=lambda p: p >= target)
    t_rise = time.perf_counter() - t0
    p_cut = x.p
    x.mark(f"bomba desligada ({'alvo' if reached else 'TIMEOUT'} em {t_rise:.1f} s)")
    x.set(0, 0)
    peak = p_cut
    end = time.perf_counter() + 1.5
    while time.perf_counter() < end:
        x.run(0.05, 0, 0)
        peak = max(peak, x.p)
    p_a = x.settled(1.0)
    x.run(5.0, 0, 0)
    p_b = x.settled(1.0)
    x.mark(f"inercia +{peak - p_cut:.1f} mmHg; vazamento {(p_a - p_b) / 6.0:.2f} mmHg/s")
    results.append(("subida", p_cut, t_rise, peak - p_cut, (p_a - p_b) / 6.0))

    for name, mask in (("V1", V1), ("V2", V2), ("V3", V3)):
        for ms in (30, 60, 120, 250):
            before = x.settled(1.2)
            x.pulse(ms, 0, mask)
            after = x.settled(1.5)
            x.mark(f"pulso {name} {ms} ms: {before:.1f} -> {after:.1f} ({before - after:.1f} mmHg)")
            results.append((f"pulso_{name}", ms, before, after, before - after))

    x.mark("esvazia ate' 40 mmHg")
    x.run(30.0, 0, V1 | V2, until=lambda p: p <= 40.0)
    for pct in (15, 20, 25, 30, 35, 40, 50, 70):
        start = x.settled(1.0)
        hit = x.run(4.0, pct, 0, until=lambda p: p >= target)
        end_p = x.settled(1.0)
        x.mark(f"bomba {pct}%: {start:.1f} -> {end_p:.1f} ({(end_p - start) / 4.0:.1f} mmHg/s)")
        results.append(("potencia", pct, start, end_p, (end_p - start) / 4.0))
        if hit:
            break

    x.mark("esvazia ate' 40 mmHg")
    x.run(30.0, 0, V1 | V2, until=lambda p: p <= 40.0)
    for pct in (30, 35, 40):
        for ms in (60, 120, 250):
            start = x.settled(1.0)
            for _ in range(5):
                x.pulse(ms, pct, 0)
                x.run(0.3, 0, 0)
            end_p = x.settled(1.0)
            x.mark(f"rajada {pct}% {ms} ms: {(end_p - start) / 5.0:.2f} mmHg por rajada")
            results.append(("rajada", pct, ms, start, (end_p - start) / 5.0))
    return results


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--name", required=True, help="identificacao do manguito (nome do CSV)")
    ap.add_argument("--port", default="auto")
    ap.add_argument("--limit", type=float, default=130.0, help="corte de seguranca do script (mmHg)")
    ap.add_argument("--target", type=float, default=120.0, help="pressao de ensaio (mmHg)")
    ap.add_argument("--rate", type=int, default=500)
    args = ap.parse_args()
    if args.limit > 170.0 or args.target >= args.limit:
        raise SystemExit("limite > 170 ou alvo >= limite: recusado")

    port = args.port
    if port == "auto":
        port = b.find_device()
    ser = serial.Serial(port, 115200, timeout=0.01)
    x = Bench(ser, args.limit)
    out = Path("build/bench")
    out.mkdir(parents=True, exist_ok=True)
    stamp = time.strftime("%Y%m%d_%H%M%S")
    results = []
    try:
        if not b.run_auth_flow(ser, 1, load_key(), 2.0, logout=False):
            raise SystemExit("falha na autenticacao DEV")
        state, p = b.get_state_pressure(ser, 50, 2.0)
        if state != 1 or abs(p) > 5.0:
            raise SystemExit(f"equipamento nao esta em STANDBY/vazio (estado {state}, {p:.1f} mmHg)")
        st = b.send_and_receive(ser, 51, b.CMD_MANUAL_ENTER, b"", 2.0)["payload"][0]
        if st != 0:
            raise SystemExit(f"MANUAL_ENTER recusado: {b.RSP_STATUS_NAMES.get(st, st)}")
        x.set(0, 0)
        ser.write(b.cobs_encode(b.build_request(52, b.CMD_STREAM_START, struct.pack("<H", args.rate))) + b"\x00")
        x.run(0.5, 0, 0)
        x.guard = True
        results = characterize(x, args.target)
    except Abort as e:
        print(f"ABORTADO: {e}", file=sys.stderr)
    finally:
        x.guard = False
        try:
            x.run(40.0, 0, ALL, until=lambda p: p < 3.0)
            x.run(1.0, 0, ALL)
            b.send_and_receive(ser, 60, b.CMD_STREAM_STOP, b"", 2.0)
            b.send_and_receive(ser, 61, b.CMD_MANUAL_EXIT, b"", 2.0)
            b.send_and_receive(ser, 62, b.CMD_AUTH_LOGOUT, b"", 2.0)
        except Exception as e:  # o firmware assume sozinho por falta de sinal de vida
            print(f"aviso ao encerrar: {e}", file=sys.stderr)
        ser.close()
        path = out / f"{args.name}_{stamp}.csv"
        with open(path, "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["t_ms", "p", "sp", "estado", "bomba", "valvulas", "erro"])
            w.writerows(x.samples)
        with open(path.with_suffix(".txt"), "w", encoding="utf-8") as f:
            for t, text in x.marks:
                f.write(f"{t}\t{text}\n")
        print(f"{len(x.samples)} amostras, descartadas={x.dropped}: {path}")


if __name__ == "__main__":
    main()
