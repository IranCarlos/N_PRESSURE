#!/usr/bin/env bash
# Compila e roda os testes de host (gcc do MSYS2: C:\msys64\ucrt64\bin).
# Uso: bash test/host/run.sh
set -euo pipefail
export PATH="/c/msys64/ucrt64/bin:$PATH"
cd "$(dirname "$0")/../.."
OUT=build/host
mkdir -p "$OUT"
CFLAGS="-std=c11 -Wall -Wextra -Wconversion -Werror -O0 -g"
INC="-ICore/Services/Pressure_Core/Inc -ICore/Bsp/Valve/Inc -ICore/Bsp/PwmMotor/Inc -ICore/App/Bench/Inc -ICore/Services/Rtc_Service/Inc -ICore/Services/Control_Core/Inc -ICore/Common/Inc -ICore/Services/Audit_Trail/Inc -ICore/Services/UsbProto/Inc -ICore/Services/Event_Watch/Inc"
fail=0
run() { # nome fontes...
    local name=$1; shift
    gcc $CFLAGS $INC "$@" -lm -o "$OUT/$name.exe"
    "$OUT/$name.exe" || fail=1
}
run test_pressure_core test/host/test_pressure_core.c Core/Services/Pressure_Core/Src/pressure_core.c
run test_actuators test/host/test_actuators.c Core/Bsp/Valve/Src/valve.c \
    Core/Bsp/PwmMotor/Src/pwm_motor.c Core/App/Bench/Src/actuator_bench.c
run test_rtc_validity test/host/test_rtc_validity.c Core/Services/Rtc_Service/Src/rtc_validity.c
run test_control_core test/host/test_control_core.c Core/Services/Control_Core/Src/control_core.c test/host/sim_plant.c
run test_audit_trail test/host/test_audit_trail.c Core/Services/Audit_Trail/Src/sha256.c \
    Core/Services/Audit_Trail/Src/audit_chain.c Core/Services/Audit_Trail/Src/cycle_record.c
run test_storage_format test/host/test_storage_format.c Core/Services/Audit_Trail/Src/sha256.c \
    Core/Services/Audit_Trail/Src/audit_chain.c Core/Services/Audit_Trail/Src/cycle_record.c \
    Core/Services/Audit_Trail/Src/storage_format.c
run test_cobs test/host/test_cobs.c Core/Services/UsbProto/Src/cobs.c
run test_usb_crc32 test/host/test_usb_crc32.c Core/Services/UsbProto/Src/usb_crc32.c
run test_usb_proto_frame test/host/test_usb_proto_frame.c \
    Core/Services/UsbProto/Src/usb_proto_frame.c Core/Services/UsbProto/Src/usb_crc32.c
run test_usb_proto_dispatch test/host/test_usb_proto_dispatch.c \
    Core/Services/UsbProto/Src/usb_proto_dispatch.c Core/Services/UsbProto/Src/usb_proto_auth.c \
    Core/Services/UsbProto/Src/hmac_sha256.c Core/Services/Audit_Trail/Src/sha256.c \
    Core/Services/UsbProto/Src/usb_telemetry.c
run test_usb_telemetry test/host/test_usb_telemetry.c Core/Services/UsbProto/Src/usb_telemetry.c
run test_event_watch test/host/test_event_watch.c Core/Services/Event_Watch/Src/event_watch.c
run test_usb_proto_policy test/host/test_usb_proto_policy.c Core/Services/UsbProto/Src/usb_proto_policy.c
run test_usb_rx_ring test/host/test_usb_rx_ring.c Core/Services/UsbProto/Src/usb_rx_ring.c
run test_hmac_sha256 test/host/test_hmac_sha256.c Core/Services/UsbProto/Src/hmac_sha256.c \
    Core/Services/Audit_Trail/Src/sha256.c
run test_usb_proto_auth test/host/test_usb_proto_auth.c Core/Services/UsbProto/Src/usb_proto_auth.c \
    Core/Services/UsbProto/Src/hmac_sha256.c Core/Services/Audit_Trail/Src/sha256.c
exit $fail
