#!/usr/bin/env python3
"""Bundle Windows UCRT64 executables and the DLLs required by them."""
from pathlib import Path
import os
import re
import shutil
import subprocess
import zipfile

ucrt_prefix = Path(subprocess.check_output(
    ["cygpath", "-w", "/ucrt64"], text=True).strip())
ucrt_bin = ucrt_prefix / "bin"
version = os.environ["VERSION"]
archive_path = Path("dist") / ("open-neo-tts-windows-x64-" + version + ".zip")
staging = Path("dist") / "windows-content"
staging.mkdir(parents=True, exist_ok=True)
for exe in ("oneo-compiler.exe", "open-neo-lite.exe"):
    shutil.copy2(Path("build") / exe, staging / exe)

pending = list(staging.glob("*.exe"))
# ONNX Runtime also loads providers dynamically.
for name in ("onnxruntime.dll", "onnxruntime_providers_shared.dll"):
    source = ucrt_bin / name
    if source.is_file() and not (staging / name).exists():
        shutil.copy2(source, staging / name)
        pending.append(staging / name)

# Resolve all UCRT64 runtime imports recursively. objdump's output is
# independent of whether the invoking Python came from MSYS2 or Windows.
seen = set()
while pending:
    binary = pending.pop()
    if binary.name.lower() in seen:
        continue
    seen.add(binary.name.lower())
    result = subprocess.run(["objdump", "-p", str(binary)],
                            capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError("Cannot inspect DLL imports for: " + str(binary))
    for name in re.findall(r"DLL Name:\s*([^\s]+\.dll)",
                           result.stdout, re.IGNORECASE):
        source = ucrt_bin / name
        target = staging / name
        if source.is_file() and not target.exists():
            shutil.copy2(source, target)
            pending.append(target)

data = ucrt_prefix / "share" / "espeak-ng-data"
if not data.is_dir():
    raise RuntimeError("eSpeak-NG language data is missing")
shutil.copytree(data, staging / "espeak-ng-data", dirs_exist_ok=True)
for name in ("README.md", "LICENSE"):
    shutil.copy2(name, staging / name)
notices = staging / "THIRD_PARTY"
notices.mkdir(exist_ok=True)
for name in ("onnxruntime", "espeak-ng", "json-c", "openssl"):
    source = ucrt_prefix / "share" / "licenses" / name
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
