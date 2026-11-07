#pragma once
// Nodo 3 - varios projetos (sites) na mesma maquina.
// Cada projeto roda no SEU PROPRIO processo, com a sua pasta, a sua chave e a sua porta: um site que cai ou e invadido
// nao derruba nem enxerga os outros. Este arquivo guarda a lista de projetos do usuario, reparte as portas sem colisao
// e liga/desliga cada um chamando o proprio programa "nodo" (sem shell: nenhum nome de pasta vira comando).
#include "nodo_core.hpp"
#ifndef _WIN32
#include <sys/wait.h>
#endif

namespace proj {

static const size_t MAX_PROJETOS = 32;
static const int PORTA_BASE = 7070;
static const int PORTA_FIM = 7470;

struct Item {
  string nome, pasta, dados = "nodo-dados";
  int porta = 0, pid = 0;
  bool rodando = false, confOk = false;
};

// ---------------------------------------------------------------- onde fica a lista (uma por usuario do sistema)
static string dirApp() {
  if (const char* h = getenv("NODO_HOME")) if (*h) return h;  // permite testar e isolar
#ifdef _WIN32
  const char* a = getenv("APPDATA");
  return string(a && *a ? a : ".") + "\\Nodo";
#elif defined(__APPLE__)
  const char* h = getenv("HOME");
  return string(h && *h ? h : ".") + "/Library/Application Support/Nodo";
#else
  const char* x = getenv("XDG_CONFIG_HOME");
  if (x && *x) return string(x) + "/nodo";
  const char* h = getenv("HOME");
  return string(h && *h ? h : ".") + "/.config/nodo";
#endif
}
static string arqLista() { return dirApp() + "/projetos.txt"; }

static bool pidVivo(int p) {
  if (p <= 0) return false;
#ifdef _WIN32
  HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, p);
  if (!h) return false;
  DWORD code = 0;
  bool ok = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
  CloseHandle(h);
  return ok;
#else
  return kill(p, 0) == 0;
#endif
}

static string canon(const string& p) {
  error_code ec;
  auto c = fs::weakly_canonical(fs::u8path(p), ec);
  return ec ? p : c.u8string();
}
static bool pastaSegura(const string& p) {  // a pasta vai para um arquivo de texto e para a linha de comando: sem controles nem aspas
  if (p.empty() || p.size() > 1000) return false;
  for (char ch : p) if ((unsigned char)ch < 0x20 || ch == '"' || ch == 0x7f) return false;
  return true;
}
static string slug(const string& s) {
  string o;
  for (char ch : s) o += (isalnum((unsigned char)ch) || ch == '.' || ch == '_' || ch == '-') ? ch : '-';
  while (!o.empty() && (o.front() == '-' || o.front() == '.')) o.erase(o.begin());
  while (!o.empty() && o.back() == '-') o.pop_back();
  if (o.size() > 40) o.resize(40);
  return o.empty() ? "site" : o;
}

// ---------------------------------------------------------------- estado de um projeto, lido dos arquivos dele (nao mexe nas variaveis globais do servidor)
static void preencher(Item& it) {
  Cfg c;
  string conf = it.pasta + "/nodo.conf";
  { ifstream t(conf); if (!t) { it.confOk = false; it.porta = 0; it.rodando = false; return; } }
  lerCfgArquivo(c, conf.c_str(), false);
  it.confOk = c.chave.size() >= 32 && c.porta > 0 && c.porta < 65536;
  it.porta = c.porta;
  it.dados = c.dados;
  fs::path d = fs::u8path(c.dados);
  fs::path pf = d.is_absolute() ? d / "nodo.pid" : fs::u8path(it.pasta) / d / "nodo.pid";
  string s;
  it.pid = 0;
  if (readFile(pf.u8string(), s)) it.pid = atoi(s.c_str());
  it.rodando = pidVivo(it.pid);
}
static vector<Item> carregar() {
  vector<Item> v;
  string txt;
  if (!readFile(arqLista(), txt)) return v;
  for (auto& l : split(txt, '\n')) {
    string linha = l;
    if (!linha.empty() && linha.back() == '\r') linha.pop_back();
    size_t t = linha.find('\t');
    if (t == string::npos || linha.empty() || linha[0] == '#') continue;
    Item it;
    it.nome = linha.substr(0, t);
    it.pasta = linha.substr(t + 1);
    if (it.nome.empty() || !pastaSegura(it.pasta) || v.size() >= MAX_PROJETOS) continue;
    preencher(it);
    v.push_back(it);
  }
  return v;
}
static bool salvar(const vector<Item>& v) {
  error_code ec;
  fs::create_directories(fs::u8path(dirApp()), ec);
  nodoChmod(dirApp().c_str(), 0700);
  string o = "# Nodo - projetos deste usuario (nome<TAB>pasta). Edite pelo app ou por: nodo projetos\n";
  for (auto& it : v) o += it.nome + "\t" + it.pasta + "\n";
  return writePrivado(arqLista(), o);
}
static Item* achar(vector<Item>& v, const string& ref) {
  string c = canon(ref);
  for (auto& it : v) if (it.nome == ref || it.pasta == c || it.pasta == ref) return &it;
  return nullptr;
}

// ---------------------------------------------------------------- portas
static bool bindavel(int porta) {
  redeInit();
  int fd = (int)socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return false;
  sockReuse(fd);
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_port = htons(porta);
  a.sin_addr.s_addr = htonl(INADDR_ANY);
  bool ok = ::bind(fd, (sockaddr*)&a, sizeof a) == 0;
  cerrar(fd);
  return ok;
}
// Escolhe uma porta livre: a preferida se der; senao a primeira livre da faixa. Ignora a porta 7071 (descoberta na rede local).
static int escolherPorta(const vector<Item>& v, const string& pastaIgnorar, int preferida) {
  set<int> usadas;
  for (auto& o : v) if (o.pasta != pastaIgnorar && o.confOk) usadas.insert(o.porta);
  auto serve = [&](int p) { return p >= 1024 && p < 65536 && p != 7071 && !usadas.count(p) && bindavel(p); };
  if (preferida > 0 && serve(preferida)) return preferida;
  for (int p = PORTA_BASE; p < PORTA_FIM; p++) if (serve(p)) return p;
  return 0;
}
static bool gravarPorta(const string& pasta, int porta) {
  string conf = pasta + "/nodo.conf", txt, o;
  if (!readFile(conf, txt)) return false;
  bool feito = false;
  for (auto& l : split(txt, '\n')) {
    if (!feito && trim(l).rfind("porta=", 0) == 0) { o += "porta=" + to_string(porta) + "\n"; feito = true; }
    else o += l + "\n";
  }
  if (!feito) o = "porta=" + to_string(porta) + "\n" + o;
  while (o.size() > 1 && o.size() >= 2 && o[o.size() - 1] == '\n' && o[o.size() - 2] == '\n') o.pop_back();
  return writePrivado(conf, o);
}

// ---------------------------------------------------------------- executar o proprio programa, sem shell
static int executar(const vector<string>& a) {
  if (a.empty()) return -1;
#ifdef _WIN32
  string cmd;
  for (auto& s : a) {
    if (s.find('"') != string::npos) return -1;
    string x = s;
    if (!x.empty() && x.back() == '\\') x += "\\";
    cmd += (cmd.empty() ? "" : " ") + string("\"") + x + "\"";
  }
  STARTUPINFOA si{};
  si.cb = sizeof si;
  PROCESS_INFORMATION pi{};
  vector<char> cb(cmd.begin(), cmd.end());
  cb.push_back(0);
  if (!CreateProcessA(nullptr, cb.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) return -1;
  WaitForSingleObject(pi.hProcess, 20000);
  DWORD code = 1;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  return (int)code;
#else
  vector<char*> v;  // montado ANTES do fork: depois do fork (em programa com varias threads) so se pode chamar funcoes simples
  for (auto& s : a) v.push_back(const_cast<char*>(s.c_str()));
  v.push_back(nullptr);
  pid_t p = fork();
  if (p < 0) return -1;
  if (p == 0) {
    int nul = open("/dev/null", O_RDWR);
    if (nul >= 0) { dup2(nul, 0); dup2(nul, 1); dup2(nul, 2); }
    execv(v[0], v.data());
    _exit(127);
  }
  int st = 0;
  if (waitpid(p, &st, 0) < 0) return -1;
  return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
#endif
}

// ---------------------------------------------------------------- operacoes (todas devolvem uma mensagem para mostrar ao usuario)
struct Res { bool ok = false; string msg, chave; int porta = 0; };

static Res adicionar(const string& pastaPedida, string nome = "", int portaPedida = 0) {
  Res R;
  if (!pastaSegura(pastaPedida)) { R.msg = "Caminho de pasta invalido (sem aspas nem caracteres de controle)."; return R; }
  auto v = carregar();
  if (v.size() >= MAX_PROJETOS) { R.msg = "Limite de " + to_string(MAX_PROJETOS) + " projetos neste computador."; return R; }
  error_code ec;
  fs::create_directories(fs::u8path(pastaPedida), ec);
  if (!fs::is_directory(fs::u8path(pastaPedida), ec)) { R.msg = "Nao foi possivel usar a pasta: " + pastaPedida; return R; }
  string pasta = canon(pastaPedida);
  if (!pastaSegura(pasta)) { R.msg = "Caminho de pasta invalido."; return R; }
  if (Item* ja = achar(v, pasta)) { R.msg = "Esta pasta ja esta na lista como '" + ja->nome + "' (porta " + to_string(ja->porta) + ")."; R.porta = ja->porta; return R; }
  IntegrarRes ir = integrarPasta(pasta, "", false);  // cria nodo.conf (com chave nova), nodo.js e nodo.regras; sem copiar o programa para dentro do site
  if (!ir.ok) { R.msg = ir.msg; return R; }
  R.chave = ir.novo ? ir.chave : "";
  nome = slug(nome.empty() ? fs::u8path(pasta).filename().u8string() : nome);
  string base = nome;
  for (int i = 2; achar(v, nome); i++) nome = base + "-" + to_string(i);
  Item it;
  it.nome = nome;
  it.pasta = pasta;
  preencher(it);
  if (!it.confOk) { R.msg = "O nodo.conf desta pasta esta invalido."; return R; }
  int quer = portaPedida > 0 ? portaPedida : it.porta;
  int p = escolherPorta(v, pasta, quer);
  if (p == 0) { R.msg = "Nao ha porta livre entre " + to_string(PORTA_BASE) + " e " + to_string(PORTA_FIM) + "."; return R; }
  if (p != it.porta && !gravarPorta(pasta, p)) { R.msg = "Nao foi possivel gravar a porta no nodo.conf."; return R; }
  it.porta = p;
  v.push_back(it);
  if (!salvar(v)) { R.msg = "Nao foi possivel gravar a lista de projetos em " + arqLista(); return R; }
  R.ok = true;
  R.porta = p;
  R.msg = "Projeto '" + nome + "' adicionado na porta " + to_string(p) + (p != quer && quer > 0 ? " (a " + to_string(quer) + " ja estava em uso)" : "") + ".";
  return R;
}
static Res remover(const string& ref) {
  Res R;
  auto v = carregar();
  Item* it = achar(v, ref);
  if (!it) { R.msg = "Projeto nao encontrado: " + ref; return R; }
  if (it->rodando) { R.msg = "'" + it->nome + "' esta ligado. Desligue antes de tirar da lista."; return R; }
  string nome = it->nome;
  v.erase(v.begin() + (it - &v[0]));
  if (!salvar(v)) { R.msg = "Nao foi possivel gravar a lista."; return R; }
  R.ok = true;
  R.msg = "'" + nome + "' saiu da lista. Os arquivos da pasta foram mantidos.";
  return R;
}
static Res definirPorta(const string& ref, int porta) {
  Res R;
  auto v = carregar();
  Item* it = achar(v, ref);
  if (!it) { R.msg = "Projeto nao encontrado: " + ref; return R; }
  if (it->rodando) { R.msg = "Desligue '" + it->nome + "' antes de trocar a porta."; return R; }
  if (porta < 1024 || porta > 65535 || porta == 7071) { R.msg = "Use uma porta entre 1024 e 65535 (a 7071 e da descoberta na rede)."; return R; }
  for (auto& o : v) if (&o != it && o.confOk && o.porta == porta) { R.msg = "A porta " + to_string(porta) + " ja e do projeto '" + o.nome + "'."; return R; }
  if (!bindavel(porta)) { R.msg = "A porta " + to_string(porta) + " esta em uso por outro programa."; return R; }
  if (!gravarPorta(it->pasta, porta)) { R.msg = "Nao foi possivel gravar o nodo.conf."; return R; }
  R.ok = true;
  R.porta = porta;
  R.msg = "'" + it->nome + "' agora usa a porta " + to_string(porta) + ".";
  return R;
}
// "self" = caminho do programa de linha de comando (nodo / nodo.exe)
static Res iniciar(const string& ref, const string& self) {
  Res R;
  auto v = carregar();
  Item* it = achar(v, ref);
  if (!it) { R.msg = "Projeto nao encontrado: " + ref; return R; }
  if (self.empty()) { R.msg = "Nao encontrei o programa 'nodo' para ligar o projeto."; return R; }
  if (!it->confOk) { R.msg = "'" + it->nome + "': nodo.conf ausente ou invalido em " + it->pasta; return R; }
  if (it->rodando) { R.ok = true; R.porta = it->porta; R.msg = "'" + it->nome + "' ja esta ligado na porta " + to_string(it->porta) + "."; return R; }
  string aviso;
  bool repetida = false;
  for (auto& o : v) if (&o != &*it && o.confOk && o.porta == it->porta && (o.rodando || &o < &*it)) repetida = true;
  if (repetida || !bindavel(it->porta)) {  // porta ja tomada por outro projeto ou outro programa: escolhe outra e grava
    int p = escolherPorta(v, it->pasta, 0);
    if (p == 0) { R.msg = "Nao ha porta livre para '" + it->nome + "'."; return R; }
    if (!gravarPorta(it->pasta, p)) { R.msg = "Nao foi possivel gravar a nova porta no nodo.conf."; return R; }
    aviso = " (a porta " + to_string(it->porta) + " estava ocupada; passou a usar a " + to_string(p) + ")";
    it->porta = p;
  }
  int rc = executar({self, "iniciar", it->pasta});
  for (int i = 0; i < 40; i++) {  // espera ate ~4 s o servidor aparecer
    preencher(*it);
    string r;
    if (it->rodando && http_do("127.0.0.1:" + to_string(it->porta), "GET", "/api/_saude", "", &r, 2)) break;
    dormirMs(100);
  }
  preencher(*it);
  if (!it->rodando) { R.msg = "'" + it->nome + "' nao ligou (codigo " + to_string(rc) + "). Veja " + it->pasta + "/" + it->dados + "/nodo.log"; return R; }
  R.ok = true;
  R.porta = it->porta;
  R.msg = "'" + it->nome + "' ligado: http://localhost:" + to_string(it->porta) + aviso;
  return R;
}
static Res parar(const string& ref, const string& self) {
  Res R;
  auto v = carregar();
  Item* it = achar(v, ref);
  if (!it) { R.msg = "Projeto nao encontrado: " + ref; return R; }
  if (!it->rodando) { R.ok = true; R.msg = "'" + it->nome + "' ja esta desligado."; return R; }
  if (self.empty()) { R.msg = "Nao encontrei o programa 'nodo' para desligar o projeto."; return R; }
  executar({self, "parar", it->pasta});
  for (int i = 0; i < 30; i++) { preencher(*it); if (!it->rodando) break; dormirMs(100); }
  if (it->rodando) { R.msg = "'" + it->nome + "' continua ligado."; return R; }
  R.ok = true;
  R.msg = "'" + it->nome + "' desligado.";
  return R;
}

// ---------------------------------------------------------------- linha de comando: nodo projetos ...
static string col(const string& s, size_t n) { return s.size() >= n ? s.substr(0, n - 1) + " " : s + string(n - s.size(), ' '); }
static void listar(const vector<Item>& v) {
  if (v.empty()) { cout << "Nenhum projeto na lista. Adicione um com:  nodo projetos adicionar <pasta> [nome]" << endl; return; }
  cout << col("NOME", 16) << col("PORTA", 7) << col("ESTADO", 11) << "PASTA\n";
  for (auto& it : v)
    cout << col(it.nome, 16) << col(it.confOk ? to_string(it.porta) : "-", 7) << col(!it.confOk ? "sem conf" : it.rodando ? "ligado" : "desligado", 11) << it.pasta << "\n";
  int n = 0;
  for (auto& it : v) if (it.rodando) n++;
  cout << "\n" << n << " ligado(s) de " << v.size() << ". Painel de cada um: http://localhost:<porta>/admin" << endl;
}
static int ajuda() {
  cout << "nodo projetos - varios sites neste computador, cada um no seu processo, com porta propria\n\n"
       << "  nodo projetos [listar]                         lista os projetos e o estado de cada um\n"
       << "  nodo projetos adicionar <pasta> [nome] [--porta N]   prepara a pasta (se precisar) e cadastra o projeto\n"
       << "  nodo projetos remover <nome>                   tira da lista (nao apaga arquivos; precisa estar desligado)\n"
       << "  nodo projetos iniciar [nome|todos]             liga um projeto ou todos\n"
       << "  nodo projetos parar [nome|todos]               desliga um projeto ou todos\n"
       << "  nodo projetos porta <nome> <numero>            troca a porta de um projeto desligado\n\n"
       << "Limite: " << MAX_PROJETOS << " projetos. Portas automaticas: " << PORTA_BASE << " a " << PORTA_FIM << ".\n";
  return 0;
}
static int cli(const vector<string>& a, const string& self) {  // a = argumentos depois de "projetos"
  string sub = a.empty() ? "listar" : a[0];
  if (sub == "listar" || sub == "status") { listar(carregar()); return 0; }
  if (sub == "adicionar" && a.size() >= 2) {
    string nome;
    int porta = 0;
    for (size_t i = 2; i < a.size(); i++) {
      if (a[i] == "--porta" && i + 1 < a.size()) porta = atoi(a[++i].c_str());
      else if (nome.empty()) nome = a[i];
    }
    Res r = adicionar(a[1], nome, porta);
    cout << r.msg << endl;
    if (!r.chave.empty()) cout << "Chave de administrador deste projeto (guarde; ela fica so em " << canon(a[1]) << "/nodo.conf): " << r.chave << endl;
    return r.ok ? 0 : 1;
  }
  if (sub == "remover" && a.size() >= 2) { Res r = remover(a[1]); cout << r.msg << endl; return r.ok ? 0 : 1; }
  if (sub == "porta" && a.size() >= 3) { Res r = definirPorta(a[1], atoi(a[2].c_str())); cout << r.msg << endl; return r.ok ? 0 : 1; }
  if ((sub == "iniciar" || sub == "parar") && a.size() <= 2) {
    string alvo = a.size() == 2 ? a[1] : "todos";
    vector<string> nomes;
    if (alvo == "todos") { for (auto& it : carregar()) nomes.push_back(it.nome); } else nomes.push_back(alvo);
    if (nomes.empty()) { cout << "Nenhum projeto na lista." << endl; return 1; }
    int falhas = 0;
    for (auto& n : nomes) {
      Res r = sub == "iniciar" ? iniciar(n, self) : parar(n, self);
      cout << (r.ok ? "  [ok] " : "  [erro] ") << r.msg << endl;
      if (!r.ok) falhas++;
    }
    return falhas ? 1 : 0;
  }
  return ajuda();
}

}  // namespace proj
