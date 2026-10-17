#!/usr/bin/env bash
set -euo pipefail
version="${VERSION:?Falta VERSION}"
arch="${ARCH:?Falta ARCH}"
ort_root="${ORT_ROOT:?Falta ORT_ROOT}"
case "$arch" in
  x64|arm64) ;;
  *) echo "Arquitectura no admitida: $arch" >&2; exit 1 ;;
esac
root="$(mktemp -d)"
trap 'rm -rf "$root"' EXIT
bundle="$root/open-neo-tts"
mkdir -p "$bundle/bin" "$bundle/lib" dist
cp build/oneo-compiler build/open-neo-lite "$bundle/bin/"
cp -a "$ort_root"/lib/libonnxruntime.so* "$bundle/lib/"
patchelf --set-rpath '$ORIGIN/../lib' "$bundle/bin/open-neo-lite"
cp README.md LICENSE "$bundle/"
cat > "$bundle/USAGE.txt" <<'TEXT'
Open Neo TTS (Linux)
Install espeak-ng, espeak-ng-data, JSON-C and OpenSSL runtime packages.
ONNX Runtime is included in lib/.
From the extracted directory:
  ./bin/oneo-compiler --help
  ./bin/open-neo-lite --check-espeak
  ./bin/open-neo-lite infer --model voice.oneo --text "Hello" --out voice.wav
No trainer, Studio or proprietary .neo support is included.
TEXT
"$bundle/bin/oneo-compiler" --help >/dev/null
"$bundle/bin/open-neo-lite" --check-espeak
tar -C "$root" -czf "dist/open-neo-tts-linux-$arch-$version.tar.gz" open-neo-tts
