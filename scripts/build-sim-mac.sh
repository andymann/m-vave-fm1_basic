#!/usr/bin/env bash
# Build the FM1 browser simulator (WASM) natively on macOS/Linux.
# Mirrors fm1_open/tools/Build-Web.ps1 -LocalPreview (which only runs on Windows).
# Output: fm1_open/build/preview/index.html  (open in Chrome/Edge)
set -euo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"
ROOT="${FM1_ROOT:-$HERE/fm1_open}"
case "$(uname -s)-$(uname -m)" in
  Darwin-arm64) SDKNAME=wasi-sdk-27.0-arm64-macos ;;
  Darwin-x86_64) SDKNAME=wasi-sdk-27.0-x86_64-macos ;;
  Linux-x86_64) SDKNAME=wasi-sdk-27.0-x86_64-linux ;;
  Linux-aarch64) SDKNAME=wasi-sdk-27.0-arm64-linux ;;
  *) echo "unsupported host"; exit 2 ;;
esac
WASI="${FM1_WASI_SDK:-$HERE/toolchains/$SDKNAME}"
if [ ! -x "$WASI/bin/clang" ]; then
  mkdir -p "$HERE/toolchains"
  echo "Downloading $SDKNAME ..."
  curl -fL --retry 2 -o "$HERE/toolchains/$SDKNAME.tar.gz" \
    "https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-27/$SDKNAME.tar.gz"
  tar -xzf "$HERE/toolchains/$SDKNAME.tar.gz" -C "$HERE/toolchains"
  xattr -dr com.apple.quarantine "$WASI" 2>/dev/null || true
fi

OUT="$ROOT/build/web"
mkdir -p "$OUT"
cd "$ROOT"
# Same source selection as Build-Web.ps1: .c files in app/, web/, third_party/synth, excluding app/board/
SOURCES=()
while IFS= read -r f; do SOURCES+=("$f"); done < <(
  find app web third_party/synth -type f -name '*.c' ! -path 'app/board/*' | LC_ALL=C sort)

echo "Compile the browser application (${#SOURCES[@]} files)"
"$WASI/bin/clang" --sysroot="$WASI/share/wasi-sysroot" \
  -std=c11 -O2 -fwrapv -fno-fast-math -ffp-contract=off \
  -DFM1_HOST_TEST -DFM1_OPEN_WEB -Wall -Wextra -Werror \
  -Wno-unused-function -Wno-array-parameter -mexec-model=reactor \
  -Iweb/engine -Iweb/synth -Ithird_party/synth -Isdk/include -Iapp/synth -Iapp \
  "${SOURCES[@]}" \
  -Wl,--strip-all -Wl,-z,stack-size=131072 -Wl,--initial-memory=4194304 -Wl,--max-memory=33554432 \
  -o "$OUT/simulator.wasm"

python3 -I "$HERE/scripts/make_preview.py" "$ROOT" "$OUT/simulator.wasm"
