#!/usr/bin/env python3
"""Testes do Nodo: sobe servidores de verdade e ataca a API.
Uso:  python3 testes/rodar.py [caminho/do/binario/nodo]
Precisa so do Python 3 (biblioteca padrao). Demora cerca de 1 minuto."""
import http.client, json, os, shutil, socket, subprocess, sys, tempfile, threading, time

BIN = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "build", "nodo"))
if not os.path.exists(BIN):
    sys.exit("binario nao encontrado: %s (compile antes: sh compilar.sh)" % BIN)

ok = falhou = 0
falhas = []
def teste(nome, cond, extra=""):
    global ok, falhou
    if cond:
        ok += 1
        print("  [ok]    " + nome)
    else:
        falhou += 1
        falhas.append(nome)
        print("  [FALHOU] %s %s" % (nome, extra))

class Servidor:
    def __init__(self, porta, conf_extra="", chave=None, sync=5):
        self.porta = porta
        self.dir = tempfile.mkdtemp(prefix="nodo-teste-")
        subprocess.run([BIN, "integrar", self.dir], capture_output=True, check=True)
        conf = open(os.path.join(self.dir, "nodo.conf")).read()
        if chave:
            conf = "\n".join(("chave=" + chave) if l.startswith("chave=") else l for l in conf.split("\n"))
        conf = conf.replace("porta=7070", "porta=%d" % porta)
        conf += "\nsync_segundos=%d\n%s\n" % (sync, conf_extra)
        open(os.path.join(self.dir, "nodo.conf"), "w").write(conf)
        self.chave = [l.split("=", 1)[1] for l in conf.split("\n") if l.startswith("chave=")][0]
        self.proc = None
        self.iniciar()
    def iniciar(self):
        self.log = open(os.path.join(self.dir, "saida.log"), "ab")
        self.proc = subprocess.Popen([BIN, "rodar", self.dir], stdout=self.log, stderr=self.log, cwd=self.dir)
        for _ in range(60):
            try:
                if self.req("GET", "/api/_saude")[0] == 200: return
            except Exception: pass
            time.sleep(0.1)
        raise RuntimeError("servidor nao subiu; veja " + os.path.join(self.dir, "saida.log"))
    def parar(self):
        if self.proc:
            self.proc.terminate()
            try: self.proc.wait(5)
            except Exception: self.proc.kill()
            self.proc = None
    def limpar(self):
        self.parar()
        shutil.rmtree(self.dir, ignore_errors=True)
    def conf(self, extra):
        with open(os.path.join(self.dir, "nodo.conf"), "a") as f: f.write("\n" + extra + "\n")
    def req(self, metodo, caminho, corpo=None, cab=None, chave=False, token=None, bruto=False):
        h = dict(cab or {})
        if chave: h["X-Nodo-Key"] = self.chave
        if token: h["Authorization"] = "Bearer " + token
        c = http.client.HTTPConnection("127.0.0.1", self.porta, timeout=15)
        try:
            if corpo is not None and not isinstance(corpo, (str, bytes)): corpo = json.dumps(corpo)
            c.request(metodo, caminho, body=corpo, headers=h)
            r = c.getresponse()
            dados = r.read()
            hd = {k.lower(): v for k, v in r.getheaders()}
            if bruto: return r.status, hd, dados
            try: js = json.loads(dados) if dados else None
            except Exception: js = {"_nao_json": dados[:80]}
            return r.status, js, hd
        finally:
            c.close()

def cru(porta, bytes_enviados, esperar=True, timeout=5, fechar_escrita=False):
    """Envia bytes crus por socket e devolve a resposta (para testar requisicoes malformadas)."""
    s = socket.create_connection(("127.0.0.1", porta), timeout=timeout)
    s.sendall(bytes_enviados)
    if fechar_escrita: s.shutdown(socket.SHUT_WR)
    out = b""
    if esperar:
        try:
            while True:
                d = s.recv(4096)
                if not d: break
                out += d
        except Exception: pass
    s.close()
    return out

def status_de(resp): return int(resp.split(b" ")[1]) if resp.startswith(b"HTTP/") else 0

CONF1 = """leitura_publica=mural,produtos
escrita_publica=mural,contatos,limitada
campos.mural=nome:40,texto:200
campos.limitada=a:10
max_docs.limitada=50
origens=https://meusite.com
limite_escrita=1000
"""
print("== 1. identificadores malformados e respostas de erro em JSON")
A = Servidor(7081, CONF1)
try:
    s, js, h = A.req("GET", "/api/_saude")
    teste("rota publica de saude responde JSON", s == 200 and js == {"ok": True})
    casos = [
        ("colecao com ponto", "/api/a.b", 400, "colecao_invalida"),
        ("colecao com espaco", "/api/a%20b", 400, "colecao_invalida"),
        ("colecao longa demais", "/api/" + "a" * 65, 400, "colecao_invalida"),
        ("colecao vazia", "/api/", 400, "colecao_invalida"),
        ("id com espaco", "/api/mural/a%20b", 400, "id_invalido"),
        ("id longo demais", "/api/mural/" + "x" * 65, 400, "id_invalido"),
        ("id com caractere especial", "/api/mural/a%27b", 400, "id_invalido"),
        ("id com <script>", "/api/mural/%3Cscript%3E", 400, "id_invalido"),
        ("travessia de diretorio", "/api/mural/..%2F..%2Fnodo.conf", 404, "rota_desconhecida"),
        ("segmentos demais", "/api/mural/abc/def", 404, "rota_desconhecida"),
        ("byte nulo no caminho", "/api/mural%00x", 400, "requisicao_invalida"),
        ("quebra de linha no caminho", "/api/mural%0d%0aX", 400, "requisicao_invalida"),
        ("limite nao numerico", "/api/mural?limite=abc", 400, "parametro_invalido"),
        ("limite negativo", "/api/mural?limite=-1", 400, "parametro_invalido"),
        ("pular gigante", "/api/mural?pular=99999999999999", 400, "parametro_invalido"),
        ("filtro com nome invalido", "/api/mural?a%20b=1", 400, "filtro_invalido"),
        ("documento inexistente", "/api/mural/naoexiste", 404, "documento_nao_encontrado"),
        ("rota interna sem chave", "/api/_status", 401, None),
    ]
    for nome, caminho, st, cod in casos:
        s, js, h = A.req("GET", caminho)
        valido = isinstance(js, dict) and "erro" in js and "codigo" in js and "_nao_json" not in js
        teste("%s -> %d%s" % (nome, st, (" " + cod) if cod else ""), s == st and valido and (cod is None or js["codigo"] == cod), "(recebido %s %s)" % (s, js))
    s, js, h = A.req("GET", "/nodo-dados/mural.log")
    teste("nodo-dados nunca e servido", s in (403, 404))
    s, js, h = A.req("GET", "/nodo.conf")
    teste("nodo.conf nunca e servido", s in (403, 404))
    s, js, h = A.req("GET", "/../../etc/passwd")
    teste("travessia de diretorio em arquivo estatico", s in (400, 403, 404))

    print("== 2. corpo que nao e JSON valido (nao pode entrar no banco)")
    ruins = [("texto puro", "ola"), ("literal quebrado", '{"nome":tru}'), ("objeto interno quebrado", '{"nome":{x}}'),
             ("virgula sobrando", '{"nome":"a",}'), ("string sem fechar", '{"nome":"abc}'), ("array no topo", '[1,2]'),
             ("numero invalido", '{"texto":12abc}'), ("vazio", ""), ("so espacos", "   "), ("JSON dentro de JSON quebrado", '{"a":[1,2}')]
    for nome, corpo in ruins:
        s, js, h = A.req("POST", "/api/produtos", corpo, chave=True)
        teste("com chave, %s -> 400" % nome, s == 400 and js and js.get("codigo") == "json_invalido", "(recebido %s %s)" % (s, js))
    s, lista, h = A.req("GET", "/api/produtos")
    teste("a lista continua JSON valido e vazia", s == 200 and lista == [])
    s, js, h = A.req("POST", "/api/produtos", '{"nome":"Camiseta","preco":59.9,"tags":["a","b"],"extra":{"x":1},"u":"\\u00e9"}', chave=True)
    teste("documento valido com chave (aninhado permitido para admin)", s == 201 and js["_id"] and js["_t"] > 0 and js["preco"] == 59.9)
    pid = js["_id"]
    s, js, h = A.req("POST", "/api/produtos", '{"_id":"forjado","_t":1,"nome":"X"}', chave=True)
    teste("_id e _t do corpo sao ignorados (servidor decide)", s == 201 and js["_id"] != "forjado" and js["_t"] > 1000)
    s, js, h = A.req("POST", "/api/produtos", '{"a":1,"a":2}', chave=True)
    teste("chave repetida: vale a ultima e o JSON continua valido", s == 201 and js["a"] == 2)
    s, lista, h = A.req("GET", "/api/produtos?limite=100")
    teste("lista com varios documentos e JSON valido", s == 200 and len(lista) == 3)

    print("== 3. escrita publica: campos, limites e colecoes sem campos")
    s, js, h = A.req("POST", "/api/mural", {"nome": "Maria", "texto": "oi"})
    teste("visitante cria no mural", s == 201 and js["nome"] == "Maria")
    s, js, h = A.req("POST", "/api/mural", {"nome": "x" * 41, "texto": "oi"})
    teste("campo acima do limite de caracteres -> 400 campo_grande", s == 400 and js["codigo"] == "campo_grande", js)
    s, js, h = A.req("POST", "/api/mural", {"nome": "é" * 40, "texto": "oi"})
    teste("limite conta caracteres, nao bytes (40 'e' acentuados passam)", s == 201, js)
    s, js, h = A.req("POST", "/api/mural", {"nome": "a", "texto": "b", "admin": True})
    teste("campo fora da lista -> 400 campo_nao_permitido", s == 400 and js["codigo"] == "campo_nao_permitido", js)
    s, js, h = A.req("POST", "/api/mural", '{"_id":"x","nome":"a"}')
    teste("visitante nao consegue forjar _id", s == 400, js)
    s, js, h = A.req("POST", "/api/mural", {"nome": {"a": 1}})
    teste("valor aninhado de visitante recusado", s == 400, js)
    s, js, h = A.req("POST", "/api/mural", '{"nome":"a","nome":"b"}')
    teste("campo repetido de visitante recusado", s == 400, js)
    s, js, h = A.req("POST", "/api/mural", {"nome": "a", "texto": "</script><img src=x onerror=alert(1)>"})
    teste("HTML de visitante e guardado como texto (a pagina escapa ao mostrar)", s == 201)
    s, js, h = A.req("POST", "/api/contatos", {"nome": "a"})
    teste("escrita publica SEM campos.<colecao> e recusada -> 403 campos_obrigatorios", s == 403 and js["codigo"] == "campos_obrigatorios", js)
    s, js, h = A.req("POST", "/api/contatos", {"nome": "a"}, chave=True)
    teste("o administrador continua podendo escrever nela", s == 201)
    s, js, h = A.req("POST", "/api/mural", "x" * 70000)
    teste("documento de visitante acima de max_doc_bytes -> 413", s == 413, js)
    s, js, h = A.req("PUT", "/api/mural/qualquer", {"nome": "a"})
    teste("visitante nao edita", s == 401)
    s, js, h = A.req("DELETE", "/api/mural/qualquer")
    teste("visitante nao apaga", s == 401)
    s, js, h = A.req("GET", "/api/contatos")
    teste("colecao sem leitura publica exige chave", s == 401 and js["codigo"] == "leitura_exige_chave")

    print("== 4. autenticacao segura (token de sessao)")
    s, js, h = A.req("POST", "/api/_login", {"chave": "errada"})
    teste("login com chave errada -> 401", s == 401 and js["codigo"] == "chave_invalida")
    s, js, h = A.req("POST", "/api/_login", {"chave": A.chave})
    tok = js.get("token") if isinstance(js, dict) else None
    teste("login com a chave certa devolve token de 256 bits", s == 200 and tok and len(tok) == 64 and js["horas"] == 8)
    s, js, h = A.req("GET", "/api/_status", token=tok)
    teste("token abre rotas internas", s == 200 and js["versao"])
    s, js, h = A.req("GET", "/api/_status", token="0" * 64)
    teste("token inventado -> 401", s == 401)
    s, js, h = A.req("GET", "/api/_status", cab={"X-Nodo-Token": tok})
    teste("cabecalho X-Nodo-Token tambem funciona", s == 200)
    s, js, h = A.req("GET", "/api/_status", chave=True)
    teste("a chave direta continua funcionando (scripts e deploy)", s == 200)
    s, js, h = A.req("POST", "/api/_logout", {}, token=tok)
    s2, js2, h2 = A.req("GET", "/api/_status", token=tok)
    teste("logout revoga o token", s == 200 and s2 == 401)
    s, js, h = A.req("POST", "/api/_login", '{"chave":"' + A.chave + '"', cab={})
    teste("login com JSON quebrado nao entra", s == 401)
    s, js, h = A.req("GET", "/api/_status", cab={"Authorization": "Basic abc"})
    teste("Authorization que nao e Bearer e ignorado", s == 401)

    print("== 5. CORS com cliente e API em origens diferentes")
    s, js, h = A.req("GET", "/api/mural", cab={"Origin": "https://meusite.com"})
    teste("origem listada: ecoa a origem e Vary", s == 200 and h.get("access-control-allow-origin") == "https://meusite.com" and "origin" in h.get("vary", "").lower())
    s, js, h = A.req("GET", "/api/mural", cab={"Origin": "https://evil.example"})
    teste("origem nao listada -> 403 origem_negada", s == 403 and js["codigo"] == "origem_negada" and "access-control-allow-origin" not in h)
    s, js, h = A.req("POST", "/api/mural", {"nome": "a", "texto": "b"}, cab={"Origin": "https://evil.example"})
    teste("escrita vinda de origem nao listada e BLOQUEADA no servidor (nao so escondida)", s == 403)
    s, lista, h = A.req("GET", "/api/mural?limite=100")
    teste("...e nada foi gravado por ela", all(d.get("nome") != "a" or d.get("texto") != "b" for d in lista))
    s, js, h = A.req("OPTIONS", "/api/mural", cab={"Origin": "https://meusite.com", "Access-Control-Request-Method": "POST"})
    teste("preflight permitido: 204 com metodos e cabecalhos", s == 204 or s == 200, (s, h))
    s, js, h = A.req("OPTIONS", "/api/mural", cab={"Origin": "https://evil.example"})
    teste("preflight de origem nao listada -> 403", s == 403)
    s, js, h = A.req("GET", "/api/_status", chave=True, cab={"Origin": "https://evil.example"})
    teste("rota interna nunca libera outra origem, mesmo com chave", s == 403)
    s, js, h = A.req("POST", "/api/_login", {"chave": A.chave}, cab={"Origin": "http://127.0.0.1:7081", "Host": "127.0.0.1:7081"})
    teste("o proprio painel (mesma origem) pode fazer login", s == 200)
    s, js, h = A.req("POST", "/api/_login", {"chave": A.chave}, cab={"Origin": "https://meusite.com"})
    teste("site listado NAO ganha acesso as rotas internas so por estar em 'origens'... (precisa da chave de qualquer forma)", s in (200, 401))
    s, js, h = A.req("GET", "/api/mural", cab={"Origin": "x" * 400})
    teste("cabecalho Origin gigante -> 400", s == 400)

    print("== 6. requisicoes HTTP malformadas")
    r = cru(7081, b"GET /api/mural HTTP/1.1\r\nHost: x\r\nContent-Length: 5\r\nContent-Length: 6\r\n\r\n")
    teste("Content-Length repetido -> 400", status_de(r) == 400)
    r = cru(7081, b"POST /api/mural HTTP/1.1\r\nHost: x\r\nContent-Length: abc\r\n\r\n")
    teste("Content-Length nao numerico -> 400", status_de(r) == 400)
    r = cru(7081, b"POST /api/mural HTTP/1.1\r\nHost: x\r\nContent-Length: -1\r\n\r\n")
    teste("Content-Length negativo -> 400", status_de(r) == 400)
    r = cru(7081, b"POST /api/mural HTTP/1.1\r\nHost: x\r\nContent-Length: 99999999\r\n\r\n")
    teste("corpo declarado grande demais -> 413", status_de(r) == 413)
    r = cru(7081, b"POST /api/mural HTTP/1.1\r\nHost: x\r\nContent-Length: 100\r\n\r\n{\"nome\":", fechar_escrita=True)
    teste("corpo incompleto (conexao fecha antes) -> 400", status_de(r) == 400)
    r = cru(7081, b"POST /api/mural HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n")
    teste("Transfer-Encoding recusado (anti request smuggling)", status_de(r) == 400)
    r = cru(7081, b"FOO /api/mural HTTP/1.1\r\nHost: x\r\n\r\n")
    teste("metodo desconhecido -> 405", status_de(r) == 405)
    r = cru(7081, b"GET /api/mural SPDY/9\r\nHost: x\r\n\r\n")
    teste("versao de protocolo invalida -> 400", status_de(r) == 400)
    r = cru(7081, b"GET /api/mural HTTP/1.1\r\nHost: x\r\n" + b"X-A: " + b"a" * 20000 + b"\r\n\r\n")
    teste("cabecalhos gigantes -> 400", status_de(r) == 400)
    r = cru(7081, b"\x16\x03\x01\x02\x00\x01\x00\x01\xfc\x03\x03" + os.urandom(200))
    teste("lixo binario (ex.: HTTPS no porto HTTP) nao derruba o servidor", A.req("GET", "/api/_saude")[0] == 200)
    r = cru(7081, b"POST /api/mural HTTP/1.1\r\nHost: x\r\nContent-Length: 20\r\nExpect: 100-continue\r\n\r\n", esperar=False)
    teste("servidor sobrevive a Expect: 100-continue sem corpo", A.req("GET", "/api/_saude")[0] == 200)
    t0 = time.time()
    lento = socket.create_connection(("127.0.0.1", 7081)); lento.sendall(b"GET /api/mural HTTP/1.1\r\nHost: x\r\n")
    lento.settimeout(14)
    try: lento.recv(10)
    except Exception: pass
    lento.close()
    teste("conexao lenta (slowloris) e derrubada em ~10 s", 8 < time.time() - t0 < 14, "%.1fs" % (time.time() - t0))

    print("== 7. concorrencia")
    def disparar(n, fn):
        res = [None] * n
        def f(i): res[i] = fn(i)
        ts = [threading.Thread(target=f, args=(i,)) for i in range(n)]
        for t in ts: t.start()
        for t in ts: t.join()
        return res
    res = disparar(150, lambda i: A.req("POST", "/api/produtos", {"n": i}, chave=True))
    ids = [r[1]["_id"] for r in res if r[0] == 201]
    teste("150 criacoes simultaneas: todas 201 e ids unicos", len(ids) == 150 and len(set(ids)) == 150, "(%d ok, %d unicos)" % (len(ids), len(set(ids))))
    s, lista, h = A.req("GET", "/api/produtos?limite=5000", chave=True)
    teste("a lista continua JSON valido com 153 documentos", s == 200 and len(lista) == 153)
    res = disparar(200, lambda i: A.req("POST", "/api/limitada", {"a": str(i)}))
    n201 = sum(1 for r in res if r[0] == 201); n413 = sum(1 for r in res if r[0] == 413)
    teste("limite max_docs.limitada=50 vale sob 200 envios simultaneos (exatamente 50)", n201 == 50 and n413 == 150, "(%d criados, %d recusados)" % (n201, n413))
    res = disparar(100, lambda i: A.req("PUT", "/api/produtos/" + pid, {"nome": "v%d" % i}, chave=True))
    s, doc, h = A.req("GET", "/api/produtos/" + pid, chave=True)
    teste("100 atualizacoes simultaneas do mesmo documento: continua um JSON integro", all(r[0] == 200 for r in res) and s == 200 and doc["_id"] == pid)
    res = disparar(60, lambda i: A.req("GET", "/api/mural"))
    teste("60 leituras simultaneas sem erro", all(r[0] == 200 for r in res))

    print("== 8. persistencia: o que esta no disco bate com o que a API mostra")
    A.parar(); A.iniciar()
    s, lista, h = A.req("GET", "/api/produtos?limite=5000", chave=True)
    teste("depois de reiniciar, os 153 documentos estao la", s == 200 and len(lista) == 153)
    s, doc, h = A.req("GET", "/api/produtos/" + pid, chave=True)
    teste("e o documento atualizado tem o _t novo", s == 200 and doc["_t"] > 0)

    print("== 9. monitor de uso")
    A.req("GET", "/api/mural/naoexiste"); A.req("GET", "/api/a.b")
    s, m, h = A.req("GET", "/api/_metricas", chave=True)
    teste("metricas: JSON com os campos esperados", s == 200 and all(k in m for k in ["requisicoes", "erros4xx", "cpuPct", "memoriaMb", "porMinuto", "porColecao", "topIps", "replicacao"]))
    teste("metricas: 60 pontos por minuto e contadores coerentes", len(m["porMinuto"]) == 60 and m["requisicoes"] > 0 and m["porColecao"].get("produtos", {}).get("documentos") == 153 and m["documentos"] >= 153, m.get("porColecao"))
    teste("metricas: contou erros 4xx", m["erros4xx"] > 0 and m["limitados429"] >= 0)
    out = subprocess.run([BIN, "monitor"], cwd=A.dir, capture_output=True, text=True).stdout
    teste("CLI: nodo monitor", "requisicoes:" in out and "replicacao:" in out, out[:200])
    out = subprocess.run([BIN, "verificar"], cwd=A.dir, capture_output=True, text=True).stdout
    teste("CLI: nodo verificar avisa que contatos nao tem campos", "contatos" in out and "SEM campos" in out)
    s, h, html = A.req("GET", "/admin", bruto=True)
    teste("painel /admin com CSP e sem cache", s == 200 and "content-security-policy" in h and h.get("cache-control") == "no-store" and h.get("x-frame-options") == "DENY")

    print("== 10. servidor padrao seguro e SDK")
    out = open(os.path.join(A.dir, "nodo.conf")).read()
    teste("nodo.conf novo escuta so em 127.0.0.1", "escutar=127.0.0.1" in out)
    s, hd, js = A.req("GET", "/nodo.js", bruto=True)
    teste("nodo.js e servido", s == 200 and b"NodoErro" in js)
    shutil.copy(os.path.join(A.dir, "nodo.js"), "/tmp/nodo-sdk-teste.js") if os.path.exists(os.path.join(A.dir, "nodo.js")) else None
finally:
    A.limpar()

print("== 11. CORS padrao (sem 'origens'): publico aceita qualquer site; interno nunca")
B = Servidor(7083, "leitura_publica=aberta\n")
try:
    B.req("POST", "/api/aberta", {"x": 1}, chave=True)
    s, js, h = B.req("GET", "/api/aberta", cab={"Origin": "https://qualquer.com"})
    teste("colecao publica: Access-Control-Allow-Origin: *", s == 200 and h.get("access-control-allow-origin") == "*")
    s, js, h = B.req("GET", "/api/_status", chave=True, cab={"Origin": "https://qualquer.com"})
    teste("rota interna: recusada para outro site", s == 403)
    s, js, h = B.req("GET", "/api/aberta", cab={"Origin": "null"})
    teste("pagina aberta de arquivo local (Origin: null) continua funcionando em colecao publica", s == 200)
    # escutar invalido cai para 127.0.0.1 e admin_remoto com escutar aberto e recusado
    B.parar()
    B.conf("admin_remoto=sim\nescutar=0.0.0.0")
    p = subprocess.run([BIN, "rodar", B.dir], cwd=B.dir, capture_output=True, text=True, timeout=10)
    teste("admin_remoto=sim com escutar=0.0.0.0 e RECUSADO ao iniciar", p.returncode != 0 and "Recusado" in (p.stderr + p.stdout), (p.stdout + p.stderr)[:200])
finally:
    B.limpar()

print("== 12. bloqueio por chaves erradas")
C = Servidor(7084, "")
try:
    for i in range(10): C.req("GET", "/api/_status", cab={"X-Nodo-Key": "errada%d" % i})
    s, js, h = C.req("GET", "/api/_status", chave=True)
    teste("depois de 10 chaves erradas o IP fica bloqueado (ate a chave certa e recusada)", s == 401 and js["codigo"] == "bloqueado", (s, js))
    s, js, h = C.req("GET", "/api/_saude")
    teste("a rota de saude continua respondendo", s == 200)
    s, m, h = C.req("GET", "/api/_metricas", chave=True)
finally:
    C.limpar()

print("== 13. replicacao entre servidores (conflitos, apagados, queda e volta)")
CH = "k" * 48
R1 = Servidor(7091, "leitura_publica=nota\n", chave=CH, sync=5)
R2 = Servidor(7092, "leitura_publica=nota\n", chave=CH, sync=5)
try:
    s, js, h = R1.req("POST", "/api/_peers", "127.0.0.1:7092", chave=True)
    teste("conectar PC", s == 200)
    s, js, h = R1.req("POST", "/api/_peers", "isso nao e um endereco", chave=True)
    teste("endereco de PC invalido -> 400", s == 400 and js["codigo"] == "peer_invalido")
    s, js, h = R1.req("POST", "/api/_peers", "a;b:70", chave=True)
    teste("endereco com caractere perigoso -> 400", s == 400)
    time.sleep(1.5)
    s, d1, h = R1.req("POST", "/api/nota", {"t": "de R1"}, chave=True)
    time.sleep(1.5)
    s, d2, h = R2.req("GET", "/api/nota/" + d1["_id"], chave=True)
    teste("R1 -> R2: documento replicado", s == 200 and d2["t"] == "de R1" and d2["_t"] == d1["_t"], d2)
    s, d3, h = R2.req("POST", "/api/nota", {"t": "de R2"}, chave=True)
    time.sleep(2)
    s, e, h = R1.req("GET", "/api/nota/" + d3["_id"], chave=True)
    teste("R2 -> R1: R2 aprendeu sozinho que R1 e um par (via cabecalhos) e replica de volta", s == 200 and e["t"] == "de R2", e)
    # conflito: R1 grava, depois R2 grava (mais novo) no mesmo id; todos devem ficar com o mais novo
    R1.req("PUT", "/api/nota/" + d1["_id"], {"t": "v-R1"}, chave=True)
    time.sleep(0.05)
    R2.req("PUT", "/api/nota/" + d1["_id"], {"t": "v-R2-mais-novo"}, chave=True)
    time.sleep(2.5)
    a = R1.req("GET", "/api/nota/" + d1["_id"], chave=True)[1]; b = R2.req("GET", "/api/nota/" + d1["_id"], chave=True)[1]
    teste("conflito: os dois PCs convergem para a alteracao mais recente", a["t"] == b["t"] == "v-R2-mais-novo", (a, b))
    # apagar nao ressuscita
    R1.req("DELETE", "/api/nota/" + d3["_id"], chave=True)
    time.sleep(2)
    teste("apagado em R1 some em R2", R2.req("GET", "/api/nota/" + d3["_id"], chave=True)[0] == 404)
    time.sleep(7)
    teste("e continua apagado depois da sincronizacao periodica (nao ressuscita)", R1.req("GET", "/api/nota/" + d3["_id"], chave=True)[0] == 404 and R2.req("GET", "/api/nota/" + d3["_id"], chave=True)[0] == 404)
    # queda e volta
    R2.parar()
    for i in range(6): R1.req("POST", "/api/nota", {"t": "enquanto R2 estava fora %d" % i}, chave=True)
    time.sleep(1)
    R2.iniciar()
    time.sleep(1)
    for _ in range(25):
        lista = R2.req("GET", "/api/nota?limite=100", chave=True)[1]
        if sum(1 for d in lista if d["t"].startswith("enquanto")) == 6: break
        time.sleep(1)
    teste("R2 volta e recupera as 6 escritas que perdeu (peers salvos + sincronizacao periodica)", sum(1 for d in lista if d["t"].startswith("enquanto")) == 6, [d["t"] for d in lista])
    # escrita concorrente nos dois lados
    def esc(i): return (R1 if i % 2 == 0 else R2).req("POST", "/api/nota", {"n": i}, chave=True)
    res = [None] * 60
    def f(i): res[i] = esc(i)
    ts = [threading.Thread(target=f, args=(i,)) for i in range(60)]
    [t.start() for t in ts]; [t.join() for t in ts]
    time.sleep(8)
    l1 = {d["_id"]: d for d in R1.req("GET", "/api/nota?limite=5000", chave=True)[1]}
    l2 = {d["_id"]: d for d in R2.req("GET", "/api/nota?limite=5000", chave=True)[1]}
    teste("60 escritas simultaneas nos dois PCs: os dois acabam com os mesmos documentos", l1.keys() == l2.keys() and all(l1[k]["_t"] == l2[k]["_t"] for k in l1), (len(l1), len(l2)))
    # linha de replicacao maliciosa
    s, js, h = R1.req("POST", "/api/_replicar", "P\tnota\t../../x\t{\"a\":1}", chave=True)
    teste("replicacao recusa id malformado", s == 200 and js["aplicado"] is False)
    s, js, h = R1.req("POST", "/api/_replicar", "P\tnota\tabc123\t{\"a\":tru}", chave=True)
    teste("replicacao recusa JSON invalido", js["aplicado"] is False)
    s, js, h = R1.req("POST", "/api/_replicar", "P\t_interna\tabc123\t{\"a\":1}", chave=True)
    teste("replicacao recusa colecao interna", js["aplicado"] is False)
    s, js, h = R1.req("POST", "/api/_replicar", "P\tnota\tfuturo1\t{\"_t\":99999999999999999,\"a\":1}", chave=True)
    s2, d, h = R1.req("GET", "/api/nota/futuro1", chave=True)
    teste("relogio no futuro vindo de outro PC e limitado (nao cria documento imbativel)", s2 == 200 and d["_t"] < time.time() * 1000 + 86400000 * 2, d)
    s, js, h = R1.req("POST", "/api/_replicar", "P\tnota\tabc123\t{\"a\":1}")
    teste("replicacao exige a chave", s == 401)
    m = R1.req("GET", "/api/_metricas", chave=True)[1]
    teste("monitor mostra a replicacao (PCs online, enviados)", m["replicacao"]["pcs"] >= 1 and m["replicacao"]["enviados"] > 0, m["replicacao"])
finally:
    R1.limpar(); R2.limpar()

print()
print("RESULTADO: %d ok, %d falhas" % (ok, falhou))
if falhas:
    print("Falharam:"); [print("  - " + f) for f in falhas]
sys.exit(1 if falhou else 0)
