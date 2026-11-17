#!/usr/bin/env python3
"""Compare every sample with Gale 1.1, including BLOW off/on. Needs git history and g++."""
import os
import pathlib
import subprocess
import tempfile
import array
root = pathlib.Path(__file__).resolve().parent.parent
subprocess.run([os.environ.get("CXX", "g++"), "--version"], check=True)
with tempfile.TemporaryDirectory() as directory:
    work = pathlib.Path(directory)
    (work / "reference.h").write_bytes(subprocess.check_output(["git", "show", "8019da2:Source/GaleDSP.h"], cwd=root))
    files = []
    for label, header in [("before", work / "reference.h"), ("after", root / "Source/GaleDSP.h")]:
        executable = work / label
        subprocess.run([os.environ.get("CXX", "g++"), "-O3", "-std=c++17", "-ffp-contract=off", f'-DHEADER="{header}"',
                        str(root / "Tests/ReferenceRender.cpp"), "-o", str(executable)], check=True)
        output = work / (label + ".f32")
        subprocess.run([str(executable), str(output)], check=True)
        files.append(output)
    with files[0].open("rb") as before, files[1].open("rb") as after:
        size = 0
        while True:
            a, b = before.read(1024 * 1024), after.read(1024 * 1024)
            if a != b:
                aa, bb = array.array("f"), array.array("f")
                aa.frombytes(a); bb.frombytes(b)
                first = next((i for i, (x,y) in enumerate(zip(aa,bb)) if x != y), 0)
                maximum = max(abs(x-y) for x,y in zip(aa,bb))
                raise SystemExit(f"Audio differs at sample {size//4+first}: {aa[first]} vs {bb[first]}, max chunk error {maximum}")
            if not a:
                break
            size += len(a)
    print(f"PASS: {size // 4} channel samples identical, five rates, four environments, BLOW off/on")
