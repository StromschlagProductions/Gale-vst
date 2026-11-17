#!/usr/bin/env python3
"""Compare every sample with Gale 1.1, including BLOW off/on. Needs git history and g++."""
import os
import pathlib
import subprocess
import tempfile
root = pathlib.Path(__file__).resolve().parent.parent
with tempfile.TemporaryDirectory() as directory:
    work = pathlib.Path(directory)
    (work / "reference.h").write_bytes(subprocess.check_output(["git", "show", "8019da2:Source/GaleDSP.h"], cwd=root))
    files = []
    for label, header in [("before", work / "reference.h"), ("after", root / "Source/GaleDSP.h")]:
        executable = work / label
        subprocess.run([os.environ.get("CXX", "g++"), "-O3", "-std=c++17", f'-DHEADER="{header}"',
                        str(root / "Tests/ReferenceRender.cpp"), "-o", str(executable)], check=True)
        output = work / (label + ".f32")
        subprocess.run([str(executable), str(output)], check=True)
        files.append(output)
    with files[0].open("rb") as before, files[1].open("rb") as after:
        size = 0
        while True:
            a, b = before.read(1024 * 1024), after.read(1024 * 1024)
            if a != b:
                raise SystemExit(f"Audio differs in chunk starting at byte {size}")
            if not a:
                break
            size += len(a)
    print(f"PASS: {size // 4} channel samples identical, five rates, four environments, BLOW off/on")
