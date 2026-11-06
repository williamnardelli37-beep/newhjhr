#!/usr/bin/env python3
"""Confere arquitetura universal e deployment target de todos os Mach-O do app."""
import argparse
import os
import subprocess
import sys
from pathlib import Path


def run(args):
    return subprocess.run(args, text=True, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, check=False).stdout


def version_tuple(value):
    return tuple(int(part) for part in value.split(".") if part.isdigit())


def deployment_targets(load_commands):
    """Le SO os campos de versao minima de macOS: LC_BUILD_VERSION (minos, so
    quando platform=MACOS) e o LC_VERSION_MIN_MACOSX legado (version). Ignora
    de proposito qualquer outro campo chamado 'version' (LC_SOURCE_VERSION,
    'current version'/'compatibility version' de bibliotecas, etc.): esses
    nao sao a versao minima do sistema e ja causaram falso positivo aqui."""
    versions = []
    commands = load_commands.split("Load command ")
    for block in commands:
        if "cmd LC_BUILD_VERSION" in block:
            if "platform MACOS" not in block:
                continue  # outra plataforma (ex.: MACCATALYST) nao conta aqui
            for line in block.splitlines():
                line = line.strip()
                if line.startswith("minos "):
                    versions.append(line.split()[1])
                    break
        elif "cmd LC_VERSION_MIN_MACOSX" in block:
            for line in block.splitlines():
                line = line.strip()
                if line.startswith("version "):
                    versions.append(line.split()[1])
                    break
    return versions


def inspect_macho(path, wanted_archs, max_target):
    file_info = run(["file", "-b", str(path)])
    if "Mach-O" not in file_info:
        return False
    arch_output = run(["lipo", "-archs", str(path)]).strip()
    archs = set(arch_output.split())
    if not wanted_archs.issubset(archs):
        raise RuntimeError(f"{path}: arquiteturas {sorted(archs)}, esperado {sorted(wanted_archs)}")

    load_commands = run(["otool", "-l", str(path)])
    versions = deployment_targets(load_commands)
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
