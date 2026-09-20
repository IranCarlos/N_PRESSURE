#!/usr/bin/env bash
# Compila e roda os testes de host (gcc do MSYS2: C:\msys64\ucrt64\bin).
# Uso: bash test/host/run.sh
set -euo pipefail
export PATH="/c/msys64/ucrt64/bin:$PATH"
cd "$(dirname "$0")/../.."
OUT=build/host
mkdir -p "$OUT"
CFLAGS="-std=c11 -Wall -Wextra -Wconversion -Werror -O0 -g"
fail=0
run() { # nome fontes...
    local name=$1; shift
    gcc $CFLAGS -ICore/Services/Pressure_Core/Inc "$@" -lm -o "$OUT/$name.exe"
    "$OUT/$name.exe" || fail=1
}
run test_pressure_core test/host/test_pressure_core.c Core/Services/Pressure_Core/Src/pressure_core.c
exit $fail
