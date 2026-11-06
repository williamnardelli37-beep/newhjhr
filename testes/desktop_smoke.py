#!/usr/bin/env python3
"""Inicia a interface Qt em modo offscreen e exige ao menos uma captura válida."""
import os
import subprocess
import sys
import tempfile
from pathlib import Path

if len(sys.argv) != 2:
    raise SystemExit("Uso: python desktop_smoke.py caminho/para/nodo-app")
exe = Path(sys.argv[1]).resolve()
if not exe.is_file():
    raise SystemExit(f"Executável GUI não encontrado: {exe}")

with tempfile.TemporaryDirectory(prefix="nodo-gui-smoke-") as tmp:
    env = os.environ.copy()
    env["NODO_SHOT"] = tmp
    env["QT_QPA_PLATFORM"] = "offscreen"
    env.setdefault("QT_OPENGL", "software")
    try:
        proc = subprocess.run([str(exe)], env=env, cwd=tmp,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              text=True, timeout=35)
    except subprocess.TimeoutExpired as exc:
        raise SystemExit("A interface Qt não encerrou pelo modo NODO_SHOT em 35s.\n" +
                         (exc.stdout or ""))
    if proc.returncode != 0:
        raise SystemExit(f"A interface encerrou com código {proc.returncode}:\n{proc.stdout}")
    captures = sorted(Path(tmp).glob("aba*.png"))
    if not captures or any(p.stat().st_size < 1000 for p in captures):
        raise SystemExit(f"Capturas ausentes ou vazias. Saída:\n{proc.stdout}")
    print(f"[OK] GUI iniciou e gerou {len(captures)} capturas com Qt offscreen.")
