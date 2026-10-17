#!/usr/bin/env python3
"""Bundle Windows UCRT64 executables and the DLLs required by them."""
from pathlib import Path
import os
import re
import shutil
import subprocess
import zipfile

version = os.environ["VERSION"]
archive_path = Path("dist") / ("open-neo-tts-windows-x64-v" + version + ".zip")
staging = Path("dist") / "windows-content"
staging.mkdir(parents=True, exist_ok=True)
for exe in ("oneo-compiler.exe", "open-neo-lite.exe"):
    shutil.copy2(Path("build") / exe, staging / exe)

# Inspect both programs and newly copied libraries until all UCRT dependencies
# are present. Never ship DLLs from Windows system folders.
pending = list(staging.glob("*.exe"))
seen = set()
while pending:
    binary = pending.pop()
    if binary.name.lower() in seen:
        continue
    seen.add(binary.name.lower())
    result = subprocess.run(["ldd", str(binary)], capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError("Cannot inspect runtime dependencies: " + str(binary) + result.stderr)
    for line in result.stdout.splitlines():
        for dll in re.findall(r"/ucrt64/bin/[^\s()]+\.dll", line, re.IGNORECASE):
            source = Path(dll)
            destination = staging / source.name
            if not destination.exists():
                shutil.copy2(source, destination)
                pending.append(destination)

for library in ("onnxruntime.dll", "onnxruntime_providers_shared.dll"):
    src = Path("/ucrt64/bin") / library
    if src.exists() and not (staging / library).exists():
        shutil.copy2(src, staging / library)
        pending.append(staging / library)

data = Path("/ucrt64/share/espeak-ng-data")
if not data.is_dir():
    raise RuntimeError("eSpeak-NG language data is missing")
shutil.copytree(data, staging / "espeak-ng-data", dirs_exist_ok=True)
for name in ("README.md", "LICENSE"):
    shutil.copy2(name, staging / name)
notices = staging / "THIRD_PARTY"
notices.mkdir(exist_ok=True)
for name in ("onnxruntime", "espeak-ng", "json-c", "openssl"):
    source = Path("/ucrt64/share/licenses") / name
    if source.is_dir():
        shutil.copytree(source, notices / name, dirs_exist_ok=True)
(staging / "USAGE.txt").write_text(
    "Open Neo TTS for Windows x64\n\n"
    "Run oneo-compiler.exe --help or open-neo-lite.exe --help.\n"
    "Keep all DLLs and the espeak-ng-data folder beside the EXEs.\n",
    encoding="utf-8",
)
archive_path.parent.mkdir(exist_ok=True)
with zipfile.ZipFile(archive_path, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
    for file in staging.rglob("*"):
        if file.is_file():
            archive.write(file, file.relative_to(staging).as_posix())
print("WINDOWS_RELEASE_READY", archive_path)
