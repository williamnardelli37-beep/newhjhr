#!/usr/bin/env python3
"""Testes de seguranca do servidor (so contra uma instancia local de teste). Uso: python3 testes/seguranca.py build/nodo"""
import os, socket, subprocess, sys, tempfile, time, shutil, json
BIN = os.path.abspath(sys.argv[1]); PORT = 17301
base = tempfile.mkdtemp(prefix="seg-"); site = os.path.join(base, "site"); os.makedirs(site)
open(os.path.join(base, "fora.txt"), "w").write("SEGREDO-FORA"); open(os.path.join(site, "index.html"), "w").write("ok")
os.symlink(os.path.join(base, "fora.txt"), os.path.join(site, "link.txt"))
subprocess.run([BIN, "integrar", site], capture_output=True, check=True)
conf = open(site + "/nodo.conf").read().replace("porta=7070", "porta=%d" % PORT) + "\nleitura_publica=mural\nescrita_publica=mural\ncampos.mural=nome:40,texto:500\norigens=https://meusite.com\n"
open(site + "/nodo.conf", "w").write(conf)
CH = [l.split("=", 1)[1] for l in conf.split("\n") if l.startswith("chave=")][0]
p = subprocess.Popen([BIN, "rodar", site], cwd=site, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
for _ in range(60):
    try: socket.create_connection(("127.0.0.1", PORT), 1).close(); break
    except OSError: time.sleep(.2)
ok = falhou = 0
def t(n, c, e=""):
    global ok, falhou
    if c: ok += 1; print("  [ok]    " + n)
    else: falhou += 1; print("  [FALHOU] %s %s" % (n, e))
def raw(d, to=5):
    s = socket.create_connection(("127.0.0.1", PORT), to); s.settimeout(to)
    try: s.sendall(d if isinstance(d, bytes) else d.encode("latin1"))
    except OSError: pass
    o = b""
    try:
        while True:
            x = s.recv(65536)
            if not x: break
            o += x
    except OSError: pass
    s.close(); return o
def st(r):
    try: return int(r.split(b" ", 2)[1])
    except Exception: return None
def get(path, host="localhost", extra=""): return raw("GET %s HTTP/1.1\r\nHost: %s\r\n%sConnection: close\r\n\r\n" % (path, host, extra))
print("== arquivos internos nunca saem")
for c in ["/nodo.conf", "/NODO.CONF", "/%6eodo.conf", "/./nodo.conf", "//nodo.conf", "/a/../nodo.conf", "/nodo-dados/", "/.git/config", "/%2e%2e/fora.txt", "/..%2ffora.txt", "/%252e%252e/fora.txt", "/link.txt", "/nodo.conf%00.html", "/nodo.regras"]:
    b = get(c).split(b"\r\n\r\n", 1)[-1]
    t("sem vazamento: " + c, CH.encode() not in b and b"SEGREDO-FORA" not in b)
print("== DNS rebinding / Host")
t("Origin estranha + Host localhost -> 403", st(get("/api/mural", extra="Origin: https://evil.example\r\n")) == 403)
t("Host estranho (rebinding) -> 421", st(get("/api/mural", host="evil.example:%d" % PORT, extra="Origin: http://evil.example:%d\r\n" % PORT)) == 421)
t("Host estranho na rota privada -> 421", st(get("/api/_status", host="evil.example")) == 421)
t("Host do proprio site (origens) -> aceito", st(get("/api/mural", host="meusite.com")) == 200)
t("Host IP e localhost -> aceitos", st(get("/api/mural", host="127.0.0.1:%d" % PORT)) == 200 and st(get("/api/mural", host="localhost")) == 200)
print("== cabecalhos")
t("dois X-Forwarded-For diferentes -> 400", st(get("/", extra="X-Forwarded-For: 1.1.1.1\r\nX-Forwarded-For: 192.168.1.5\r\n")) == 400)
t("LF solto no cabecalho -> 400", st(raw(b"GET / HTTP/1.1\r\nHost: localhost\nX-Forwarded-For: 192.168.1.9\r\n\r\n")) == 400)
t("Content-Length duplo diferente -> 400", st(raw(b"POST /api/mural HTTP/1.1\r\nHost: localhost\r\nContent-Length: 4\r\nContent-Length: 5\r\n\r\nabcde")) == 400)
t("Content-Length + Transfer-Encoding -> 400", st(raw(b"POST /api/mural HTTP/1.1\r\nHost: localhost\r\nContent-Length: 4\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n")) == 400)
t("byte nulo no cabecalho -> 400", st(raw(b"GET / HTTP/1.1\r\nHost: localhost\r\nX-A: a\x00b\r\n\r\n")) == 400)
print("== robustez")
for prof in (1000, 20000, 200000):
    corpo = '{"nome":"a","texto":' + "[" * prof + "]" * prof + "}"
    raw("POST /api/mural HTTP/1.1\r\nHost: localhost\r\nContent-Type: application/json\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s" % (len(corpo), corpo), 10)
    t("JSON aninhado %d niveis: servidor continua vivo" % prof, st(get("/api/_saude")) == 200)
for i in range(11): get("/api/_status", extra="X-Nodo-Key: errada%d\r\n" % i)
t("chave certa depois de varias erradas continua bloqueada (anti forca bruta)", st(get("/api/_status", extra="X-Nodo-Key: %s\r\n" % CH)) == 401)
p.terminate(); p.wait(5); shutil.rmtree(base, ignore_errors=True)
print("RESULTADO SEGURANCA: %d ok, %d falhas" % (ok, falhou)); sys.exit(1 if falhou else 0)
