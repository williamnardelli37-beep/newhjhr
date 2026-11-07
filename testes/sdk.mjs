// Testa o nodo.js (SDK do site) no Node 18+: node testes/sdk.mjs [caminho/do/binario/nodo]
import { spawn, spawnSync } from "node:child_process";
import http from "node:http";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";

const BIN = path.resolve(process.argv[2] || path.join(path.dirname(fileURLToPath(import.meta.url)), "..", "build", "nodo"));
let ok = 0, falhou = 0;
const t = (nome, cond, extra = "") => { if (cond) { ok++; console.log("  [ok]    " + nome); } else { falhou++; console.log("  [FALHOU] " + nome + " " + extra); } };

const dir = fs.mkdtempSync(path.join(os.tmpdir(), "nodo-sdk-"));
spawnSync(BIN, ["integrar", dir]);
let conf = fs.readFileSync(path.join(dir, "nodo.conf"), "utf8").replace("porta=7070", "porta=7086");
conf += "\nleitura_publica=mural\nescrita_publica=mural\ncampos.mural=nome:10,texto:50\nlimite_escrita=1000\n";
fs.writeFileSync(path.join(dir, "nodo.conf"), conf);
const chave = conf.match(/^chave=(.*)$/m)[1];
const srv = spawn(BIN, ["rodar", dir], { cwd: dir, stdio: "ignore" });
const dorme = ms => new Promise(r => setTimeout(r, ms));
await dorme(800);

function carrega(base) {  // executa o nodo.js como um navegador faria
  const js = fs.readFileSync(path.join(dir, "nodo.js"), "utf8");
  const g = { window: {}, document: { currentScript: { src: base + "/nodo.js" } }, location: { origin: base, hostname: new URL(base).hostname } };
  new Function("window", "document", "location", "fetch", "URLSearchParams", "AbortController", "URL", "console", "setTimeout", "clearTimeout", js)(g.window, g.document, g.location, fetch, URLSearchParams, AbortController, URL, console, setTimeout, clearTimeout);
  return g.window.Nodo;
}
const erro = async p => { try { await p; return null; } catch (e) { return e; } };

try {
  const N = carrega("http://127.0.0.1:7086");
  console.log("== validacao local (nada vai para a rede)");
  let e = null; try { N.colecao("a b"); } catch (x) { e = x; }
  t("colecao com espaco lanca NodoErro colecao_invalida", e && e.name === "NodoErro" && e.codigo === "colecao_invalida");
  e = null; try { N.colecao("_interna"); } catch (x) { e = x; }
  t("colecao que comeca com _ e recusada", e && e.codigo === "colecao_invalida");
  e = null; try { N.colecao("../x"); } catch (x) { e = x; }
  t("colecao com ../ e recusada", e && e.codigo === "colecao_invalida");
  e = null; try { N.colecao(42); } catch (x) { e = x; }
  t("colecao que nao e texto e recusada", e && e.codigo === "colecao_invalida");
  const mural = N.colecao("mural");
  t("id com / e rejeitado sem chamar a rede", (await erro(mural.obter("a/b"))).codigo === "id_invalido");
  t("id vazio rejeitado", (await erro(mural.obter(""))).codigo === "id_invalido");
  t("id undefined rejeitado", (await erro(mural.remover(undefined))).codigo === "id_invalido");
  t("id numerico rejeitado", (await erro(mural.atualizar(5, {}))).codigo === "id_invalido");
  t("criar(texto) rejeitado", (await erro(mural.criar("oi"))).codigo === "dados_invalidos");
  t("criar(null) rejeitado", (await erro(mural.criar(null))).codigo === "dados_invalidos");
  t("criar([..]) rejeitado", (await erro(mural.criar([1]))).codigo === "dados_invalidos");
  const circ = {}; circ.eu = circ;
  t("referencia circular vira erro claro", (await erro(mural.criar(circ))).codigo === "dados_invalidos");
  t("filtro com chave invalida", (await erro(mural.listar({ "a b": 1 }))).codigo === "filtro_invalido");
  t("filtro com objeto", (await erro(mural.listar({ a: { b: 1 } }))).codigo === "filtro_invalido");
  t("limite negativo", (await erro(mural.listar({ limite: -3 }))).codigo === "filtro_invalido");
  t("limite fracionado", (await erro(mural.listar({ limite: 1.5 }))).codigo === "filtro_invalido");
  e = null; try { N.configurar({ servidor: "ftp://x" }); } catch (x) { e = x; }
  t("configurar com servidor nao-http e recusado", e && e.codigo === "servidor_invalido");
  e = null; try { N.configurar({ servidor: "isso nao e url" }); } catch (x) { e = x; }
  t("configurar com servidor sem sentido e recusado", e && e.codigo === "servidor_invalido");

  console.log("== uso normal contra o Nodo de verdade");
  const novo = await mural.criar({ nome: "Maria", texto: "oi" });
  t("criar devolve _id e _t", novo._id && novo._t > 0 && novo.nome === "Maria");
  t("listar devolve array com o documento", (await mural.listar()).some(d => d._id === novo._id));
  t("listar com filtro e limite", (await mural.listar({ nome: "Maria", limite: 5 })).length >= 1);
  t("filtro com caracteres especiais vai codificado", Array.isArray(await mural.listar({ nome: "a&b=c d%" })));
  t("obter", (await mural.obter(novo._id)).texto === "oi");
  e = await erro(mural.criar({ nome: "x".repeat(11) }));
  t("erro do servidor chega como NodoErro com status e codigo", e.name === "NodoErro" && e.status === 400 && e.codigo === "campo_grande" && !e.rede, JSON.stringify(e && e.message));
  e = await erro(mural.criar({ admin: true }));
  t("campo nao permitido", e.codigo === "campo_nao_permitido");
  e = await erro(mural.obter("naoexiste"));
  t("404 vira NodoErro documento_nao_encontrado", e.status === 404 && e.codigo === "documento_nao_encontrado");
  e = await erro(mural.remover(novo._id));
  t("visitante sem chave nao apaga (401)", e.status === 401);
  e = await erro(N.colecao("segredo").listar());
  t("colecao privada sem chave -> 401", e.status === 401);
  t("saude()", (await N.saude()).ok === true);
  e = await erro(N.login("chave errada"));
  t("login com chave errada falha e nao guarda nada", e.status === 401 && !N.token && !N.chave);
  const l = await N.login(chave);
  t("login troca a chave por token e nao guarda a chave", l.token === N.token && N.token.length === 64 && N.chave === null);
  t("com o token, apaga documento", (await mural.remover(novo._id)).ok === true);
  t("com o token, le colecao privada", Array.isArray(await N.colecao("segredo").listar()));
  await N.logout();
  e = await erro(N.colecao("segredo").listar());
  t("depois do logout o token nao vale mais", e.status === 401 && N.token === null);

  console.log("== servidor fora do ar, lento ou respondendo lixo");
  const morto = carrega("http://127.0.0.1:1");
  morto.configurar({ tentativas: 1, timeout: 2000 });
  let t0 = Date.now();
  e = await erro(morto.colecao("x").listar());
  t("rede fora: NodoErro rede=true com mensagem util (e tentou de novo)", e.rede === true && e.codigo === "rede" && /ligado/.test(e.message) && Date.now() - t0 >= 250, e && e.message);
  t0 = Date.now();
  e = await erro(morto.colecao("x").criar({ a: 1 }));
  t("escrita NAO e repetida sozinha (evita duplicar)", e.rede === true && Date.now() - t0 < 250);

  const falso = http.createServer((req, res) => {
    if (req.url.startsWith("/api/html200")) { res.writeHead(200, { "content-type": "text/html" }); res.end("<html>Bem-vindo ao WiFi do hotel</html>"); }
    else if (req.url.startsWith("/api/json200html")) { res.writeHead(200, { "content-type": "application/json" }); res.end("<html>quebrado</html>"); }
    else if (req.url.startsWith("/api/html502")) { res.writeHead(502, { "content-type": "text/html" }); res.end("<h1>Bad Gateway</h1>"); }
    else if (req.url.startsWith("/api/objeto")) { res.writeHead(200, { "content-type": "application/json" }); res.end('{"nao":"e lista"}'); }
    else if (req.url.startsWith("/api/vazio204")) { res.writeHead(204); res.end(); }
    else if (req.url.startsWith("/api/erro500")) { res.writeHead(500, { "content-type": "application/json" }); res.end('{"erro":"deu ruim","codigo":"erro_interno"}'); }
    else if (req.url.startsWith("/api/lento")) { /* nunca responde */ }
    else if (req.url.startsWith("/api/instavel")) { falso.n = (falso.n || 0) + 1; if (falso.n < 3) { res.writeHead(503, { "content-type": "text/plain" }); res.end("indisponivel"); } else { res.writeHead(200, { "content-type": "application/json" }); res.end("[]"); } }
    else { res.writeHead(404); res.end(); }
  }).listen(7087);
  const F = carrega("http://127.0.0.1:7087");
  F.configurar({ timeout: 1000, tentativas: 2 });
  e = await erro(F.colecao("html200").listar());
  t("200 com HTML (portal de WiFi/proxy) -> resposta_invalida, nunca vira dado", e.codigo === "resposta_invalida" && e.status === 200);
  e = await erro(F.colecao("json200html").listar());
  t("content-type JSON mas corpo quebrado -> resposta_invalida", e.codigo === "resposta_invalida");
  e = await erro(F.colecao("html502").listar());
  t("502 com HTML -> erro claro http_502", e.status === 502 && e.codigo === "http_502");
  e = await erro(F.colecao("objeto").listar());
  t("listar recebendo objeto em vez de lista -> resposta_invalida", e.codigo === "resposta_invalida");
  t("204 sem corpo -> null (sem excecao)", (await F._f("DELETE", "/api/vazio204/abc")) === null);
  e = await erro(F.colecao("erro500").listar());
  t("500 em JSON preserva mensagem e codigo do servidor", e.status === 500 && e.message === "deu ruim" && e.codigo === "erro_interno");
  t0 = Date.now();
  e = await erro(F.colecao("lento").listar());
  t("servidor que nao responde -> tempo_esgotado (com tentativas)", e.codigo === "tempo_esgotado" && e.rede === true && Date.now() - t0 >= 3000 && Date.now() - t0 < 9000, String(Date.now() - t0));
  const r = await F.colecao("instavel").listar();
  t("leitura se recupera sozinha de 503 passageiro (2 tentativas)", Array.isArray(r) && falso.n === 3);
  falso.close(); falso.closeAllConnections?.();
} finally {
  srv.kill();
  fs.rmSync(dir, { recursive: true, force: true });
}
console.log("\nRESULTADO SDK: " + ok + " ok, " + falhou + " falhas");
process.exit(falhou ? 1 : 0);
