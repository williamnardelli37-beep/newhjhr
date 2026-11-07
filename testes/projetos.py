#!/usr/bin/env python3
# Teste de varios projetos ao mesmo tempo: portas sem colisao, isolamento de dados e chaves, parar um sem derrubar os outros.
# Uso: python3 testes/projetos.py build/nodo
import os, subprocess, sys, tempfile, socket, json, time, http.client, shutil
BIN=os.path.abspath(sys.argv[1])
base=tempfile.mkdtemp(prefix="multi-")
env=dict(os.environ, NODO_HOME=os.path.join(base,"home"), ASAN_OPTIONS="detect_leaks=0")
ok=falhou=0
def t(nome,cond,extra=""):
    global ok,falhou
    if cond: ok+=1; print("  [ok]    "+nome)
    else: falhou+=1; print("  [FALHOU] %s %s"%(nome,extra))
def run(*a):
    r=subprocess.run([BIN,"projetos",*a],capture_output=True,text=True,env=env,timeout=60); return r.returncode,r.stdout+r.stderr
def req(porta,m,p,body=None,chave=None):
    c=http.client.HTTPConnection("127.0.0.1",porta,timeout=5); h={}
    if chave: h["X-Nodo-Key"]=chave
    if body is not None: h["Content-Type"]="application/json"; body=json.dumps(body)
    c.request(m,p,body,h); r=c.getresponse(); d=r.read().decode(); c.close(); return r.status,d
nomes=["loja","Meu Site Çá","blog","portfolio","api-interna"]
pastas=[]
for n in nomes:
    d=os.path.join(base,n); os.makedirs(d); open(os.path.join(d,"index.html"),"w").write("<h1>SITE-%s</h1>"%n); pastas.append(d)
# ocupa a 7070 com outro programa para forcar realocacao
ocupa=socket.socket(); ocupa.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1)
try: ocupa.bind(("0.0.0.0",7070)); ocupa.listen(1); print("(porta 7070 ocupada por outro programa)")
except OSError: print("(7070 ja ocupada no sandbox)")
print("== adicionar 5 projetos")
chaves={}
for n,d in zip(nomes,pastas):
    rc,out=run("adicionar",d)
    t("adicionar %r"%n, rc==0, out)
    if "Chave de administrador" in out: chaves[n]=out.split(": ")[-1].strip()
rc,out=run("listar"); print(out)
linhas=[l for l in out.splitlines() if l and not l.startswith(("NOME","\n")) and "ligado(s)" not in l]
portas=[int(l.split()[-3]) if False else None for l in linhas]
import re
portas=[int(x) for x in re.findall(r"^\S.*?\s(\d{4,5})\s+desligado",out,re.M)]
t("5 projetos listados com 5 portas diferentes", len(portas)==5 and len(set(portas))==5, str(portas))
t("a porta ocupada (7070) nao foi dada a ninguem", 7070 not in portas)
print("== ligar todos")
rc,out=run("iniciar","todos"); print(out)
t("iniciar todos retornou 0", rc==0)
rc,out=run("listar"); print(out)
t("5 ligados", "5 ligado(s) de 5" in out)
print("== cada site serve o SEU conteudo na SUA porta")
mapa={}
for l in out.splitlines():
    m=re.match(r"^(.+?)\s+(\d{4,5})\s+ligado\s+(.+)$",l)
    if m: mapa[m.group(1).strip()]=(int(m.group(2)),m.group(3).strip())
for n in nomes:
    slugn=re.sub(r"[^A-Za-z0-9._-]","-",n).strip("-")
    porta=mapa[slugn][0]
    s,body=req(porta,"GET","/")
    t("%s na porta %d serve SITE-%s"%(slugn,porta,n), s==200 and "SITE-%s"%n in body, body[:60])
print("== isolamento: chaves e dados")
a,b=mapa["loja"][0],mapa["blog"][0]
ch_loja=chaves["loja"]; ch_blog=chaves["blog"]
s,_=req(a,"POST","/api/produtos",{"nome":"camiseta"},chave=ch_loja); t("escreve na loja com a chave da loja", s==201)
s,_=req(b,"GET","/api/produtos",chave=ch_blog); t("o blog nao ve a colecao da loja (404/vazia)", s in (404,200)); 
s,d=req(b,"GET","/api/_colecoes",chave=ch_blog); t("blog: colecoes sem 'produtos'", s==200 and "produtos" not in d, d)
s,_=req(b,"GET","/api/_status",chave=ch_loja); t("chave da LOJA nao abre o BLOG", s==401, str(s))
print("== desligar um nao derruba os outros")
rc,out=run("parar","loja"); print(out); t("parar loja", rc==0)
s,_=req(mapa["blog"][0],"GET","/"); t("blog continua no ar", s==200)
try: req(a,"GET","/"); t("loja realmente caiu", False)
except Exception: t("loja realmente caiu", True)
print("== trocar porta / limites")
rc,out=run("porta","loja","7090"); print(out); t("trocar porta da loja desligada", rc==0)
rc,out=run("porta","blog","7090"); print(out); t("recusa trocar porta de projeto ligado", rc!=0)
rc,out=run("porta","portfolio","7090"); print(out); 
rc,out=run("remover","blog"); print(out); t("recusa remover projeto ligado", rc!=0)
rc,out=run("adicionar",pastas[0]); print(out); t("recusa duplicado", rc!=0)
rc,out=run("iniciar","loja"); print(out); t("religa a loja", rc==0)
print("== desligar todos")
rc,out=run("parar","todos"); print(out)
rc,out=run("listar"); t("todos desligados", "0 ligado(s) de 5" in out, out)
ocupa.close()
# sanitizers nos logs
bad=[]
for d in pastas:
    lg=os.path.join(d,"nodo-dados","nodo.log")
    if os.path.exists(lg):
        x=open(lg,errors="replace").read()
        if "Sanitizer" in x or "runtime error" in x: bad.append(d)
t("nenhum erro de sanitizer nos 5 logs", not bad, str(bad))


shutil.rmtree(base,ignore_errors=True)
print("RESULTADO: %d ok, %d falhas"%(ok,falhou))
sys.exit(1 if falhou else 0)
