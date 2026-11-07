#!/usr/bin/env python3
"""Smoke test do CLI/servidor sem Qt: integra, inicia, consulta health e grava um documento."""
import http.client
import json
import os
import re
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

if len(sys.argv) != 2:
    raise SystemExit("Uso: python3 testes/platform_smoke.py caminho/para/nodo")
binary = str(Path(sys.argv[1]).resolve())
if not Path(binary).is_file():
    raise SystemExit(f"Binário ausente: {binary}")

with tempfile.TemporaryDirectory(prefix="nodo-platform-smoke-") as tmp:
    site = Path(tmp) / "site"
    site.mkdir()
    subprocess.run([binary, "integrar", str(site)], check=True, capture_output=True, text=True, timeout=20)
    conf_path = site / "nodo.conf"
    conf = conf_path.read_text(encoding="utf-8")
    key_match = re.search(r"(?m)^chave=(.+)$", conf)
    if not key_match:
        raise SystemExit("nodo.conf não contém chave de administrador")
    key = key_match.group(1).strip()
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        port = sock.getsockname()[1]
    conf = re.sub(r"(?m)^porta=\d+", f"porta={port}", conf)
    conf_path.write_text(conf, encoding="utf-8")

    log_path = Path(tmp) / "nodo.log"
    with log_path.open("w", encoding="utf-8") as log:
        proc = subprocess.Popen([binary, "rodar", str(site)], cwd=site,
                                stdout=log, stderr=subprocess.STDOUT)
        try:
            healthy = False
            for _ in range(100):
                if proc.poll() is not None:
                    raise RuntimeError(f"Nodo encerrou com código {proc.returncode}: {log_path.read_text(errors='replace')}")
                try:
                    conn = http.client.HTTPConnection("127.0.0.1", port, timeout=2)
                    conn.request("GET", "/api/_saude")
                    response = conn.getresponse()
                    payload = response.read()
                    conn.close()
                    if response.status == 200 and json.loads(payload) == {"ok": True}:
                        healthy = True
                        break
                except Exception:
                    pass
                time.sleep(0.2)
            if not healthy:
                raise RuntimeError(f"Servidor não ficou saudável: {log_path.read_text(errors='replace')}")

            body = json.dumps({"nome": "Produto smoke test", "preco": 12.34})
            conn = http.client.HTTPConnection("127.0.0.1", port, timeout=4)
            conn.request("POST", "/api/produtos", body,
                         {"Content-Type": "application/json", "X-Nodo-Key": key})
            response = conn.getresponse()
            created = json.loads(response.read())
            conn.close()
            if response.status != 201 or created.get("nome") != "Produto smoke test":
                raise RuntimeError(f"Gravação no banco falhou: HTTP {response.status}, {created}")
            print("[OK] integrar cria a configuração")
            print("[OK] servidor inicia e /api/_saude responde")
            print("[OK] banco grava um documento autenticado")
        finally:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait(timeout=3)
print("RESULTADO: smoke test da plataforma aprovado.")
