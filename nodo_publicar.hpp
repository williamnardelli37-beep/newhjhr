#pragma once
// Nodo - publicacao: Tailscale (HTTPS/acesso remoto), Git (deploy) e Vercel (site + dominio .com).
// Tudo que vem do usuario e validado por lista de caracteres permitidos antes de ir para um comando.
#include "nodo_core.hpp"
#include <regex>
#ifndef _WIN32
#include <sys/wait.h>
#endif
namespace pub {

struct Res { int cod = -1; string out; bool ok() const { return cod == 0; } };

static bool charsOk(const string& s, size_t max, const string& extra) {
  if (s.empty() || s.size() > max) return false;
  for (char ch : s) if (!isalnum((unsigned char)ch) && extra.find(ch) == string::npos) return false;
  return true;
}
static bool urlGitOk(const string& u) {
  bool inicio = u.rfind("https://", 0) == 0 || u.rfind("git@", 0) == 0 || u.rfind("ssh://", 0) == 0 || u.rfind("file://", 0) == 0 || u[0] == '/';
  return !u.empty() && inicio && charsOk(u, 300, "@:/._~+-") && u.find("..") == string::npos;
}
static bool branchOk(const string& b) { return charsOk(b, 100, "._/-") && b[0] != '-' && b.find("..") == string::npos; }
static bool dominioOk(const string& d) {
  return charsOk(d, 253, ".-") && d.find('.') != string::npos && d.front() != '.' && d.front() != '-' && d.back() != '.' && d.back() != '-' && d.find("..") == string::npos;
}
static bool tokenOk(const string& t) { return charsOk(t, 300, "_-"); }
static bool projOk(const string& p) { return charsOk(p, 100, "._-"); }
static bool numOk(const string& p) {
  int pontos = 0;
  if (p.empty() || p.size() > 12) return false;
  for (char ch : p) { if (ch == '.') pontos++; else if (!isdigit((unsigned char)ch)) return false; }
  return pontos <= 1 && p.front() != '.';
}

// ---------------------------------------------------------------- executar comandos
static Res rodar(const string& cmd, size_t cap = 400000) {
  Res r;
  string full = cmd + " 2>&1";
#ifdef _WIN32
  full = "\"" + full + "\"";
  FILE* f = _popen(full.c_str(), "r");
#else
  FILE* f = popen(full.c_str(), "r");
#endif
  if (!f) { r.out = "nao foi possivel executar o comando"; return r; }
  char b[4096];
  size_t n;
  while ((n = fread(b, 1, sizeof b, f)) > 0) if (r.out.size() < cap) r.out.append(b, n);
#ifdef _WIN32
  r.cod = _pclose(f);
#else
  int st = pclose(f);
  r.cod = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
#endif
  return r;
}
// linha a linha (ex.: "tailscale up" bloqueia ate o login e imprime o link no inicio)
static int rodarLinhas(const string& cmd, const function<void(const string&)>& cb) {
  string full = cmd + " 2>&1";
#ifdef _WIN32
  full = "\"" + full + "\"";
  FILE* f = _popen(full.c_str(), "r");
#else
  FILE* f = popen(full.c_str(), "r");
#endif
  if (!f) { cb("nao foi possivel executar o comando"); return -1; }
  char b[4096];
  while (fgets(b, sizeof b, f)) cb(trim(b));
#ifdef _WIN32
  return _pclose(f);
#else
  int st = pclose(f);
  return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
#endif
}
static void setEnv(const char* k, const char* v) {
#ifdef _WIN32
  _putenv_s(k, v);
#else
  setenv(k, v, 1);
#endif
}
static Res git(const string& args) {
  setEnv("GIT_TERMINAL_PROMPT", "0");  // nunca trava esperando senha: usa o que ja esta salvo no seu sistema
  setEnv("GCM_INTERACTIVE", "never");
  setEnv("GIT_SSH_COMMAND", "ssh -o BatchMode=yes -o StrictHostKeyChecking=accept-new");
  return rodar("git " + args);
}
static string linhas(const Res& r) { string o = trim(r.out); return o.empty() ? (r.ok() ? "ok" : "falhou (codigo " + to_string(r.cod) + ")") : o; }

// ---------------------------------------------------------------- arquivos do projeto: .gitignore, .vercelignore, README
static const char* SEGREDOS = "# Nodo - NUNCA envie estes arquivos (contem a chave de administrador e os dados)\nnodo.conf\nnodo-dados/\nnodo\nnodo.exe\niniciar.sh\nparar.sh\niniciar.bat\nparar.bat\n*.bak\n.DS_Store\n";
static void garantirIgnore(const string& arq) {  // acrescenta so as linhas que faltam
  string atual;
  readFile(arq, atual);
  set<string> tem;
  for (auto& l : split(atual, '\n')) tem.insert(trim(l));
  string add;
  for (auto& l : split(SEGREDOS, '\n')) if (!trim(l).empty() && !tem.count(trim(l))) add += l + "\n";
  if (!add.empty()) writeFile(arq, atual + (atual.empty() || atual.back() == '\n' ? "" : "\n") + add);
}
static string nomePasta() {
  error_code ec;
  string n = fs::current_path(ec).filename().u8string();
  return n.empty() ? "meu-site" : n;
}
static string gerarReadme(bool forcar = false) {
  if (fs::exists("README.md") && !forcar) return "README.md ja existe (mantido).";
  string nome = nomePasta();
  string r = "# " + nome + "\n\nSite com servidor e banco de dados pelo [Nodo](https://nodo.app) - *server for life*.\n\n## Rodar no seu computador\n\n```\nnodo iniciar .\n```\n\nAbra http://localhost:" + to_string(cfg.porta) + ". O painel de dados fica em `/admin` e o guia de integração em `/guia`.\n\n";
  r += "## Publicar\n\n- **Código e deploy:** `git push` envia o site";
  if (!cfg.gitRemoto.empty()) r += " para `" + cfg.gitRemoto + "`";
  r += ". Para atualizar o servidor depois de um push: `nodo implantar` (ou `POST /api/_deploy` com o cabeçalho `X-Nodo-Key`).\n";
  r += "- **Servidor na internet (HTTPS):** Tailscale Funnel";
  if (!cfg.tsUrl.empty()) r += " em " + cfg.tsUrl;
  r += ".\n- **Site na Vercel";
  if (!cfg.dominio.empty()) r += " com o domínio " + cfg.dominio;
  r += ":** a Vercel publica a cada push; o site fala com o Nodo pelo endereço acima.\n\n";
  r += "## Segurança\n\n- `nodo.conf` (chave de administrador) e `nodo-dados/` **nunca** vão para o Git: já estão no `.gitignore`.\n- Nunca coloque a chave no código do site.\n- Antes de publicar: `nodo verificar`.\n";
  return writeFile("README.md", r) ? "README.md criado." : "Nao foi possivel criar o README.md.";
}
static string prepararArquivos() {
  garantirIgnore(".gitignore");
  garantirIgnore(".vercelignore");
  return gerarReadme() + " .gitignore e .vercelignore protegem nodo.conf e nodo-dados.";
}

// ---------------------------------------------------------------- Git
static bool temRepo() { return fs::exists(".git"); }
static string gitVincular(const string& url, const string& branch) {
  if (!urlGitOk(url)) return "Endereco do repositorio invalido. Use https://github.com/usuario/repo.git ou git@github.com:usuario/repo.git";
  if (!branchOk(branch)) return "Nome de branch invalido.";
  string log = prepararArquivos() + "\n";
  if (!git("--version").ok()) return "Git nao encontrado. Instale em https://git-scm.com/downloads e tente de novo.";
  if (!temRepo()) log += "git init: " + linhas(git("init -q")) + "\n";
  git("symbolic-ref HEAD refs/heads/" + branch);
  if (git("remote get-url origin").ok()) log += "origin atualizado: " + linhas(git("remote set-url origin \"" + url + "\"")) + "\n";
  else log += "origin adicionado: " + linhas(git("remote add origin \"" + url + "\"")) + "\n";
  Res f = git("fetch origin");
  log += f.ok() ? "Repositorio remoto alcancado.\n" : "Aviso: nao foi possivel buscar o remoto agora (repositorio vazio, sem acesso ou sem login no Git): " + linhas(f) + "\n";
  if (!git("rev-parse --verify HEAD").ok() && git("rev-parse --verify origin/" + branch).ok()) {
    git("reset -q origin/" + branch);
    log += "Historico do remoto adotado; seus arquivos locais foram mantidos (veja o que mudou antes de enviar).\n";
  }
  cfg.gitRemoto = url; cfg.gitBranch = branch;
  saveCfg();
  return log + "Pronto: repositorio vinculado.";
}
static string gitEnviar(string msg) {
  if (!temRepo()) return "Esta pasta ainda nao esta vinculada a um repositorio.";
  if (!branchOk(cfg.gitBranch)) return "Branch invalida.";
  string m;
  for (char ch : msg) m += (isalnum((unsigned char)ch) || strchr(" .,-_:", ch)) ? ch : '_';
  if (m.size() > 100) m.resize(100);
  if (trim(m).empty()) m = "Atualizacao pelo Nodo";
  string log = prepararArquivos() + "\n";
  git("add -A");
  for (const char* seg : {"nodo.conf", "nodo-dados"})  // rede de seguranca: se algum segredo ja estava versionado, tira
    if (git(string("ls-files --error-unmatch ") + seg).ok()) { git(string("rm -r --cached -q ") + seg); log += string("Removido do Git (era segredo): ") + seg + "\n"; }
  Res c = git("commit -m \"" + m + "\"");
  log += "commit: " + linhas(c) + "\n";
  Res p = git("push -u origin " + cfg.gitBranch);
  log += "push: " + linhas(p);
  if (!p.ok()) log += "\nSe disser 'rejected', rode Atualizar antes. Se pedir login, entre no Git no seu computador (GitHub Desktop, 'gh auth login' ou chave SSH).";
  return log;
}
static string gitAtualizar() {
  if (!temRepo()) return "Esta pasta ainda nao esta vinculada a um repositorio.";
  Res r = git("pull --ff-only origin " + cfg.gitBranch);
  return linhas(r);
}
static string deploy() { return gitAtualizar(); }
static string gitEstado() {
  if (!temRepo()) return "Nao vinculado.";
  Res s = git("status --short");
  int n = 0;
  for (char ch : s.out) if (ch == '\n') n++;
  return "Repositorio: " + (cfg.gitRemoto.empty() ? string("(sem remoto)") : cfg.gitRemoto) + " | branch " + cfg.gitBranch + " | " + (n ? to_string(n) + " arquivo(s) com mudancas" : string("sem mudancas"));
}

// ---------------------------------------------------------------- apontar o site para o servidor (site na Vercel + Nodo em casa)
static string apontarSite(const string& url) {  // url vazia = volta ao nodo.js local
  string alvo = "nodo.js";
  if (!url.empty()) {
    if (url.rfind("https://", 0) != 0 || !charsOk(url, 200, ".:/-")) return "Endereco invalido (use https://...).";
    alvo = (url.back() == '/' ? url.substr(0, url.size() - 1) : url) + "/nodo.js";
  }
  regex re("src\\s*=\\s*\"[^\"]*nodo\\.js\"");
  int arquivos = 0;
  error_code ec;
  fs::recursive_directory_iterator it(fs::current_path(ec), ec), fim;
  for (; !ec && it != fim; it.increment(ec)) {
    string nome = it->path().filename().u8string();
    if (it->is_directory(ec) && (nome[0] == '.' || nome == "nodo-dados" || nome == "node_modules" || nome == "downloads")) { it.disable_recursion_pending(); continue; }
    if (!it->is_regular_file(ec) || it->path().extension() != ".html") continue;
    string h;
    if (!readFile(it->path().u8string(), h)) continue;
    string novo = regex_replace(h, re, "src=\"" + alvo + "\"");
    if (novo != h && writeFile(it->path().u8string(), novo)) arquivos++;
  }
  return to_string(arquivos) + " arquivo(s) HTML agora usam " + alvo;
}

// ---------------------------------------------------------------- Tailscale
static string tsExe() {
  static string cache;
  if (!cache.empty()) return cache;
  for (const char* c : {"tailscale", "\"/Applications/Tailscale.app/Contents/MacOS/Tailscale\"", "\"C:\\Program Files\\Tailscale\\tailscale.exe\""})
    if (rodar(string(c) + " version", 4000).ok()) return cache = c;
  return "";
}
struct TsEstado { bool instalado = false, logado = false, funnel = false; string estado, host, ip, url, erro; };
static TsEstado tsEstado() {
  TsEstado e;
  string ts = tsExe();
  if (ts.empty()) return e;
  e.instalado = true;
  Res r = rodar(ts + " status --json", 2000000);
  vector<pair<string, string>> top;
  if (!jpairs(minify(r.out), top)) { e.erro = trim(r.out).substr(0, 300); return e; }
  for (auto& kv : top) {
    if (kv.first == "BackendState") e.estado = unq(kv.second);
    if (kv.first == "Self") {
      vector<pair<string, string>> self;
      if (jpairs(kv.second, self)) for (auto& s2 : self) if (s2.first == "DNSName") e.host = unq(s2.second);
    }
  }
  while (!e.host.empty() && e.host.back() == '.') e.host.pop_back();
  e.logado = e.estado == "Running";
  if (e.logado && !e.host.empty()) {
    e.url = "https://" + e.host;
    e.funnel = rodar(ts + " funnel status", 8000).out.find("Funnel on") != string::npos;
  }
  return e;
}
static string tsPublicar(bool internet) {  // internet=true: Funnel (qualquer pessoa); false: so seus dispositivos (Serve)
  string ts = tsExe();
  if (ts.empty()) return "Tailscale nao encontrado. Instale em https://tailscale.com/download";
  TsEstado e = tsEstado();
  if (!e.logado) return "Entre no Tailscale primeiro (botao Entrar).";
  Res r = rodar(ts + (internet ? " funnel --bg " : " serve --bg ") + to_string(cfg.porta), 30000);
  string log = linhas(r);
  if (r.ok()) {
    cfg.tsUrl = e.url;
    cfg.proxyConfiavel = true;  // o Tailscale fica na frente do Nodo, no mesmo PC
    saveCfg();
    log += "\nEndereco: " + e.url + (internet ? " (publico, com HTTPS)" : " (so nos seus dispositivos)") + "\nO servidor Nodo precisa ser reiniciado para aplicar (o app faz isso).";
  } else if (internet) log += "\nSe pedir, habilite HTTPS e o Funnel no painel do Tailscale (login.tailscale.com/admin/dns e /admin/acls).";
  return log;
}
static string tsDesligar() {
  string ts = tsExe();
  if (ts.empty()) return "Tailscale nao encontrado.";
  Res r = rodar(ts + " funnel --https=443 off", 15000);
  Res s = rodar(ts + " serve reset", 15000);
  return linhas(r) + "\n" + linhas(s);
}

// ---------------------------------------------------------------- Vercel (API) via curl, token so em memoria
static string escCurl(const string& s) {
  string o;
  for (char ch : s) { if (ch == '"' || ch == '\\') o += '\\'; o += ch; }
  return o;
}
static Res vercel(const string& token, const string& metodo, const string& caminho, const string& corpo = "") {
  Res r;
  if (!tokenOk(token)) { r.out = "Token da Vercel invalido (crie em vercel.com/account/tokens)."; return r; }
  if (!rodar("curl --version", 4000).ok()) { r.out = "curl nao encontrado neste computador."; return r; }
  string url = "https://api.vercel.com" + caminho;
  if (!cfg.vercelTeam.empty()) url += (caminho.find('?') != string::npos ? "&" : "?") + string("teamId=") + cfg.vercelTeam;
  string conf = "silent\nshow-error\nmax-time = 30\nrequest = \"" + metodo + "\"\nurl = \"" + escCurl(url) + "\"\nheader = \"Authorization: Bearer " + token + "\"\nheader = \"Content-Type: application/json\"\n";
  if (!corpo.empty()) conf += "data = \"" + escCurl(corpo) + "\"\n";
  error_code ec;
  string tmp = (fs::temp_directory_path(ec) / ("nodo-v-" + hexrand(12) + ".cfg")).u8string();
  if (!writePrivado(tmp, conf)) { r.out = "Nao foi possivel criar arquivo temporario."; return r; }
  r = rodar("curl -K \"" + tmp + "\"", 200000);
  fs::remove(fs::u8path(tmp), ec);
  return r;
}
static string numeroApos(const string& s, const string& chave) {
  size_t p = s.find(chave);
  if (p == string::npos) return "";
  p += chave.size();
  while (p < s.size() && (s[p] == ' ' || s[p] == '"')) p++;
  string n;
  while (p < s.size() && (isdigit((unsigned char)s[p]) || s[p] == '.')) n += s[p++];
  return numOk(n) ? n : "";
}
struct DominioInfo { bool ok = false, disponivel = false; string preco, msg; };
static DominioInfo dominioBuscar(const string& token, const string& nome) {
  DominioInfo d;
  if (!dominioOk(nome)) { d.msg = "Nome de dominio invalido."; return d; }
  Res a = vercel(token, "GET", "/v4/domains/status?name=" + nome);
  if (a.out.find("\"available\"") == string::npos) { d.msg = "Resposta da Vercel: " + trim(a.out).substr(0, 400); return d; }
  d.ok = true;
  d.disponivel = a.out.find("\"available\":true") != string::npos || a.out.find("\"available\": true") != string::npos;
  if (d.disponivel) {
    Res p = vercel(token, "GET", "/v4/domains/price?name=" + nome);
    d.preco = numeroApos(p.out, "\"price\":");
    if (d.preco.empty()) d.msg = "Disponivel, mas nao consegui ler o preco: " + trim(p.out).substr(0, 300);
  }
  return d;
}
static string dominioComprar(const string& token, const string& nome, const string& preco) {
  if (!dominioOk(nome)) return "Nome de dominio invalido.";
  if (!numOk(preco)) return "Preco invalido; busque o dominio de novo antes de comprar.";
  Res r = vercel(token, "POST", "/v5/domains/buy", "{\"name\":\"" + nome + "\",\"expectedPrice\":" + preco + ",\"renew\":true}");
  if (r.out.find("\"error\"") == string::npos && !trim(r.out).empty()) { cfg.dominio = nome; saveCfg(); }
  return "Resposta da Vercel: " + trim(r.out).substr(0, 600);
}
static string repoDoRemoto(string& tipo) {  // "owner/repo" do remoto Git (github/gitlab/bitbucket)
  string u = cfg.gitRemoto;
  smatch m;
  if (regex_search(u, m, regex("^(?:https://|git@|ssh://git@)(github\\.com|gitlab\\.com|bitbucket\\.org)[:/]([A-Za-z0-9._-]+/[A-Za-z0-9._-]+?)(?:\\.git)?/?$"))) {
    tipo = m[1] == "github.com" ? "github" : m[1] == "gitlab.com" ? "gitlab" : "bitbucket";
    return m[2];
  }
  return "";
}
static string vercelCriarProjeto(const string& token, const string& nome) {
  if (!projOk(nome)) return "Nome de projeto invalido (letras minusculas, numeros, . _ -).";
  string tipo, repo = repoDoRemoto(tipo);
  if (repo.empty()) return "Vincule primeiro um repositorio do GitHub, GitLab ou Bitbucket (etapa 2).";
  Res r = vercel(token, "POST", "/v10/projects", "{\"name\":\"" + nome + "\",\"gitRepository\":{\"type\":\"" + tipo + "\",\"repo\":\"" + repo + "\"}}");
  if (r.out.find("\"error\"") == string::npos && !trim(r.out).empty()) { cfg.vercelProjeto = nome; saveCfg(); }
  return "Resposta da Vercel: " + trim(r.out).substr(0, 600) + "\nSe pedir, instale o app da Vercel no seu GitHub: vercel.com/new";
}
static string vercelVincularDominio(const string& token, const string& projeto, const string& dominio) {
  if (!projOk(projeto) || !dominioOk(dominio)) return "Projeto ou dominio invalido.";
  Res r = vercel(token, "POST", "/v10/projects/" + projeto + "/domains", "{\"name\":\"" + dominio + "\"}");
  return "Resposta da Vercel: " + trim(r.out).substr(0, 600);
}

}  // namespace pub
