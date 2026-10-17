#!/usr/bin/env bash
set -euo pipefail
version="$VERSION"
test -n "$version"
root="$(mktemp -d)"
trap 'rm -rf "$root"' EXIT
mkdir -p "$root/open-neo-tts/bin" "$root/open-neo-tts/lib" dist
cp build/oneo-compiler build/open-neo-lite "$root/open-neo-tts/bin/"
cp /tmp/ort/onnxruntime-linux-x64-1.22.0/lib/libonnxruntime.so* "$root/open-neo-tts/lib/"
cp README.md LICENSE "$root/open-neo-tts/"
cat > "$root/open-neo-tts/USAGE.txt" <<'TEXT'
Open Neo TTS for Linux x64
System packages needed: libespeak-ng1, espeak-ng-data, libjson-c5 and OpenSSL.
ONNX Runtime is included with this bundle.

Run from the extracted directory:
  LD_LIBRARY_PATH="$PWD/lib" ./bin/open-neo-lite --help
  ./bin/oneo-compiler --help

This package contains only the public ONEO compiler and Lite inference CLI.
TEXT
tar -C "$root" -czf "dist/open-neo-tts-linux-x64-v$version.tar.gz" open-neo-tts
