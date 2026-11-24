#!/usr/bin/env bash
# Builds the X3DAudio1_7.dll audio copy guard and its tests.
#   scanner-test                    native; run here against synthetic images
#   x3daudio1_7.dll                 installed into prefixes by Fluorine
#   audio-guard-runtime-test.exe    run under Wine: wine audio-guard-runtime-test.exe <log dir>
set -euo pipefail
root="$(cd "$(dirname "$0")" && pwd)"
out="${1:?usage: build.sh OUTPUT_DIRECTORY}"
mkdir -p "${out}"

g++ -std=c++17 -O2 -Wall -Wextra -Werror "${root}/scanner-test.cpp" -o "${out}/scanner-test"
"${out}/scanner-test"

flags=(-std=c++17 -O2 -Wall -Wextra -Werror -fno-exceptions -static-libgcc -static-libstdc++)
x86_64-w64-mingw32-g++ "${flags[@]}" -shared "${root}/guard.cpp" "${root}/x3daudio1_7.def" \
    -o "${out}/x3daudio1_7.dll"
x86_64-w64-mingw32-strip "${out}/x3daudio1_7.dll"
x86_64-w64-mingw32-g++ "${flags[@]}" "${root}/runtime-test.cpp" \
    -o "${out}/audio-guard-runtime-test.exe"

# The wrapper must forward both X3DAudio exports to Microsoft's DLL.
exports="$(x86_64-w64-mingw32-objdump -p "${out}/x3daudio1_7.dll")"
for name in X3DAudioCalculate X3DAudioInitialize; do
    if ! grep -q "x3daudio1_7_ms.${name}" <<<"${exports}"; then
        echo "ERROR: x3daudio1_7.dll does not forward ${name}" >&2
        exit 1
    fi
done
(cd "${out}" && sha256sum x3daudio1_7.dll > x3daudio1_7.dll.sha256)
