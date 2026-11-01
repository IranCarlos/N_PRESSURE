import hashlib
import time
from datetime import date, datetime
from pathlib import Path

import pytest

from core import protocol as p
from core import reports, sdfiles as sd
from core.archive import Archive
from core.device import Device, DeviceError
from core.fake_device import FakeDevice, VirtualCard

SD_SAMPLES = Path(__file__).parent / "data"


@pytest.fixture
def link():
    fake = FakeDevice(time_scale=25.0)
    dev = Device(fake)
    yield dev, fake
    dev.close()


def test_crc_matches_firmware():
    # vetor classico do CRC-32/MPEG-2, o mesmo algoritmo de usb_crc32.c
    assert p.crc32(b"123456789") == 0x0376E6E7


@pytest.mark.parametrize("data", [b"", b"\x00", b"\x00\x00", b"abc", b"a\x00b", bytes(range(1, 255)) * 2, b"\x01" * 600])
def test_cobs_roundtrip(data):
    encoded = p.cobs_encode(data)
    assert 0 not in encoded
    assert p.cobs_decode(encoded) == data


def test_frame_rejects_corruption():
    wire = bytearray(p.encode_frame(p.Frame(p.TYPE_REQ, 7, p.CMD_HELLO, b"ab")))
    wire[3] ^= 0x10
    assert p.FrameSplitter().feed(bytes(wire)) == []


def test_splitter_handles_partial_chunks():
    wire = p.encode_frame(p.Frame(p.TYPE_RSP, 1, 2, b"xyz")) * 2
    s = p.FrameSplitter()
    assert s.feed(wire[:5]) == []
    assert len(s.feed(wire[5:])) == 2


def test_hello_and_status(link):
    dev, _ = link
    assert dev.hello().device_id.startswith(p.DEVICE_ID_PREFIX)
    assert dev.status().state == p.ST_IDLE


def test_setpoint_out_of_range_is_refused(link):
    dev, _ = link
    with pytest.raises(DeviceError) as err:
        dev.set_setpoint(500.0)
    assert err.value.status == 7


@pytest.mark.parametrize("cmd", [0x0100, 0x0102, 0x0103, 0x0110, 0x0112, 0x0120, 0x0122, 0x0150, 0x0160])
def test_dev_commands_are_refused(link, cmd):
    dev, _ = link
    with pytest.raises(DeviceError) as err:
        dev.request(cmd, b"\x00\x00")
    assert err.value.status == p.RSP_FORBIDDEN


def test_user_client_has_no_control_functions():
    forbidden = ("param", "manual", "cal_", "calib", "reboot", "selftest", "auth")
    assert not [n for n in dir(Device) if any(w in n.lower() for w in forbidden)]


def wait_for(cond, seconds=3.0):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        if cond():
            return True
        time.sleep(0.01)
    return False


def test_cycle_events_and_stream(link):
    dev, fake = link
    states, done, samples = [], [], []
    dev.on_event = lambda cmd, pl: (states.append(pl[1]) if cmd == p.EVT_STATE else
                                    done.append(p.parse_test_done(pl)) if cmd == p.EVT_TEST_DONE else None)
    dev.on_samples = lambda s, _: samples.extend(s)
    dev.hello()
    dev.stream_start(50)
    dev.start_test()
    assert wait_for(lambda: done)
    assert states == [p.ST_INFLATING, p.ST_HOLD, p.ST_DEFLATING, p.ST_IDLE]
    assert done[0].nc_code == "NC00"
    assert max(s.pressure for s in samples) > 50.0
    dev.stream_stop()


def test_read_file_larger_than_block(link):
    dev, fake = link
    blob = bytes(i % 251 for i in range(5000))
    fake.card.files["/tests/big.bin"] = bytearray(blob)
    assert dev.fs_read_verified("/tests/big.bin") == blob


def test_read_refuses_outside_user_roots(link):
    dev, _ = link
    with pytest.raises(DeviceError):
        dev.fs_read("/logs/audit/2026-10.log")


def run_cycles(dev, fake, count):
    for _ in range(count):
        dev.start_test()
        assert wait_for(lambda: fake.state != p.ST_IDLE or fake._cycle is None)
        assert wait_for(lambda: fake.state == p.ST_IDLE)


def test_archive_sync_and_integrity(link, tmp_path):
    dev, fake = link
    run_cycles(dev, fake, 2)
    archive = Archive(tmp_path)
    archive.sync(dev)
    assert len(archive.index()) == 2
    assert archive.curve(archive.index()[0])
    assert archive.verify().verdict == sd.INTACT
    run_cycles(dev, fake, 1)
    archive.sync(dev)
    assert len(archive.index()) == 3
    assert archive.verify().verdict == sd.INTACT


def tamper(path: Path, old: bytes, new: bytes):
    data = path.read_bytes()
    assert old in data
    path.write_bytes(data.replace(old, new, 1))


def card_with_tests():
    card = VirtualCard()
    when = datetime(2026, 10, 31, 12, 0, 0)
    for i in range(3):
        card.store_test(when.replace(minute=i), 100.0, 102.0, 100.0, 5.0, 8.0, 1.5, 3.0, "NC00", 1, 2, [0.0, 50.0, 100.0])
    return card


def synced_archive(tmp_path):
    fake = FakeDevice(card_with_tests())
    dev = Device(fake)
    archive = Archive(tmp_path)
    archive.sync(dev)
    dev.close()
    return archive


def test_edited_record_is_tampered(tmp_path):
    archive = synced_archive(tmp_path)
    tamper(archive.local("/tests/2026/10/T00000002.rec"), b"Max 102.0", b"Max 101.0")
    result = archive.verify()
    assert result.verdict == sd.TAMPERED
    assert [e.test_id for e, v in result.tests.items() if v == sd.TAMPERED] == ["T00000002"]


def test_edited_curve_is_tampered(tmp_path):
    archive = synced_archive(tmp_path)
    tamper(archive.local("/tests/2026/10/T00000001.csv"), b"100.0", b"101.0")
    assert archive.verify().verdict == sd.TAMPERED


def test_edited_index_is_tampered(tmp_path):
    archive = synced_archive(tmp_path)
    tamper(archive.local("/tests/index.csv"), b"NC00", b"NC01")
    assert archive.verify().verdict == sd.TAMPERED


def test_removed_log_line_is_tampered(tmp_path):
    archive = synced_archive(tmp_path)
    path = archive.local("/logs/events/2026-10.log")
    lines = path.read_text().splitlines(keepends=True)
    path.write_text("".join(lines[:1] + lines[2:]))
    report = archive.verify().logs[0]
    assert report.verdict == sd.TAMPERED
    assert "elo quebrado" in report.problems[0]


def test_truncated_lines_are_never_called_intact():
    prev = "0" * 64
    body = f"2026-10-22T01:10:48-03:00;seq=000013;lvl=ERR;code=E013;test=;state=X;p=1.0;msg=m;prev={prev}"
    digest = hashlib.sha256((prev + body).encode()).hexdigest()
    report, records = sd.verify_log("x", f"{body};hash={digest[:50]}\n")
    assert report.verdict == sd.UNVERIFIABLE
    assert records[0].code == "E013"


def test_chain_restart_is_unverifiable_not_tampered():
    def line(prev, seq):
        body = f"2026-10-22T01:10:48-03:00;seq={seq};lvl=EVT;code=;test=;state=X;p=1.0;msg=m;prev={prev}"
        digest = hashlib.sha256((prev + body).encode()).hexdigest()
        return f"{body};hash={digest}", digest

    first, h1 = line(sd.GENESIS, 1)
    second, _ = line(sd.GENESIS, 2)  # reboot do firmware antigo
    report, _ = sd.verify_log("x", first + chr(10) + second + chr(10))
    assert report.verdict == sd.UNVERIFIABLE and not report.problems
    assert h1


def test_parse_rec_with_real_firmware_line():
    line = ("T00000027 | P000 | OP00 | 31/10/2026 12:36:45 | Ciclo 27 | Alvo 100.0 mmHg | Max 103.6 | "
            "Plato medio 100.2 | Faixa 98.3-103.6 | Insuflacao 3.4 s | Plato 15.0 s | Descompressao 0.6 s | "
            "Pressao residual 4.1 mmHg | Nao conforme | NC02 | E000 | myPRESSURE NP0")
    rec = sd.parse_rec_line(line)
    assert rec.peak == 103.6 and rec.plateau_min == 98.3 and rec.nc == "NC02" and not rec.complete


def test_parse_rec_old_sentinel_does_not_crash():
    line = ("T00000027 | P000 | OP00 | 31/10/2026 17:24:00 | Ciclo 27 | Alvo 100.0 mmHg | Max 100.0 | "
            "Plato medio 0.0 | Faixa 1000000000.0--1000000000.0 | Insuflacao 8.1 s | Plato 0.0 s | "
            "Descompressao 2.4 s | Pressao residual 8.5 mmHg | Nao conforme | NC07 | E000 | m")
    assert sd.parse_rec_line(line).nc == "NC07"


def test_exports_1000_tests(tmp_path):
    entries = []
    for i in range(1000):
        body = f"T{i + 1:08d};31/10/2026 10:{i // 60 % 60:02d}:{i % 60:02d};OP01;100.0;101.0;NC00;"
        entries.append(body + hashlib.sha256((sd.GENESIS + body).encode()).hexdigest())
    parsed = sd.parse_index("\n".join(entries))
    assert all(e.hash_ok for e in parsed)
    rows = [reports.TestRow(e, None, sd.INTACT) for e in parsed]
    started = time.monotonic()
    reports.export_csv(rows, tmp_path / "a.csv")
    reports.export_xlsx(rows, tmp_path / "a.xlsx")
    reports.export_usage_pdf(reports.summarize(parsed, date(2026, 10, 1), date(2026, 10, 31)), tmp_path / "u.pdf")
    assert time.monotonic() - started < 20.0
    assert len((tmp_path / "a.csv").read_text("utf-8-sig").splitlines()) == 1001
    for name in ("a.xlsx", "u.pdf"):
        assert (tmp_path / name).stat().st_size > 500


def test_test_pdf_and_errors_pdf(tmp_path):
    entry = sd.parse_index("T00000001;31/10/2026 10:00:00;OP01;100.0;101.0;NC05;" + "0" * 64)[0]
    reports.export_test_pdf(reports.TestRow(entry, None, sd.UNVERIFIABLE), [(0.0, 0.0), (1.0, 100.0)], tmp_path / "t.pdf")
    rec = sd.LogRecord("2026-10-31T10:00:00-03:00", "ERR", "E013", "", "X", 95.0, "Vazamento", True)
    picked = reports.errors_in_period([rec], date(2026, 10, 1), date(2026, 10, 31))
    reports.export_errors_pdf(picked, date(2026, 10, 1), date(2026, 10, 31), tmp_path / "e.pdf")
    assert picked and (tmp_path / "t.pdf").exists() and (tmp_path / "e.pdf").exists()


def test_wiped_card_clears_local_copy(tmp_path):
    fake = FakeDevice(card_with_tests())
    dev = Device(fake)
    archive = Archive(tmp_path)
    archive.sync(dev)
    assert archive.local("/tests/2026/10/T00000001.rec").exists()
    fake.card = VirtualCard()
    fake.card.store_test(datetime(2026, 11, 1, 9, 0, 0), 80.0, 81.0, 80.0, 5.0, 8.0, 1.5, 3.0, 'NC00', 1, 2, [0.0, 80.0])
    archive.sync(dev)
    dev.close()
    assert not archive.local("/tests/2026/10/T00000001.rec").exists()
    assert [e.setpoint for e in archive.index()] == [80.0]
    assert archive.verify().verdict == sd.INTACT


def test_sync_replaces_a_bad_local_copy(tmp_path):
    fake = FakeDevice(card_with_tests())
    dev = Device(fake)
    archive = Archive(tmp_path)
    archive.sync(dev)
    tamper(archive.local("/logs/events/2026-10.log"), b"lvl=EVT", b"lvl=EVX")
    archive.sync(dev)
    dev.close()
    assert archive.verify().verdict == sd.INTACT


def chain(*links):
    out, digests = [], []
    for i, ref in enumerate(links):
        prev = sd.GENESIS if ref is None else digests[ref]
        body = f'2026-10-31T10:00:{i:02d}-03:00;seq={i};lvl=EVT;code=;test=;state=X;p=1.0;msg=evento {i};prev={prev}'
        digest = hashlib.sha256((prev + body).encode()).hexdigest()
        out.append(f'{body};hash={digest}')
        digests.append(digest)
    return chr(10).join(out) + chr(10)


def test_resume_from_older_line_is_not_tampering():
    report, _ = sd.verify_log('x', chain(None, 0, 1, 1))  # 4a linha retomou da 2a, e a 3a continua la'
    assert report.verdict == sd.UNVERIFIABLE and not report.problems
    issue = report.issues[0]
    assert issue.line == 4 and issue.kind == 'Retomada após reinício' and not issue.tampered
    assert issue.when == '2026-10-31 10:00:03' and issue.detail == 'evento 3'


def test_removed_line_points_where():
    lines = chain(None, 0, 1, 2).splitlines()
    report, _ = sd.verify_log('x', chr(10).join(lines[:1] + lines[2:]))
    assert report.verdict == sd.TAMPERED
    issue = report.issues[0]
    assert issue.line == 2 and issue.tampered and issue.detail == 'evento 2'


def test_edited_line_points_where():
    text = chain(None, 0, 1).replace('msg=evento 1', 'msg=evento 9')
    report, _ = sd.verify_log('x', text)
    assert report.issues[0].kind == 'Conteúdo alterado' and report.issues[0].line == 2


def test_tampered_test_says_what(tmp_path):
    archive = synced_archive(tmp_path)
    tamper(archive.local('/tests/2026/10/T00000001.csv'), b'100.0', b'101.0')
    issue = next(i for i in archive.verify().issues if i.tampered)
    assert issue.source == 'T00000001' and 'Curva' in issue.kind
