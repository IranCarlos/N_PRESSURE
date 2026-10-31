#!/usr/bin/env python3
"""Fase 7.9: le GET_DIAG, roda SELFTEST e, com --reboot, reinicia o equipamento.

    python tools/bench_diag.py [--port COM3] [--reboot]

--check-refusals tambem confirma as recusas: comandos sem sessao DEV e REBOOT
durante um ensaio (inicia e aborta um ensaio: so' com o manguito fora de pessoa).
A chave DEV vem de dev_key_secret.h (nao versionado) e fica so' em memoria.
"""
import argparse
import contextlib
import io
import struct
import time

import serial

import bench_usb_hello as b
from bench_characterize import load_key

CMD_SELFTEST = 0x0140
CMD_GET_DIAG = 0x0150
CMD_REBOOT = 0x0160
TASKS = ("control", "safety", "input", "app", "usbProto", "storage", "ui", "audio")
FAIL_NAMES = ("SD", "RTC", "EEPROM", "SENSOR", "ZERO")


def auth(ser):
    with contextlib.redirect_stdout(io.StringIO()):
        if not b.run_auth_flow(ser, 1, load_key(), 2.0, logout=False):
            raise SystemExit("falha na autenticacao DEV")


def status_name(st):
    return b.RSP_STATUS_NAMES.get(st, st)


def show_diag(ser, seq):
    p = b.send_and_receive(ser, seq, CMD_GET_DIAG, b"", 2.0)["payload"]
    if p[0] != 0:
        print(f"GET_DIAG: {status_name(p[0])}")
        return False
    up, jit, rsr, drop, heap, heap_min = struct.unpack("<6I", p[1:25])
    flags = p[25]
    pc, lr, cfsr, hfsr = struct.unpack("<4I", p[26:42])
    stacks = struct.unpack(f"<{p[42]}H", p[43:43 + 2 * p[42]])
    print(f"uptime {up / 1000.0:.1f} s, jitter max {jit} us, RCC_RSR {rsr:#010x}, telemetria descartada {drop}")
    print(f"heap livre {heap} B (minimo {heap_min} B), hard fault={flags & 1}, IWDG={(flags >> 1) & 1}")
    if flags & 1:
        print(f"fault: pc={pc:#010x} lr={lr:#010x} cfsr={cfsr:#010x} hfsr={hfsr:#010x}")
    print("pilha livre (palavras): " + ", ".join(f"{n}={s}" for n, s in zip(TASKS, stacks)))
    return True


def show_selftest(ser, seq):
    p = b.send_and_receive(ser, seq, CMD_SELFTEST, b"", 2.0)["payload"]
    if p[0] != 0:
        print(f"SELFTEST: {status_name(p[0])}")
        return
    fails = [n for i, n in enumerate(FAIL_NAMES) if p[1] & (1 << i)]
    (pressure,) = struct.unpack("<f", p[2:6])
    print(f"SELFTEST: {'APROVADO' if not fails else 'REPROVADO: ' + ', '.join(fails)} (pressao {pressure:.1f} mmHg)")


def refusals(ser):
    st = b.send_and_receive(ser, 10, CMD_GET_DIAG, b"", 2.0)["payload"][0]
    print(f"GET_DIAG sem DEV: {status_name(st)}")
    st = b.send_and_receive(ser, 11, CMD_REBOOT, b"", 2.0)["payload"][0]
    print(f"REBOOT sem DEV: {status_name(st)}")
    auth(ser)
    b.send_and_receive(ser, 20, b.CMD_SET_SETPOINT, struct.pack("<f", 60.0), 2.0)
    st = b.send_and_receive(ser, 21, b.CMD_START_TEST, b"", 2.0)["payload"][0]
    print(f"START_TEST: {status_name(st)}")
    time.sleep(0.3)
    try:
        st = b.send_and_receive(ser, 22, CMD_REBOOT, b"", 2.0)["payload"][0]
        print(f"REBOOT durante ensaio: {status_name(st)}")
        st = b.send_and_receive(ser, 23, CMD_SELFTEST, b"", 2.0)["payload"][0]
        print(f"SELFTEST durante ensaio: {status_name(st)}")
    finally:
        b.send_and_receive(ser, 24, b.CMD_ABORT_TEST, b"", 2.0)
    time.sleep(4.0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM3")
    ap.add_argument("--reboot", action="store_true")
    ap.add_argument("--check-refusals", action="store_true")
    args = ap.parse_args()
    ser = serial.Serial(args.port, 115200, timeout=0.2)
    if args.check_refusals:
        refusals(ser)
    else:
        auth(ser)
    show_diag(ser, 40)
    show_selftest(ser, 41)
    if args.reboot:
        st = b.send_and_receive(ser, 42, CMD_REBOOT, b"", 2.0)["payload"][0]
        print(f"REBOOT: {status_name(st)}")
    else:
        b.send_and_receive(ser, 43, b.CMD_AUTH_LOGOUT, b"", 2.0)
    ser.close()


if __name__ == "__main__":
    main()
