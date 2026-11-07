// Nodo (linha de comando) - o mesmo servidor do app com janela, para rodar em segundo plano pelo terminal.
// Compilar: g++ -std=c++17 -O2 -pthread nodo.cpp -o nodo
#include "nodo_publicar.hpp"
#include "nodo_projetos.hpp"

static string pidFile() { return cfg.dados + "/nodo.pid"; }
static bool vivo(int p) {
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
static int readPid() {
  string s;
  if (!readFile(pidFile(), s)) return 0;
  int p = atoi(s.c_str());
  return (p > 0 && vivo(p)) ? p : 0;
}
static void matar(int p) {
#ifdef _WIN32
  HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, p);
  if (h) { TerminateProcess(h, 0); CloseHandle(h); }
#else
  kill(p, SIGTERM);
#endif
}
static void cmdStatus() {
  int p = readPid();
  if (!p) { cout << "Nodo: parado. (veja nodo-dados/nodo.log se esperava que estivesse rodando)" << endl; return; }
  cout << "Nodo: rodando (pid " << p << ")\n  Painel:  http://localhost:" << cfg.porta << "/admin\n";
  for (auto& ip : localIps()) cout << "  Na rede: http://" << ip << ":" << cfg.porta << "\n";
  cout << "  Chave:   (arquivo nodo.conf)" << endl;
}
static string pegaNum(map<string, string>& m, const char* k) { return m.count(k) ? m[k] : "?"; }
static int cmdMonitor(bool seguir) {
  if (!readPid()) { cerr << "O Nodo esta parado. Ligue com: nodo iniciar" << endl; return 1; }
  string alvo = (cfg.escutar == "0.0.0.0" ? string("127.0.0.1") : cfg.escutar) + ":" + to_string(cfg.porta);
  while (true) {
    string r;
    if (!http_do(alvo, "GET", "/api/_metricas", "", &r)) { cerr << "Nao consegui ler as metricas em " << alvo << " (chave errada ou servidor ocupado)." << endl; return 1; }
    vector<pair<string, string>> ps;
    if (!jpairs(minify(r), ps)) { cerr << "Resposta inesperada do servidor." << endl; return 1; }
    map<string, string> m(ps.begin(), ps.end());
    vector<pair<string, string>> rep;
    jpairs(minify(m["replicacao"]), rep);
    map<string, string> rp(rep.begin(), rep.end());
    if (seguir) cout << "\033[2J\033[H";
    long up = atol(pegaNum(m, "uptime").c_str());
    cout << "Nodo " << unq(pegaNum(m, "versao")) << " - monitor de uso (atualiza a cada 2 s com --seguir)\n"
         << "  online ha:        " << up / 3600 << " h " << (up % 3600) / 60 << " min\n"
         << "  requisicoes:      " << pegaNum(m, "requisicoes") << "  (leituras " << pegaNum(m, "leituras") << ", escritas " << pegaNum(m, "escritas") << ", arquivos " << pegaNum(m, "estaticos") << ")\n"
         << "  erros:            4xx " << pegaNum(m, "erros4xx") << " | 5xx " << pegaNum(m, "erros5xx") << " | sem resposta " << pegaNum(m, "semResposta") << " | limite 429: " << pegaNum(m, "limitados429") << "\n"
         << "  seguranca:        chaves erradas " << pegaNum(m, "authFalhas") << " | origens recusadas " << pegaNum(m, "origemNegada") << " | logins " << pegaNum(m, "logins") << "\n"
         << "  conexoes:         " << pegaNum(m, "conexoes") << " agora, pico " << pegaNum(m, "picoConexoes") << "\n"
         << "  latencia:         media " << pegaNum(m, "latenciaMediaMs") << " ms, p95 " << pegaNum(m, "latenciaP95Ms") << " ms\n"
         << "  CPU / memoria:    " << pegaNum(m, "cpuPct") << " % / " << pegaNum(m, "memoriaMb") << " MB\n"
         << "  banco:            " << pegaNum(m, "documentos") << " documentos em " << pegaNum(m, "colecoes") << " colecoes, " << humano(strtoull(pegaNum(m, "discoBytes").c_str(), nullptr, 10)) << " em disco\n"
         << "  trafego:          enviado " << humano(strtoull(pegaNum(m, "bytesEnviados").c_str(), nullptr, 10)) << ", recebido " << humano(strtoull(pegaNum(m, "bytesRecebidos").c_str(), nullptr, 10)) << "\n"
         << "  replicacao:       " << pegaNum(rp, "online") << "/" << pegaNum(rp, "pcs") << " PCs online, fila " << pegaNum(rp, "fila") << ", falhas " << pegaNum(rp, "falhas") << ", descartados " << pegaNum(rp, "descartados") << endl;
    if (!seguir) return 0;
    dormirMs(2000);
  }
}
static int serveCli() {
  string e = startServer();
  if (!e.empty()) { cerr << "Erro: " << e << endl; return 1; }
  #ifdef _WIN32
  writeFile(pidFile(), to_string((int)GetCurrentProcessId()));
#else
  writeFile(pidFile(), to_string(getpid()));
#endif
  signal(SIGTERM, [](int) { unlink(pidFile().c_str()); _exit(0); });
  signal(SIGINT, [](int) { unlink(pidFile().c_str()); _exit(0); });
  while (true) dormirMs(3600000);
}
// Liga em segundo plano. Unix: fork duplo. Windows: reinicia o programa sem janela com o comando "rodar".
static void daemonize(const string& pasta, const char* exe) {
#ifdef _WIN32
  (void)pasta;
  SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
  string logp = cfg.dados + "\\nodo.log";
  HANDLE lg = CreateFileA(logp.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  STARTUPINFOA si{};
  si.cb = sizeof si;
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdOutput = lg; si.hStdError = lg; si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  PROCESS_INFORMATION pi{};
  char cwd[MAX_PATH];
  GetCurrentDirectoryA(MAX_PATH, cwd);
  string cmd = "\"" + selfPath(exe) + "\" rodar \"" + string(cwd) + "\"";
  vector<char> cb(cmd.begin(), cmd.end()); cb.push_back(0);
  if (!CreateProcessA(nullptr, cb.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | DETACHED_PROCESS, nullptr, nullptr, &si, &pi)) { cerr << "Nao foi possivel iniciar em segundo plano." << endl; exit(1); }
  CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
  dormirMs(900);
  cmdStatus();
  exit(0);
#else
  (void)pasta; (void)exe;
  pid_t p = fork();
  if (p < 0) { perror("fork"); exit(1); }
  if (p > 0) { dormirMs(900); cmdStatus(); exit(0); }
  setsid();
  if (fork() > 0) _exit(0);
  int fd = open((cfg.dados + "/nodo.log").c_str(), O_WRONLY | O_CREAT | O_APPEND, 0600);
  int nul = open("/dev/null", O_RDONLY);
  dup2(nul, 0);
  dup2(fd, 1);
  dup2(fd, 2);
#endif
}
static int ajuda() {
  cout << "Nodo 3 " << VERSAO << " - server for life\n\n"
       << "  nodo integrar <pasta>   prepara a pasta do projeto (instala tudo nela)\n"
       << "  nodo iniciar  [pasta]   liga em segundo plano\n"
       << "  nodo projetos ...       varios sites ao mesmo tempo, cada um com porta propria (veja: nodo projetos ajuda)\n"
       << "  nodo rodar    [pasta]   liga neste terminal (ver logs)\n"
       << "  nodo parar    [pasta]   desliga\n"
       << "  nodo status   [pasta]   mostra enderecos e estado\n"
       << "  nodo verificar [pasta]  auditoria de seguranca (rode antes de publicar)\n"
       << "  nodo backup   [pasta]   salva uma copia do banco em nodo-dados/backups\n"
       << "  nodo restaurar <arquivo.json> [pasta]   restaura um backup\n"
       << "  nodo chave    [pasta]   gera uma nova chave de administrador\n"
       << "  nodo guia     [pasta]   mostra o guia de integracao com o seu site\n"
       << "  nodo monitor  [pasta] [--seguir]   uso do servidor: requisicoes, erros, CPU, memoria, replicacao\n"
       << "  nodo git vincular <url> [branch]   liga a pasta ao seu repositorio (cria .gitignore e README)\n"
       << "  nodo git enviar [mensagem]         commit + push (nunca envia a chave)\n"
       << "  nodo implantar [pasta]             atualiza o site a partir do Git (deploy)\n"
       << "  nodo tailscale status|publicar|privado|desligar   HTTPS e acesso remoto via Tailscale\n"
       << "  nodo apontar <https://seu.ts.net|local>   aponta o nodo.js do site para o servidor\n"
       << "  nodo readme [pasta]                gera o README.md do projeto\n"
       << "  nodo dominio buscar <nome.com>     (token em VERCEL_TOKEN) preco e disponibilidade\n";
  return 0;
}
int main(int argc, char** argv) {
  redeInit();
#ifndef _WIN32
  signal(SIGPIPE, SIG_IGN);
#endif
  if (argc < 2) return ajuda();
  string cmd = argv[1], pasta = argc > 2 ? argv[2] : ".";
  if (cmd == "projetos") {  // varios sites neste computador (cada um em um processo proprio)
    vector<string> a;
    for (int i = 2; i < argc; i++) a.push_back(argv[i]);
    return proj::cli(a, selfPath(argv[0]));
  }
  if (cmd == "integrar") {
    auto r = integrarPasta(pasta, argv[0], true);
    if (!r.ok) { cerr << r.msg << endl; return 1; }
    cout << "Pasta integrada: " << pasta << "\n  Instalado: " << r.msg << "\n\nPara ligar:  cd " << pasta << " && ./nodo iniciar\n";
    if (r.novo) cout << "Sua chave de administrador (guarde): " << r.chave << "\n";
    return 0;
  }
  if (cmd == "restaurar") { if (argc < 3) return ajuda(); pasta = argc > 3 ? argv[3] : "."; }
  bool seguirMon = false;
  if (cmd == "monitor") { pasta = "."; for (int i = 2; i < argc; i++) { string a = argv[i]; if (a == "--seguir") seguirMon = true; else pasta = a; } }

  if (cmd == "git" || cmd == "tailscale" || cmd == "apontar" || cmd == "dominio") pasta = ".";  // estes usam a pasta atual
  if (cmd != "implantar" && cmd != "readme" && cmd != "git" && cmd != "tailscale" && cmd != "apontar" && cmd != "dominio" && cmd != "iniciar" && cmd != "rodar" && cmd != "parar" && cmd != "status" && cmd != "verificar" && cmd != "backup" && cmd != "restaurar" && cmd != "chave" && cmd != "guia" && cmd != "monitor") return ajuda();
  string arqRest = cmd == "restaurar" ? string(argv[2]) : "";
  string e = openProject(pasta);
  if (e == "NOCONF") { cerr << "nodo.conf ausente ou invalido. Rode antes: nodo integrar " << pasta << endl; return 1; }
  if (!e.empty()) { cerr << e << endl; return 1; }
  deployHook = [] { return pub::deploy(); };
  if (cmd == "status") { cmdStatus(); return 0; }
  if (cmd == "monitor") return cmdMonitor(seguirMon);
  if (cmd == "implantar") { cout << pub::deploy() << endl; return 0; }
  if (cmd == "readme") { cout << pub::gerarReadme(true) << endl; return 0; }
  if (cmd == "apontar") {
    if (argc < 3) return ajuda();
    string a = argv[2];
    cout << pub::apontarSite(a == "local" ? "" : a) << endl;
    if (a != "local") { cfg.tsUrl = a; saveCfg(); }
    return 0;
  }
  if (cmd == "git") {
    string sub = argc > 2 ? argv[2] : "";
    if (sub == "vincular" && argc > 3) { cout << pub::gitVincular(argv[3], argc > 4 ? argv[4] : "main") << endl; return 0; }
    if (sub == "enviar") { cout << pub::gitEnviar(argc > 3 ? argv[3] : "") << endl; return 0; }
    if (sub == "estado") { cout << pub::gitEstado() << endl; return 0; }
    return ajuda();
  }
  if (cmd == "tailscale") {
    string sub = argc > 2 ? argv[2] : "status";
    if (sub == "publicar") { cout << pub::tsPublicar(true) << endl; return 0; }
    if (sub == "privado") { cout << pub::tsPublicar(false) << endl; return 0; }
    if (sub == "desligar") { cout << pub::tsDesligar() << endl; return 0; }
    auto e = pub::tsEstado();
    if (!e.instalado) { cout << "Tailscale nao instalado: https://tailscale.com/download" << endl; return 0; }
    cout << "Estado: " << (e.estado.empty() ? "desconhecido" : e.estado) << (e.logado ? "\nEndereco: " + e.url + (e.funnel ? " (publico, Funnel ligado)" : " (Funnel desligado)") : string("")) << endl;
    return 0;
  }
  if (cmd == "dominio") {
    const char* tk = getenv("VERCEL_TOKEN");
    if (argc < 4 || string(argv[2]) != "buscar" || !tk) { cerr << "Uso: VERCEL_TOKEN=... nodo dominio buscar nome.com" << endl; return 1; }
    auto d = pub::dominioBuscar(tk, argv[3]);
    if (!d.ok) { cout << d.msg << endl; return 1; }
    cout << argv[3] << (d.disponivel ? " esta DISPONIVEL" + (d.preco.empty() ? string("") : " por US$ " + d.preco + " (1 ano)") : string(" NAO esta disponivel")) << (d.msg.empty() ? "" : "\n" + d.msg) << endl;
    return 0;
  }
  if (cmd == "guia") {
    cout << "Guia de integracao (passo a passo para ligar o seu site ao Nodo):\n  1. Ligue o Nodo:  nodo iniciar " << pasta << "\n  2. Abra no navegador:  http://localhost:" << cfg.porta << "/guia\n\nResumo:\n"
         << "  - O nodo.js ja foi adicionado ao seu index.html; use Nodo.colecao('nome').criar/listar.\n"
         << "  - Libere o que visitantes podem fazer no nodo.conf: leitura_publica, escrita_publica, campos.<colecao>.\n"
         << "  - NUNCA coloque a chave no codigo do site. Escape o texto de visitantes (textContent / Nodo.esc).\n"
         << "  - Antes de publicar: nodo verificar  +  HTTPS com Caddy (veja SEGURANCA.md)." << endl;
    return 0;
  }
  if (cmd == "verificar") {
    int x = 0, a = 0;
    for (auto& f : auditoria()) {
      cout << (f.nivel == 'O' ? "  [ok]     " : f.nivel == 'A' ? "  [aviso]  " : "  [ALERTA] ") << f.msg << "\n";
      if (f.nivel == 'X') x++; else if (f.nivel == 'A') a++;
    }
    cout << "\nResultado: " << x << " alerta(s), " << a << " aviso(s). " << (x ? "Corrija os alertas antes de publicar." : "Sem alertas criticos.") << endl;
    return x ? 1 : 0;
  }
  if (cmd == "backup") { loadStore(); string b = backupAgora(); if (b.empty()) { cerr << "Falha ao salvar o backup." << endl; return 1; } cout << "Backup salvo em " << pasta << "/" << b << endl; return 0; }
  if (cmd == "restaurar") {
    if (readPid()) { cerr << "Pare o Nodo antes de restaurar (nodo parar)." << endl; return 1; }
    string txt;
    if (!readFile(arqRest, txt)) { cerr << "Arquivo nao encontrado: " << arqRest << endl; return 1; }
    loadStore();
    int n = storeImport(txt);
    if (n < 0) { cerr << "Arquivo de backup invalido." << endl; return 1; }
    cout << n << " documento(s) restaurado(s)." << endl;
    return 0;
  }
  if (cmd == "chave") {
    cfg.chave = hexrand(48);
    if (!saveCfg()) { cerr << "Nao foi possivel gravar o nodo.conf." << endl; return 1; }
    cout << "Nova chave de administrador (guarde): " << cfg.chave << "\n" << (readPid() ? "Reinicie o Nodo para ela valer: nodo parar && nodo iniciar\n" : "");
    return 0;
  }
  if (cmd == "parar") {
    int p = readPid();
    if (!p) { cout << "Nodo ja esta parado." << endl; return 0; }
    matar(p);
    unlink(pidFile().c_str());
    cout << "Nodo parado." << endl;
    return 0;
  }
  if (readPid()) { cout << "Nodo ja esta rodando." << endl; cmdStatus(); return 0; }
  if (cmd == "iniciar") daemonize(pasta, argv[0]);
  return serveCli();
}
