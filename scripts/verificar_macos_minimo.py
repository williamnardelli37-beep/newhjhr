#!/usr/bin/env python3
"""Confere arquitetura universal e deployment target de todos os Mach-O do app."""
import argparse
import os
import re
import subprocess
import sys
from pathlib import Path


def run(args):
    return subprocess.run(args, text=True, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, check=False).stdout


def version_tuple(value):
    return tuple(int(part) for part in value.split(".") if part.isdigit())


def inspect_macho(path, wanted_archs, max_target):
    file_info = run(["file", "-b", str(path)])
    if "Mach-O" not in file_info:
        return False
    arch_output = run(["lipo", "-archs", str(path)]).strip()
    archs = set(arch_output.split())
    if not wanted_archs.issubset(archs):
        raise RuntimeError(f"{path}: arquiteturas {sorted(archs)}, esperado {sorted(wanted_archs)}")

    load_commands = run(["otool", "-l", str(path)])
    versions = re.findall(r"^\s*(?:minos|version)\s+([0-9]+(?:\.[0-9]+){1,2})\s*$",
                          load_commands, re.MULTILINE)
    if not versions:
        raise RuntimeError(f"{path}: não foi possível ler LC_BUILD_VERSION/LC_VERSION_MIN_MACOSX")
    for raw in versions:
        if version_tuple(raw) > max_target:
            raise RuntimeError(f"{path}: exige macOS {raw}, acima do mínimo permitido 12.0")
    print(f"[OK] {path.relative_to(bundle)} | {arch_output} | min={','.join(versions)}")
    return True


parser = argparse.ArgumentParser()
parser.add_argument("bundle", type=Path)
parser.add_argument("--minimum", default="12.0")
parser.add_argument("--archs", default="x86_64,arm64")
args = parser.parse_args()
bundle = args.bundle.resolve()
if not bundle.is_dir() or bundle.suffix != ".app":
    sys.exit(f"Bundle .app inválido: {bundle}")

wanted = set(args.archs.split(","))
minimum = version_tuple(args.minimum)
main_exe = bundle / "Contents/MacOS/Nodo"
if not main_exe.is_file():
    sys.exit(f"Executável principal não encontrado: {main_exe}")

count = 0
try:
    for root, dirs, files in os.walk(bundle, followlinks=False):
        for name in files:
            path = Path(root) / name
            try:
                if path.is_file() and inspect_macho(path, wanted, minimum):
                    count += 1
            except RuntimeError:
                raise
    if count == 0:
        raise RuntimeError("Nenhum binário Mach-O foi encontrado no app.")
except RuntimeError as exc:
    sys.exit(f"[FALHA] {exc}")

main_archs = set(run(["lipo", "-archs", str(main_exe)]).strip().split())
if not wanted.issubset(main_archs):
    sys.exit(f"Executável principal não é universal: {sorted(main_archs)}")
print(f"VALIDAÇÃO OK: {count} binários Mach-O, arquitetura universal, deployment target <= {args.minimum}.")
