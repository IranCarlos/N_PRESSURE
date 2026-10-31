import threading
import time

import pytest

from core import procedures as proc
from core import protocol as p
from core.dev import ALL_VALVES, DevSession, ManualLink, NotStable, V3, diff_params, export_params, parse_key
from core.device import Device, DeviceError
from core.fake_device import FakeDevice

KEY = bytes(range(32))


@pytest.fixture
def link():
    fake = FakeDevice(dev_key=KEY, time_scale=2.0)
    dev = Device(fake)
    dev.stream_start(50)
    yield dev, fake
    dev.close()


@pytest.fixture
def session(link):
    return DevSession.login(link[0], KEY), link[1]


def wait_sample(dev):
    end = time.monotonic() + 1.0
    while dev.last_sample is None and time.monotonic() < end:
        time.sleep(0.01)


def context(dev, sess, cancel=None):
    wait_sample(dev)
    link = ManualLink(sess)
    return link, cancel or threading.Event()


def test_wrong_key_is_refused(link):
    dev, fake = link
    with pytest.raises(DeviceError):
        DevSession.login(dev, bytes(32))
    assert fake.role == 0


def test_login_logout_changes_permissions(session):
    sess, fake = session
    assert sess.params()[0]
    sess.logout()
    with pytest.raises(DeviceError) as err:
        sess.params()
    assert err.value.status == p.RSP_FORBIDDEN


def test_parse_key_from_hex_and_header():
    assert parse_key(KEY.hex()) == KEY
    header = "static const uint8_t DEV_KEY_PROVISION[32] = {" + ",".join(f"0x{b:02x}" for b in KEY) + "};"
    assert parse_key(header) == KEY
    for bad in ("abc", "zz" * 32, "00" * 31):
        with pytest.raises(ValueError):
            parse_key(bad)


def test_key_never_appears_in_session_state(session):
    sess, _ = session
    assert KEY not in [v for v in vars(sess).values()]


def test_param_set_commit_revert(session):
    sess, fake = session
    params, dirty = sess.params()
    assert len(params) == 4 and not dirty
    kp = next(x for x in params if x.name == "pid.kp")
    assert sess.param_set(kp.id, 2.0) == 2.0
    assert sess.params()[1]
    sess.param_revert()
    assert next(x for x in sess.params()[0] if x.name == "pid.kp").value == 1.0
    sess.param_set(kp.id, 3.0)
    sess.param_commit()
    assert fake.commits == 1 and not sess.params()[1]
    assert fake.audit[-1].startswith("param pid.kp")
    with pytest.raises(DeviceError) as err:
        sess.param_set(kp.id, 1000.0)
    assert err.value.status == 7


def test_params_json_roundtrip_and_diff(session, tmp_path):
    sess, _ = session
    params, _ = sess.params()
    path = tmp_path / "p.json"
    export_params(params, path)
    assert diff_params(params, path) == []
    path.write_text('{"pid.kp": 5.0}')
    assert [(c[0].name, c[1]) for c in diff_params(params, path)] == [("pid.kp", 5.0)]
    path.write_text('{"pid.kp": 500.0}')
    with pytest.raises(ValueError):
        diff_params(params, path)
    path.write_text('{"nao.existe": 1}')
    with pytest.raises(ValueError):
        diff_params(params, path)


def test_manual_link_keeps_alive_and_exits_safe(link, session):
    dev, _ = link
    sess, fake = session
    with ManualLink(sess) as m:
        m.set(0, V3)
        time.sleep(0.6)  # mais que os 300 ms do firmware
        assert fake.state == p.ST_MANUAL
        m.set(50, 0)
    assert fake.state == p.ST_IDLE and fake.pump == 0 and fake.valves == ALL_VALVES


def test_exception_inside_manual_still_exits(session):
    sess, fake = session
    with pytest.raises(RuntimeError):
        with ManualLink(sess) as m:
            m.set(80, 0)
            raise RuntimeError("falha no meio")
    assert fake.state == p.ST_IDLE and fake.pump == 0


def test_silent_pc_drops_manual_alone(session):
    sess, fake = session
    sess.manual_enter()
    sess.manual_set(50, 0)
    time.sleep(0.5)
    assert fake.state == p.ST_IDLE and fake.pump == 0


def test_manual_refused_outside_standby(link, session):
    dev, fake = link
    sess, _ = session
    dev.start_test()
    with pytest.raises(DeviceError):
        sess.manual_enter()
    dev.abort_test()


def test_go_to_pressure_settles_in_band(link, session):
    dev, fake = link
    sess, _ = session
    wait_sample(dev)
    with ManualLink(sess) as m:
        ctx = proc.Context(dev, m, threading.Event())
        reached = proc.go_to_pressure(ctx, 100.0)
        assert abs(reached - 100.0) <= proc.BAND_MMHG + 0.5
        assert fake.pump == 0


def test_target_above_limit_never_starts_pump(link, session):
    dev, fake = link
    sess, _ = session
    wait_sample(dev)
    with ManualLink(sess) as m:
        ctx = proc.Context(dev, m, threading.Event())
        with pytest.raises(proc.ProcedureError):
            proc.go_to_pressure(ctx, 200.0)
        assert fake.pump == 0


def test_cancel_stops_and_leaves_safe(link, session):
    dev, fake = link
    sess, _ = session
    wait_sample(dev)
    cancel = threading.Event()
    threading.Timer(0.05, cancel.set).start()
    with pytest.raises(proc.Cancelled):
        with ManualLink(sess) as m:
            proc.go_to_pressure(proc.Context(dev, m, cancel), 150.0)
    assert fake.state == p.ST_IDLE and fake.pump == 0


def test_stale_telemetry_aborts(link, session):
    dev, fake = link
    sess, _ = session
    wait_sample(dev)
    dev.stream_stop()
    time.sleep(1.1)
    with pytest.raises(proc.ProcedureError):
        with ManualLink(sess) as m:
            proc.go_to_pressure(proc.Context(dev, m, threading.Event()), 100.0)
    assert fake.pump == 0


def leak_run(dev, sess, max_drop, hold=0.5, go_timeout=proc.GO_TIMEOUT_S):
    wait_sample(dev)
    with ManualLink(sess) as m:
        ctx = proc.Context(dev, m, threading.Event(), go_timeout_s=go_timeout)
        return proc.leak_test(ctx, 80.0, hold, max_drop)


def test_leak_test_passes_with_small_drop(link, session):
    dev, fake = link
    result = leak_run(dev, session[0], 8.0)
    assert result.passed and result.drop >= 0.0


def test_leak_test_fails_with_real_leak(link, session):
    dev, fake = link
    fake.leak_rate = 0.5
    result = leak_run(dev, session[0], 1.0, hold=1.5)
    assert result.held and not result.passed and result.drop > 1.0


def test_leak_too_big_to_hold_is_rejected(link, session):
    dev, fake = link
    fake.leak_rate = 2.0
    result = leak_run(dev, session[0], 8.0, go_timeout=4.0)
    assert not result.held and not result.passed


def test_leak_test_failure_is_never_approval(link, session):
    dev, fake = link
    sess, _ = session
    wait_sample(dev)
    fake.pressure = 0.0
    with pytest.raises(proc.ProcedureError):
        with ManualLink(sess) as m:
            ctx = proc.Context(dev, m, threading.Event())
            threading.Timer(0.1, lambda: setattr(fake, "state", p.ST_IDLE)).start()
            proc.leak_test(ctx, 150.0, 0.5, 8.0)


def test_calibration_three_points(link, session):
    dev, fake = link
    sess, _ = session
    wait_sample(dev)
    with ManualLink(sess) as m:
        ctx = proc.Context(dev, m, threading.Event())
        sess.cal_start()
        for target in (50.0, 100.0, 150.0):
            reached = proc.go_to_pressure(ctx, target, keep_valves=V3)
            point = sess.cal_point(reached + 1.0)
            assert point.count >= 1
        result = sess.cal_apply()
    assert result.applied and result.span >= 50.0
    assert fake.audit[-1].startswith("cal offset")


def test_calibration_with_too_few_points_is_rejected(session):
    sess, fake = session
    sess.cal_start()
    sess.cal_point(10.0)
    result = sess.cal_apply()
    assert not result.applied and "poucos" in result.reason
    assert fake.cal_offset == 0.0


def test_cal_duplicate_point_refused(session):
    sess, _ = session
    sess.cal_start()
    sess.cal_point(50.0)
    with pytest.raises(DeviceError) as err:
        sess.cal_point(52.0)
    assert err.value.status == 10


def test_not_stable_reports_deviation():
    assert NotStable(1.25).deviation == 1.25


def test_diag_and_selftest(session):
    sess, _ = session
    diag = sess.diagnostics()
    assert diag.exec_us == 38 and diag.stacks["control"] == 215 and not diag.hard_fault
    assert sess.selftest()[0] == []


def test_tlag_estimate():
    times = [0.0, 0.1, 0.2, 0.3, 0.4]
    assert proc.estimate_tlag(times, [10.0, 12.0, 13.0, 12.5, 12.0], 0.0) == pytest.approx(0.2)


def test_stream_1khz_only_for_dev(link, session):
    dev, _ = link
    assert dev.stream_start(1000) == 1000
    dev.stream_start(50)
    session[0].logout()
    with pytest.raises(DeviceError):
        dev.stream_start(1000)
