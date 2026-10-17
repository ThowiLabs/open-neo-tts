#!/usr/bin/env python3
"""ONEO v1/v2 compatibility and immutable v2 model-card export."""
import base64
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

compiler, lite, model, config = map(Path, sys.argv[1:])
png = base64.b64decode(
    'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAusB9Wy1qE8AAAAASUVORK5CYII=')


def call(*arguments, success=True):
    result = subprocess.run([str(arg) for arg in arguments], capture_output=True, text=True)
    assert (result.returncode == 0) == success, (
        str(arguments), result.stdout, result.stderr)
    return result.stdout


with tempfile.TemporaryDirectory(prefix="oneo-interoperability-") as tmp:
    root = Path(tmp)
    cover = root / "portada.png"
    cover.write_bytes(png)
    output = root / "voice-v2.oneo"
    call(compiler, "build", "--source", model, "--config", config, "--out", output,
         "--name", "Mi voz", "--author", "Autor de prueba",
         "--description", "Voz de demostración", "--cover", cover,
         "--license", "CC BY 4.0", "--version", "1.2.0",
         "--url", "https://example.org/voice")
    raw = output.read_bytes()
    assert raw[:8] == b"ONEOTTS2"
    assert struct.unpack_from("<I", raw, 8)[0] == 2
    config_len = struct.unpack_from("<I", raw, 12)[0]
    onnx_len = struct.unpack_from("<Q", raw, 16)[0]
    meta_len, cover_len = struct.unpack_from("<II", raw, 24)
    meta_at = 64 + config_len
    card = json.loads(raw[meta_at:meta_at+meta_len])
    assert card["name"] == "Mi voz"
    assert card["author"] == "Autor de prueba"
    assert card["description"] == "Voz de demostración"
    assert card["license"] == "CC BY 4.0"
    assert card["version"] == "1.2.0"
    assert card["url"] == "https://example.org/voice"
    assert cover_len == len(png)
    assert raw[meta_at+meta_len:meta_at+meta_len+cover_len] == png
    assert raw[-onnx_len:] == model.read_bytes()
    assert "ONEO_VALID format=v2" in call(compiler, "verify", "--model", output)
    assert "v2" in call(lite, "info", "--model", output)

    # Old exports (v1) must be readable without requiring metadata.
    original_config = config.read_bytes()
    original_model = model.read_bytes()
    header = b"ONEOTTS1" + struct.pack("<IIQ", 1, len(original_config), len(original_model))
    body = original_config + original_model
    digest = hashlib.sha256(b"OPEN-NEO-TTS-ONEO-1\0" + header + body).digest()
    legacy = root / "old.oneo"
    legacy.write_bytes(header + digest + body)
    assert "ONEO_VALID format=v1" in call(compiler, "verify", "--model", legacy)
    assert "v1" in call(lite, "info", "--model", legacy)

    # Default exports also contain a card, with optional fields empty.
    without_meta = root / "default.oneo"
    call(compiler, "build", "--source", model, "--config", config, "--out", without_meta)
    assert "ONEO_VALID format=v2" in call(compiler, "verify", "--model", without_meta)

    for original in (output, legacy):
        corrupted = root / ("corrupted-" + original.name)
        tampered = bytearray(original.read_bytes())
        tampered[-3] ^= 0x80
        corrupted.write_bytes(tampered)
        call(compiler, "verify", "--model", corrupted, success=False)
        call(lite, "info", "--model", corrupted, success=False)

    call(compiler, "build", "--source", model, "--config", config,
         "--out", root / "url.oneo", "--url", "file:///secrets", success=False)
    call(compiler, "edit", "--model", output, success=False)
print("ONEO_V1_V2_INTEROPERABILITY_PASS")
