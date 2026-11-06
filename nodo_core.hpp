#pragma once
// Nodo - nucleo: servidor HTTP + banco de documentos + rede. Usado pelo CLI e pelo app com janela.
#pragma GCC diagnostic ignored "-Wunused-function"
#include "plataforma.hpp"
#ifndef F_OK
#define F_OK 0
#endif
#ifndef X_OK
#define X_OK 1
#endif
#include <algorithm>
#include <array>
#include <cstddef>
#include <atomic>
#include <chrono>
#include <climits>
#include <deque>
#include <functional>
#include <cstdint>
#include <ctime>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <random>
#include <set>
#include <shared_mutex>
#include <sstream>
#include <thread>
#include <vector>
#ifndef _WIN32
#include <sys/resource.h>
#endif
using namespace std;
using nodo_ssize_t = std::ptrdiff_t;

#include "guia_embutido.hpp"
#ifndef NODO_VERSION_STRING
#define NODO_VERSION_STRING "3.0.0"
#endif
static const char* VERSAO = NODO_VERSION_STRING;
static const size_t MAX_BODY = 1024 * 1024;

struct Cfg {
  int porta = 7070;
  string escutar = "127.0.0.1";  // padrao seguro: so este PC. Use 0.0.0.0 apenas na rede local confiavel
  string chave;
  string dados = "nodo-dados";
  set<string> leitura, escrita;
  bool descoberta = false;      // busca automatica de PCs na rede local (desligada por padrao)
  bool proxyConfiavel = false;  // true: atras de Caddy/nginx/Cloudflare (usa X-Forwarded-For/Proto)
  bool adminRemoto = false;     // true: aceita a chave por HTTP puro vindo da internet (NAO recomendado)
  int limiteEscrita = 30;       // escritas publicas por minuto, por IP
  size_t maxDocs = 200000;      // maximo de documentos por colecao
  size_t maxDocBytes = 65536;   // tamanho maximo de um documento enviado por visitante
  int limiteLeitura = 300;      // leituras publicas por minuto, por IP
  map<string, map<string, int>> campos;  // campos.<colecao>=a:80,b,c:500 : unicos campos aceitos de visitantes (e o maximo de caracteres de cada um)
  map<string, size_t> maxDocsCol;         // max_docs.<colecao>=N : limite proprio de documentos por colecao
  set<string> origens;                    // origens=https://meusite.com,https://outro.com : sites autorizados (CORS). Vazio = qualquer site, so em rotas publicas
  bool escritaLivre = false;              // escrita_livre=sim : permite escrita publica em colecao SEM campos.<colecao> (nao recomendado)
  int sessaoHoras = 8;                    // validade do token de sessao do painel
  int syncSegundos = 30;                  // sync_segundos : de quanto em quanto tempo puxa as mudancas dos PCs conectados (min. 5)
  int maxCampoPadrao = 1000;              // maximo de caracteres de um campo de visitante quando nao ha ":N"
  // publicacao (Git, Tailscale, Vercel) - nenhum segredo aqui; tokens nunca sao gravados
  string gitRemoto, gitBranch = "main", tsUrl, dominio, vercelProjeto, vercelTeam;
  set<string> hostsPermitidos;            // hosts_permitidos=meusite.com : nomes (com ponto) aceitos no cabecalho Host em acesso direto (protege contra DNS rebinding)
  vector<string> proxies;                 // proxies_confiaveis=172.16.0.0/12 : alem do loopback, quem pode dizer o IP real (X-Forwarded-For). Ex.: o gateway do Docker
  string escutarArquivo;                  // valor de "escutar" como esta no nodo.conf (a variavel NODO_ESCUTAR so vale em memoria e nunca e gravada)
} cfg;
static shared_mutex regrasMx;  // requisicoes seguram em leitura; recarregar as regras segura em escrita
static function<string()> deployHook;  // definido por nodo.cpp / nodo_gui.cpp (git pull)
static string meuId;
static time_t inicioT = time(0);
static atomic<bool> rodando{false};

// log em memoria (o app com janela mostra isto na aba Registro; o CLI imprime no terminal)
static mutex lmx;
static vector<string> logbuf;
static void logln(const string& s) {
  char t[16];
  time_t n = time(0);
  tm lt{};
#ifdef _WIN32
  localtime_s(&lt, &n);
#else
  localtime_r(&n, &lt);
#endif
  strftime(t, sizeof t, "%H:%M:%S", &lt);
  string l = string(t) + "  " + s;
  {
    lock_guard<mutex> g(lmx);
    logbuf.push_back(l);
    if (logbuf.size() > 600) logbuf.erase(logbuf.begin(), logbuf.begin() + 100);
  }
  cout << l << endl;
}
static vector<string> logCopy() { lock_guard<mutex> g(lmx); return logbuf; }

// ---------------------------------------------------------------- utilidades
static string trim(string s) {
  size_t a = s.find_first_not_of(" \t\r\n");
  if (a == string::npos) return "";
  size_t b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}
static vector<string> split(const string& s, char d) {
  vector<string> o;
  string cur;
  for (char c : s) {
    if (c == d) { o.push_back(cur); cur.clear(); }
    else cur += c;
  }
  o.push_back(cur);
  return o;
}
static string lower(string s) { for (auto& c : s) c = tolower((unsigned char)c); return s; }
// Aleatorio criptografico do sistema operacional (chaves e ids). Se falhar, aborta: nunca gera chave fraca.
static string hexrand(int n) {
  vector<unsigned char> b(n);
  if (!bytesAleatorios(b.data(), b.size())) { cerr << "Falha ao obter aleatorio seguro do sistema." << endl; abort(); }
  const char* h = "0123456789abcdef";
  string s;
  for (int i = 0; i < n; i++) s += h[b[i] & 15];
  return s;
}
static string urldec(const string& s) {
  string o;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '%' && i + 2 < s.size() + 0 && isxdigit((unsigned char)s[i + 1]) && isxdigit((unsigned char)s[i + 2])) {
      o += (char)stoi(s.substr(i + 1, 2), nullptr, 16);
      i += 2;
    } else if (s[i] == '+') o += ' ';
    else o += s[i];
  }
  return o;
}
static string jesc(const string& s) {
  string o;
  for (char c : s) {
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if ((unsigned char)c < 0x20) o += ' ';
    else o += c;
  }
  return o;
}
static bool readFile(const string& p, string& out) {
  ifstream f(fs::u8path(p), ios::binary);
  if (!f) return false;
  ostringstream ss;
  ss << f.rdbuf();
  out = ss.str();
  return true;
}
static bool writeFile(const string& p, const string& c) {
  ofstream f(fs::u8path(p), ios::binary | ios::trunc);
  if (!f) return false;
  f << c;
  return (bool)f;
}

// ---------------------------------------------------------------- mini JSON
static size_t skipWs(const string& s, size_t i) { while (i < s.size() && isspace((unsigned char)s[i])) i++; return i; }
static size_t skipStr(const string& s, size_t i) {
  i++;
  while (i < s.size()) {
    if (s[i] == '\\') i += 2;
    else if (s[i] == '"') return i + 1;
    else i++;
  }
  return string::npos;
}
static size_t skipVal(const string& s, size_t i) {
  i = skipWs(s, i);
  if (i >= s.size()) return string::npos;
  char c = s[i];
  if (c == '"') return skipStr(s, i);
  if (c == '{' || c == '[') {
    int d = 0;
    while (i < s.size()) {
      char x = s[i];
      if (x == '"') { i = skipStr(s, i); if (i == string::npos) return i; continue; }
      if (x == '{' || x == '[') d++;
      else if (x == '}' || x == ']') { d--; if (d == 0) return i + 1; }
      i++;
    }
    return string::npos;
  }
  size_t j = i;
  while (j < s.size() && !strchr(",}] \t\r\n", s[j])) j++;
  return j > i ? j : string::npos;
}
// Le um objeto JSON e devolve seus pares (chave, valor bruto). false se invalido.
static bool jpairs(const string& s, vector<pair<string, string>>& out) {
  size_t i = skipWs(s, 0);
  if (i >= s.size() || s[i] != '{') return false;
  i = skipWs(s, i + 1);
  if (i < s.size() && s[i] == '}') return skipWs(s, i + 1) == s.size();
  while (true) {
    i = skipWs(s, i);
    if (i >= s.size() || s[i] != '"') return false;
    size_t e = skipStr(s, i);
    if (e == string::npos) return false;
    string k = s.substr(i + 1, e - i - 2);
    i = skipWs(s, e);
    if (i >= s.size() || s[i] != ':') return false;
    size_t vs = skipWs(s, i + 1), ve = skipVal(s, vs);
    if (ve == string::npos) return false;
    out.push_back({k, s.substr(vs, ve - vs)});
    i = skipWs(s, ve);
    if (i >= s.size()) return false;
    if (s[i] == ',') { i++; continue; }
    if (s[i] == '}') return skipWs(s, i + 1) == s.size();
    return false;
  }
}
// Remove espacos fora de strings e escapa controles dentro delas (documento em 1 linha).
static string minify(const string& s) {
  string o;
  bool in = false;
  for (size_t i = 0; i < s.size(); i++) {
    char c = s[i];
    if (in) {
      if (c == '\\' && i + 1 < s.size()) { o += c; o += s[++i]; continue; }
      if (c == '"') in = false;
      if (c == '\n') { o += "\\n"; continue; }
      if (c == '\r') { o += "\\r"; continue; }
      if (c == '\t') { o += "\\t"; continue; }
      o += c;
    } else {
      if (isspace((unsigned char)c)) continue;
      if (c == '"') in = true;
      o += c;
    }
  }
  return o;
}
static long long agoraMs() { return chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()).count(); }
// Validador JSON estrito (RFC 8259): rejeita lixo como {"a":tru} ou {"a":{x}} que quebraria o JSON.parse dos sites ao listar a colecao.
static bool jvVal(const string& s, size_t& i, int d);
static bool jvStr(const string& s, size_t& i) {
  i++;
  while (i < s.size()) {
    unsigned char c = s[i];
    if (c == '"') { i++; return true; }
    if (c < 0x20) return false;
    if (c == '\\') {
      if (i + 1 >= s.size()) return false;
      char e = s[i + 1];
      if (e == 'u') {
        if (i + 5 >= s.size()) return false;
        for (int k = 2; k <= 5; k++) if (!isxdigit((unsigned char)s[i + k])) return false;
        i += 6;
      } else if (strchr("\"\\/bfnrt", e) && e) i += 2;
      else return false;
    } else i++;
  }
  return false;
}
static bool jvVal(const string& s, size_t& i, int d) {
  if (d > 32) return false;
  i = skipWs(s, i);
  if (i >= s.size()) return false;
  char c = s[i];
  if (c == '"') return jvStr(s, i);
  if (c == '{') {
    i = skipWs(s, i + 1);
    if (i < s.size() && s[i] == '}') { i++; return true; }
    while (true) {
      i = skipWs(s, i);
      if (i >= s.size() || s[i] != '"' || !jvStr(s, i)) return false;
      i = skipWs(s, i);
      if (i >= s.size() || s[i] != ':') return false;
      i++;
      if (!jvVal(s, i, d + 1)) return false;
      i = skipWs(s, i);
      if (i >= s.size()) return false;
      if (s[i] == ',') { i++; continue; }
      if (s[i] == '}') { i++; return true; }
      return false;
    }
  }
  if (c == '[') {
    i = skipWs(s, i + 1);
    if (i < s.size() && s[i] == ']') { i++; return true; }
    while (true) {
      if (!jvVal(s, i, d + 1)) return false;
      i = skipWs(s, i);
      if (i >= s.size()) return false;
      if (s[i] == ',') { i++; continue; }
      if (s[i] == ']') { i++; return true; }
      return false;
    }
  }
  if (s.compare(i, 4, "true") == 0) { i += 4; return true; }
  if (s.compare(i, 5, "false") == 0) { i += 5; return true; }
  if (s.compare(i, 4, "null") == 0) { i += 4; return true; }
  size_t j = i;
  if (j < s.size() && s[j] == '-') j++;
  if (j >= s.size() || !isdigit((unsigned char)s[j])) return false;
  if (s[j] == '0') j++; else while (j < s.size() && isdigit((unsigned char)s[j])) j++;
  if (j < s.size() && s[j] == '.') { j++; if (j >= s.size() || !isdigit((unsigned char)s[j])) return false; while (j < s.size() && isdigit((unsigned char)s[j])) j++; }
  if (j < s.size() && (s[j] == 'e' || s[j] == 'E')) {
    j++;
    if (j < s.size() && (s[j] == '+' || s[j] == '-')) j++;
    if (j >= s.size() || !isdigit((unsigned char)s[j])) return false;
    while (j < s.size() && isdigit((unsigned char)s[j])) j++;
  }
  i = j;
  return true;
}
static bool jsonValido(const string& s) {
  size_t i = 0;
  if (!jvVal(s, i, 0)) return false;
  return skipWs(s, i) == s.size();
}
// Monta o documento: _id e _t (instante da ultima alteracao, em ms) sao do servidor; o corpo nao consegue forjar nenhum dos dois.
static bool makeDoc(const string& body, const string& id, string& doc, long long t = 0) {
  string m = minify(body);
  vector<pair<string, string>> p;
  if (!jsonValido(m) || !jpairs(m, p)) return false;
  vector<pair<string, string>> u;  // chaves repetidas: vale a ultima
  for (auto& kv : p) {
    if (kv.first == "_id" || kv.first == "_t") continue;
    bool achou = false;
    for (auto& x : u) if (x.first == kv.first) { x.second = kv.second; achou = true; break; }
    if (!achou) u.push_back(kv);
  }
  doc = "{\"_id\":\"" + id + "\",\"_t\":" + to_string(t > 0 ? t : agoraMs());
  for (auto& kv : u) doc += ",\"" + kv.first + "\":" + kv.second;
  doc += "}";
  return true;
}
static string unq(const string& v) { return (v.size() >= 2 && v[0] == '"') ? v.substr(1, v.size() - 2) : v; }

// ---------------------------------------------------------------- SHA-256 (descoberta na rede) e utilidades de seguranca
static string sha256hex(const string& in) {
  static const uint32_t K[64] = {0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
  uint32_t h[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
  string m = in;
  uint64_t bits = (uint64_t)in.size() * 8;
  m += (char)0x80;
  while (m.size() % 64 != 56) m += (char)0;
  for (int i = 7; i >= 0; i--) m += (char)((bits >> (i * 8)) & 0xff);
  auto rr = [](uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };
  for (size_t o = 0; o < m.size(); o += 64) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++) w[i] = ((uint32_t)(uint8_t)m[o + i * 4] << 24) | ((uint32_t)(uint8_t)m[o + i * 4 + 1] << 16) | ((uint32_t)(uint8_t)m[o + i * 4 + 2] << 8) | (uint8_t)m[o + i * 4 + 3];
    for (int i = 16; i < 64; i++) {
      uint32_t s0 = rr(w[i - 15], 7) ^ rr(w[i - 15], 18) ^ (w[i - 15] >> 3), s1 = rr(w[i - 2], 17) ^ rr(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; i++) {
      uint32_t t1 = hh + (rr(e, 6) ^ rr(e, 11) ^ rr(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
      uint32_t t2 = (rr(a, 2) ^ rr(a, 13) ^ rr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
      hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
  }
  char out[65];
  for (int i = 0; i < 8; i++) snprintf(out + i * 8, 9, "%08x", h[i]);
  return out;
}
static bool idOk(const string& id) {
  if (id.empty() || id.size() > 64) return false;
  for (char ch : id) if (!isalnum((unsigned char)ch) && ch != '_' && ch != '-') return false;
  return true;
}
static bool ipLocal(const string& ip) {  // loopback ou rede privada (IPv4)
  if (ip == "::1") return true;
  int a = 0, b = 0;
  if (sscanf(ip.c_str(), "%d.%d", &a, &b) != 2) return false;
  return a == 127 || a == 10 || (a == 100 && b >= 64 && b <= 127) || (a == 192 && b == 168) || (a == 172 && b >= 16 && b <= 31) || (a == 169 && b == 254);
}
static bool ipv4Num(const string& s, unsigned& out) { in_addr a; if (inet_pton(AF_INET, s.c_str(), &a) != 1) return false; out = ntohl(a.s_addr); return true; }
// "proxies_confiaveis": enderecos ou faixas CIDR IPv4 (ex.: 172.17.0.1 ou 172.16.0.0/12). Faixas largas demais (menos de /8) sao ignoradas.
static bool proxyListado(const string& ip) {
  unsigned x;
  if (cfg.proxies.empty() || !ipv4Num(ip, x)) return false;
  for (auto& e : cfg.proxies) {
    size_t b = e.find('/');
    int bits = 32;
    if (b != string::npos) { bits = atoi(e.c_str() + b + 1); if (bits < 8 || bits > 32) continue; }
    unsigned y;
    if (!ipv4Num(e.substr(0, b), y)) continue;
    unsigned mask = bits == 0 ? 0 : (0xFFFFFFFFu << (32 - bits));
    if ((x & mask) == (y & mask)) return true;
  }
  return false;
}
static bool ehProxy(const string& ip) { return cfg.proxyConfiavel && (ip == "127.0.0.1" || ip == "::1" || proxyListado(ip)); }
// limitador por janela deslizante: devolve false se passou do maximo (e nao registra); true registra o evento
static mutex rlmx;
static map<string, deque<time_t>> rlmap;
static size_t rlConta(const string& k, int janela) {
  auto it = rlmap.find(k);
  if (it == rlmap.end()) return 0;
  time_t n = time(0);
  while (!it->second.empty() && n - it->second.front() >= janela) it->second.pop_front();
  return it->second.size();
}
static bool rlPermite(const string& k, int maxN, int janela) {
  lock_guard<mutex> l(rlmx);
  if (rlmap.size() > 20000) rlmap.clear();
  if ((int)rlConta(k, janela) >= maxN) return false;
  rlmap[k].push_back(time(0));
  return true;
}
static void rlRegistra(const string& k) { lock_guard<mutex> l(rlmx); rlmap[k].push_back(time(0)); }
static bool rlBloqueado(const string& k, int maxN, int janela) { lock_guard<mutex> l(rlmx); return (int)rlConta(k, janela) >= maxN; }
static bool writePrivado(const string& p, const string& c) {
#ifndef _WIN32
  int fd = open(p.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (fd < 0) return false;
  size_t o = 0;
  while (o < c.size()) { nodo_ssize_t n = write(fd, c.data() + o, c.size() - o); if (n <= 0) { cerrar(fd); return false; } o += n; }
  cerrar(fd);
  nodoChmod(p.c_str(), 0600);
  return true;
#else
  return writeFile(p, c);
#endif
}

// ---------------------------------------------------------------- cliente HTTP (rede entre PCs)
static bool peerValido(const string& hp) {  // host:porta, so letras, numeros, ponto, hifen e underline
  size_t p = hp.rfind(':');
  if (p == string::npos || p == 0 || hp.size() > 255) return false;
  string h = hp.substr(0, p), pt = hp.substr(p + 1);
  if (pt.empty() || pt.size() > 5) return false;
  for (char ch : pt) if (!isdigit((unsigned char)ch)) return false;
  int n = atoi(pt.c_str());
  if (n < 1 || n > 65535) return false;
  for (char ch : h) if (!isalnum((unsigned char)ch) && ch != '.' && ch != '-' && ch != '_') return false;
  return true;
}
static bool http_do(const string& hp, const string& method, const string& path, const string& body, string* resp, int tmo = 3) {
  redeInit();
  if (!peerValido(hp)) return false;
  size_t p = hp.rfind(':');
  addrinfo hints{}, *res = nullptr;
  hints.ai_socktype = SOCK_STREAM;
  if (getaddrinfo(hp.substr(0, p).c_str(), hp.substr(p + 1).c_str(), &hints, &res) != 0 || !res) return false;
  int fd = socket(res->ai_family, SOCK_STREAM, 0);
  if (fd < 0) { freeaddrinfo(res); return false; }
  sockTimeout(fd, tmo);
  if (connect(fd, res->ai_addr, res->ai_addrlen) != 0) { cerrar(fd); freeaddrinfo(res); return false; }
  freeaddrinfo(res);
  string rq = method + " " + path + " HTTP/1.1\r\nHost: " + hp + "\r\nX-Nodo-Key: " + cfg.chave + "\r\nX-Nodo-Id: " + meuId + "\r\nX-Nodo-Porta: " + to_string(cfg.porta) +
              "\r\nContent-Type: text/plain\r\nContent-Length: " + to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
  if (send(fd, rq.data(), rq.size(), 0) < 0) { cerrar(fd); return false; }
  string all;
  char buf[8192];
  nodo_ssize_t n;
  while ((n = recv(fd, buf, sizeof buf, 0)) > 0 && all.size() < 64u * 1024 * 1024) all.append(buf, n);
  cerrar(fd);
  bool ok = all.size() > 12 && all.compare(0, 5, "HTTP/") == 0 && all[9] == '2';
  if (resp) { size_t h = all.find("\r\n\r\n"); *resp = h == string::npos ? "" : all.substr(h + 4); }
  return ok;
}

// ---------------------------------------------------------------- banco de dados (colecoes de documentos JSON)
static mutex smx;
static map<string, map<string, string>> cols;
static map<string, map<string, long long>> tomb;  // documentos apagados (id -> instante), para a replicacao nao ressuscita-los
static mutex pmx;
static set<string> peers;
static map<string, int> peerFalhas;         // falhas seguidas por PC (sob pmx); com 3 ou mais o PC fica "fora" ate a proxima sincronizacao
static map<string, long long> peerSync;     // ultima sincronizacao completa por PC (sob pmx)

static string newId() {
  char t[20];
  snprintf(t, sizeof t, "%011llx", (unsigned long long)time(0) * 1000ULL);
  return string(t) + hexrand(6);
}
static bool colOk(const string& c) {
  if (c.empty() || c.size() > 64 || c[0] == '_') return false;
  for (char ch : c) if (!isalnum((unsigned char)ch) && ch != '_' && ch != '-') return false;
  return true;
}
static bool campoNomeOk(const string& c) {  // nomes de campo/filtro: letras, numeros, _ - e .
  if (c.empty() || c.size() > 64) return false;
  for (char ch : c) if (!isalnum((unsigned char)ch) && ch != '_' && ch != '-' && ch != '.') return false;
  return true;
}
static size_t charsUtf8(const string& s) { size_t n = 0; for (unsigned char c : s) if ((c & 0xC0) != 0x80) n++; return n; }
static string curto(const string& s, size_t n = 40) {  // trecho seguro para mensagens de erro
  string o;
  for (char c : s) { if (o.size() >= n) { o += "..."; break; } o += ((unsigned char)c < 0x20 || c == '"' || c == '\\') ? '?' : c; }
  return o;
}
static string jdecode(const string& s);
static bool camposOk(const string& col, const string& body, string& erro) {
  auto it = cfg.campos.find(col);
  if (it == cfg.campos.end()) return true;
  string m = minify(body);
  vector<pair<string, string>> ps;
  if (!jsonValido(m) || !jpairs(m, ps)) { erro = "corpo precisa ser um objeto JSON valido"; return false; }
  set<string> vistos;
  for (auto& kv : ps) {
    auto f = it->second.find(kv.first);
    if (f == it->second.end()) { erro = "campo nao permitido nesta colecao: " + curto(kv.first); return false; }
    if (!vistos.insert(kv.first).second) { erro = "campo repetido: " + curto(kv.first); return false; }
    const string& v = kv.second;
    if (v.empty()) { erro = "valor vazio em " + curto(kv.first); return false; }
    if (v[0] == '{' || v[0] == '[') { erro = "valores aninhados nao sao permitidos"; return false; }
    if (v[0] == '"') {
      size_t n = charsUtf8(jdecode(v.substr(1, v.size() - 2)));  // conta caracteres reais, nao os bytes nem os escapes \\uXXXX
      if (n > (size_t)f->second) { erro = "o campo '" + curto(kv.first) + "' passou do limite de " + to_string(f->second) + " caracteres"; return false; }
    } else if (v.size() > 40) { erro = "numero longo demais em " + curto(kv.first); return false; }
  }
  return true;
}
static void logAppend(const string& col, const string& line) {
  ofstream f(fs::u8path(cfg.dados + "/" + col + ".log"), ios::app);
  f << line << "\n";
}
static long long docT(const string& json) {  // _t do documento (sempre o 2o campo, escrito pelo servidor); 0 se nao houver
  size_t p = json.find("\"_t\":");
  if (p == string::npos || p > 160) return 0;
  return atoll(json.c_str() + p + 5);
}

// ---- fila de replicacao: uma unica thread envia, em ordem, aos PCs conectados (nada de uma thread por escrita)
static mutex qmx;
static deque<string> outq;
static atomic<unsigned long long> replEnviados{0}, replFalhas{0}, replDescartados{0};
static void replicar(const string& line) {
  lock_guard<mutex> l(qmx);
  if (outq.size() >= 5000) { outq.pop_front(); replDescartados++; }  // a sincronizacao periodica recupera o que for descartado
  outq.push_back(line);
}
static void replWorker() {
  while (true) {
    string line;
    { lock_guard<mutex> l(qmx); if (!outq.empty()) { line = outq.front(); outq.pop_front(); } }
    if (line.empty() || !rodando) { dormirMs(line.empty() ? 150 : 500); if (!line.empty()) { lock_guard<mutex> l(qmx); outq.push_front(line); } continue; }
    vector<string> ps;
    { lock_guard<mutex> l(pmx); for (auto& p : peers) if (peerFalhas[p] < 3) ps.push_back(p); }
    for (auto& p : ps) {
      bool ok = http_do(p, "POST", "/api/_replicar", line, nullptr);
      lock_guard<mutex> l(pmx);
      if (ok) { peerFalhas[p] = 0; replEnviados++; }
      else { replFalhas++; if (++peerFalhas[p] == 3) logln("[rede] PC sem resposta: " + p + " (sera ressincronizado quando voltar)"); }
    }
  }
}

enum { SA_OK = 0, SA_ANTIGO = 1, SA_CHEIO = 2 };
// Grava um documento. lww=true (vindo de outro PC): so vale se for mais novo que o local e que o registro de apagado.
// maxDocs>0: recusa documento NOVO se a colecao ja esta cheia (a checagem e a gravacao acontecem sob o mesmo lock).
static int storeApply(const string& col, const string& id, const string& json, bool rep, bool lww, size_t maxDocs) {
  {
    lock_guard<mutex> l(smx);
    long long t = docT(json);
    auto ci = cols.find(col);
    bool existe = ci != cols.end() && ci->second.count(id);
    if (lww) {
      auto tc = tomb.find(col);
      if (tc != tomb.end()) { auto tt = tc->second.find(id); if (tt != tc->second.end() && tt->second >= t) return SA_ANTIGO; }
      if (existe && docT(ci->second[id]) >= t) return SA_ANTIGO;
    }
    if (!existe && maxDocs && ci != cols.end() && ci->second.size() >= maxDocs) return SA_CHEIO;
    cols[col][id] = json;
    auto tc = tomb.find(col);
    if (tc != tomb.end()) tc->second.erase(id);
    logAppend(col, "P\t" + id + "\t" + json);
  }
  if (rep) replicar("P\t" + col + "\t" + id + "\t" + json);
  return SA_OK;
}
static void storePut(const string& col, const string& id, const string& json, bool rep) { storeApply(col, id, json, rep, false, 0); }
static bool storeDel(const string& col, const string& id, bool rep, long long t = 0, bool lww = false) {
  long long tt = t > 0 ? t : agoraMs();
  {
    lock_guard<mutex> l(smx);
    auto it = cols.find(col);
    bool existe = it != cols.end() && it->second.count(id);
    if (lww) {
      if (existe && docT(it->second[id]) > tt) return false;
      auto tc = tomb.find(col);
      if (tc != tomb.end()) { auto x = tc->second.find(id); if (x != tc->second.end() && x->second >= tt) return false; }
    } else if (!existe) return false;
    if (existe) it->second.erase(id);
    tomb[col][id] = tt;
    logAppend(col, "D\t" + id + "\t" + to_string(tt));
  }
  if (rep) replicar("D\t" + col + "\t" + id + "\t" + to_string(tt));
  return true;
}
static bool storeGet(const string& col, const string& id, string& out) {
  lock_guard<mutex> l(smx);
  auto it = cols.find(col);
  if (it == cols.end()) return false;
  auto d = it->second.find(id);
  if (d == it->second.end()) return false;
  out = d->second;
  return true;
}
static void loadStore() {
  { lock_guard<mutex> l(smx); cols.clear(); tomb.clear(); }
  vector<string> files;
  error_code ec;
  for (auto& e : fs::directory_iterator(fs::u8path(cfg.dados), ec)) {
    string n = e.path().filename().u8string();
    if (n.size() > 4 && n.substr(n.size() - 4) == ".log" && colOk(n.substr(0, n.size() - 4))) files.push_back(n.substr(0, n.size() - 4));
  }
  long long agora = agoraMs(), validade = 30LL * 86400000LL;  // registros de apagado valem 30 dias
  size_t ruins = 0, migrados = 0;
  lock_guard<mutex> l(smx);
  for (auto& col : files) {
    ifstream f(fs::u8path(cfg.dados + "/" + col + ".log"));
    string line;
    auto& m = cols[col];
    auto& tb = tomb[col];
    while (getline(f, line)) {
      auto p = split(line, '\t');
      if (p.size() >= 3 && p[0] == "P" && idOk(p[1])) {
        string json = line.substr(line.find('\t', 2) + 1);
        string doc;
        long long t = docT(json);
        if (!makeDoc(json, p[1], doc, t > 0 ? t : 1)) { ruins++; continue; }  // documento sem _t (versao antiga) ganha _t=1; JSON invalido e descartado
        if (t <= 0) migrados++;
        m[p[1]] = doc;
        tb.erase(p[1]);
      } else if (p.size() >= 2 && p[0] == "D" && idOk(p[1])) {
        long long t = p.size() >= 3 ? atoll(p[2].c_str()) : agora;
        m.erase(p[1]);
        tb[p[1]] = t;
      }
    }
    f.close();
    for (auto it = tb.begin(); it != tb.end();) { if (agora - it->second > validade) it = tb.erase(it); else ++it; }
    // compactacao: reescreve o log so com o estado atual (e os registros de apagado ainda validos)
    string tmp = cfg.dados + "/" + col + ".tmp";
    {
      ofstream o(fs::u8path(tmp), ios::trunc);
      for (auto& kv : m) o << "P\t" << kv.first << "\t" << kv.second << "\n";
      for (auto& kv : tb) o << "D\t" << kv.first << "\t" << kv.second << "\n";
    }
    error_code e2;
    fs::rename(fs::u8path(tmp), fs::u8path(cfg.dados + "/" + col + ".log"), e2);
    if (e2) { fs::remove(fs::u8path(cfg.dados + "/" + col + ".log"), e2); fs::rename(fs::u8path(tmp), fs::u8path(cfg.dados + "/" + col + ".log"), e2); }
  }
  if (ruins) logln("[banco] " + to_string(ruins) + " documento(s) com JSON invalido ignorado(s) ao carregar");
  if (migrados) logln("[banco] " + to_string(migrados) + " documento(s) de versao antiga receberam o campo _t");
}
// Aplica uma linha vinda de outro PC (formato "P\tcolecao\tid\tjson" ou "D\tcolecao\tid\tinstante"). true se mudou algo.
static bool applyLine(const string& line) {
  auto f = split(line, '\t');
  if (f.size() < 3 || !colOk(f[1]) || !idOk(f[2])) return false;
  long long agora = agoraMs();
  if (f[0] == "P" && f.size() >= 4) {
    size_t p3 = line.find('\t', line.find('\t', line.find('\t') + 1) + 1);
    string json = line.substr(p3 + 1);
    if (json.size() > MAX_BODY || !jsonValido(json)) return false;
    long long t = docT(json);
    if (t > agora + 86400000LL) t = agora;  // relogio de outro PC no futuro: nao deixa um documento "imbativel"
    string doc;
    if (!makeDoc(json, f[2], doc, t > 0 ? t : 1)) return false;
    return storeApply(f[1], f[2], doc, false, true, 0) == SA_OK;
  }
  if (f[0] == "D") {
    long long t = f.size() >= 4 ? atoll(f[3].c_str()) : agora;
    if (t > agora + 86400000LL || t <= 0) t = agora;
    return storeDel(f[1], f[2], false, t, true);
  }
  return false;
}

// ---------------------------------------------------------------- rede: pares, sincronizacao e descoberta na LAN
// Traz do outro PC tudo o que mudou desde a ultima sincronizacao (a primeira e completa). Conflitos: vence a alteracao mais recente.
static bool syncFrom(const string& hp) {
  long long desde = 0, inicio = agoraMs();
  { lock_guard<mutex> l(pmx); auto it = peerSync.find(hp); if (it != peerSync.end()) desde = max(0LL, it->second - 300000); }  // 5 min de folga para relogios diferentes
  string r;
  if (!http_do(hp, "GET", "/api/_dump?desde=" + to_string(desde), "", &r, 30)) return false;
  istringstream in(r);
  string l;
  int n = 0;
  while (getline(in, l)) if (applyLine(l)) n++;
  { lock_guard<mutex> k(pmx); peerSync[hp] = inicio; peerFalhas[hp] = 0; }
  if (n) logln("[rede] " + to_string(n) + " alteracao(oes) sincronizada(s) de " + hp);
  return true;
}
static void salvarPeers() {  // nodo-dados/peers.txt: os PCs conectados voltam sozinhos quando o servidor reinicia
  string o;
  { lock_guard<mutex> l(pmx); for (auto& p : peers) o += p + "\n"; }
  writePrivado(cfg.dados + "/peers.txt", o);
}
static void addPeer(const string& hp, bool sync) {
  if (!peerValido(hp)) return;
  {
    lock_guard<mutex> l(pmx);
    if (peers.size() >= 64 || !peers.insert(hp).second) return;
    peerFalhas[hp] = 0;
  }
  salvarPeers();
  logln("[rede] PC conectado: " + hp);
  if (sync) thread([hp] { syncFrom(hp); }).detach();
}
// De tempos em tempos puxa as mudancas de cada PC conectado: recupera o que se perdeu com a rede fora do ar e fecha a conexao nos dois sentidos.
static void antiEntropia() {
  int tick = 0;
  while (true) {
    dormirMs(1000);
    if (!rodando || ++tick < cfg.syncSegundos) continue;
    tick = 0;
    vector<string> ps;
    { lock_guard<mutex> l(pmx); ps.assign(peers.begin(), peers.end()); }
    for (auto& p : ps) if (rodando) syncFrom(p);
  }
}
static string keyHash() { return sha256hex("nodo-descoberta-v1|" + cfg.chave).substr(0, 32); }
static void discovery() {
  redeInit();
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  setInt(fd, SOL_SOCKET, SO_REUSEADDR, 1);
#ifdef SO_REUSEPORT
  setInt(fd, SOL_SOCKET, SO_REUSEPORT, 1);
#endif
  setInt(fd, SOL_SOCKET, SO_BROADCAST, 1);
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_port = htons(7071);
  a.sin_addr.s_addr = INADDR_ANY;
  if (::bind(fd, (sockaddr*)&a, sizeof a) < 0) { logln("[rede] descoberta automatica indisponivel (porta 7071 ocupada); conecte PCs pelo endereco"); return; }
  thread([fd] {
    sockaddr_in b{};
    b.sin_family = AF_INET;
    b.sin_port = htons(7071);
    b.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    while (true) {
      if (rodando && cfg.descoberta) {
        string msg = "NODO1|" + to_string(cfg.porta) + "|" + keyHash() + "|" + meuId;
        sendto(fd, msg.data(), msg.size(), 0, (sockaddr*)&b, sizeof b);
      }
      dormirMs(5000);
    }
  }).detach();
  char buf[256];
  while (true) {
    sockaddr_in from{};
    socklen_t fl = sizeof from;
    nodo_ssize_t n = recvfrom(fd, buf, 255, 0, (sockaddr*)&from, &fl);
    if (n <= 0) continue;
    buf[n] = 0;
    auto f = split(buf, '|');
    if (rodando && cfg.descoberta && f.size() == 4 && f[0] == "NODO1" && f[2] == keyHash() && f[3] != meuId) {
      char ip[64];
      inet_ntop(AF_INET, &from.sin_addr, ip, sizeof ip);
      int pt = atoi(f[1].c_str());
      if (ipLocal(ip) && pt > 0 && pt < 65536) addPeer(string(ip) + ":" + to_string(pt), true);
    }
  }
}
static vector<string> localIps() {
  vector<string> o;
#ifdef _WIN32
  ULONG sz = 16384;
  vector<char> buf(sz);
  auto* a = (IP_ADAPTER_ADDRESSES*)buf.data();
  ULONG fl = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
  if (GetAdaptersAddresses(AF_INET, fl, nullptr, a, &sz) == ERROR_BUFFER_OVERFLOW) { buf.resize(sz); a = (IP_ADAPTER_ADDRESSES*)buf.data(); }
  if (GetAdaptersAddresses(AF_INET, fl, nullptr, a, &sz) != NO_ERROR) return o;
  for (; a; a = a->Next) {
    if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
    for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
      char ip[64];
      inet_ntop(AF_INET, &((sockaddr_in*)u->Address.lpSockaddr)->sin_addr, ip, sizeof ip);
      if (strncmp(ip, "127.", 4) != 0 && strncmp(ip, "169.254.", 8) != 0) o.push_back(ip);
    }
  }
#else
  ifaddrs *ifa = nullptr, *p;
  if (getifaddrs(&ifa) != 0) return o;
  for (p = ifa; p; p = p->ifa_next) {
    if (!p->ifa_addr || p->ifa_addr->sa_family != AF_INET) continue;
    char ip[64];
    inet_ntop(AF_INET, &((sockaddr_in*)p->ifa_addr)->sin_addr, ip, sizeof ip);
    if (strncmp(ip, "127.", 4) != 0) o.push_back(ip);
  }
  freeifaddrs(ifa);
#endif
  return o;
}

// ---------------------------------------------------------------- monitor de uso (tudo em memoria; nada disto vai para o disco)
#ifdef _WIN32
#include <psapi.h>
#endif
typedef unsigned long long ull;
struct MinBucket { time_t min = 0; unsigned reqs = 0, erros = 0; };
static mutex mmx;
static deque<MinBucket> mBuckets;                 // ultimos 60 minutos
static atomic<ull> mReqs{0}, mBytesIn{0}, mBytesOut{0}, mAuthFalhas{0}, mLimitados{0}, mE4{0}, mE5{0}, mLeituras{0}, mEscritas{0}, mEstaticos{0}, mSemResposta{0}, mLogins{0}, mOrigemNegada{0};
static atomic<int> mPicoCon{0};
static map<string, array<ull, 2>> mCols;          // colecao -> {leituras, escritas}
static map<string, ull> mIps;                     // IP -> requisicoes (limitado)
static map<int, ull> mStatus;                     // codigo HTTP -> quantidade
static float mLat[512];
static size_t mLatN = 0;
static thread_local int tlStatus = 0;
static thread_local size_t tlBytesOut = 0;
static thread_local string tlCol, tlIp, tlExtra, tlOrigem;
static thread_local char tlTipo = 'o';            // l = leitura da API, e = escrita da API, s = arquivo estatico, o = outros
static thread_local bool tlCoringa = false, tlHttps = false, tlCorsPriv = false;

static void metricaRegistra(const string& ip, int status, size_t bin, size_t bout, double ms, const string& col, char tipo) {
  mReqs++;
  mBytesIn += bin;
  mBytesOut += bout;
  if (status == 0) mSemResposta++;
  else if (status >= 500) mE5++;
  else if (status >= 400) mE4++;
  if (status == 429) mLimitados++;
  if (status > 0 && status < 400) { if (tipo == 'l') mLeituras++; else if (tipo == 'e') mEscritas++; else if (tipo == 's') mEstaticos++; }
  time_t m = time(0) / 60;
  lock_guard<mutex> l(mmx);
  if (mBuckets.empty() || mBuckets.back().min != m) {
    mBuckets.push_back(MinBucket{m, 0, 0});
    while (mBuckets.size() > 60) mBuckets.pop_front();
  }
  mBuckets.back().reqs++;
  if (status == 0 || status >= 400) mBuckets.back().erros++;
  mStatus[status]++;
  if (!col.empty() && status > 0 && status < 400 && (tipo == 'l' || tipo == 'e')) mCols[col][tipo == 'l' ? 0 : 1]++;
  if (!ip.empty()) { if (mIps.size() > 2000 && !mIps.count(ip)) mIps.clear(); mIps[ip]++; }
  mLat[mLatN++ % 512] = (float)ms;
}

static bool dirSizeCache(ull& out) {  // tamanho de nodo-dados/ (recalculado no maximo a cada 15 s)
  static mutex m;
  static time_t quando = 0;
  static ull val = 0;
  lock_guard<mutex> l(m);
  if (time(0) - quando >= 15) {
    ull t = 0;
    error_code ec;
    for (auto it = fs::recursive_directory_iterator(fs::u8path(cfg.dados), ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
      error_code e2;
      if (it->is_regular_file(e2)) { auto z = it->file_size(e2); if (!e2) t += z; }
    }
    val = t;
    quando = time(0);
  }
  out = val;
  return true;
}
// CPU (% de um nucleo) e memoria deste processo. -1 = indisponivel nesta plataforma.
static void procRecursos(double& cpu, double& rssMb) {
  static mutex m;
  static double cpuAnt = -1, tAnt = 0;
  double cpuAgora = -1;
  rssMb = -1;
#ifdef _WIN32
  FILETIME c, e, k, u;
  if (GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u)) {
    ULARGE_INTEGER a, b;
    a.LowPart = k.dwLowDateTime; a.HighPart = k.dwHighDateTime;
    b.LowPart = u.dwLowDateTime; b.HighPart = u.dwHighDateTime;
    cpuAgora = (double)(a.QuadPart + b.QuadPart) / 1e7;
  }
  PROCESS_MEMORY_COUNTERS pmc;
  if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof pmc)) rssMb = pmc.WorkingSetSize / 1048576.0;
#else
  rusage ru{};
  if (getrusage(RUSAGE_SELF, &ru) == 0) cpuAgora = ru.ru_utime.tv_sec + ru.ru_utime.tv_usec / 1e6 + ru.ru_stime.tv_sec + ru.ru_stime.tv_usec / 1e6;
#ifdef __linux__
  { ifstream f("/proc/self/statm"); long a = 0, b = 0; if (f >> a >> b) rssMb = b * (double)sysconf(_SC_PAGESIZE) / 1048576.0; }
#else
  if (ru.ru_maxrss > 0) rssMb = ru.ru_maxrss / 1048576.0;  // macOS: bytes (pico de uso)
#endif
#endif
  double agora = chrono::duration<double>(chrono::steady_clock::now().time_since_epoch()).count();
  static double ultCpu = -1;
  lock_guard<mutex> l(m);
  cpu = -1;
  if (cpuAgora < 0) return;
  if (cpuAnt < 0) { cpuAnt = cpuAgora; tAnt = agora; return; }
  double dt = agora - tAnt;
  if (dt >= 2.0) { ultCpu = max(0.0, (cpuAgora - cpuAnt) / dt * 100.0); cpuAnt = cpuAgora; tAnt = agora; }  // media dos ultimos ~2 s ou mais
  cpu = ultCpu;
}
struct MetSnap {
  ull reqs = 0, bytesIn = 0, bytesOut = 0, authFalhas = 0, limitados = 0, e4 = 0, e5 = 0, leituras = 0, escritas = 0, estaticos = 0, semResposta = 0, logins = 0, origemNegada = 0;
  ull replEnv = 0, replFalh = 0, replDesc = 0, disco = 0;
  int conexoes = 0, pico = 0, sessoes = 0;
  long uptime = 0;
  double cpu = -1, rssMb = -1, latMed = 0, latP95 = 0;
  size_t nCols = 0, nDocs = 0, fila = 0, peersTotal = 0, peersOnline = 0;
  vector<unsigned> serieReq, serieErr;                 // 60 minutos, do mais antigo para o mais novo
  vector<pair<string, array<ull, 3>>> colunas;         // colecao -> {documentos, leituras, escritas}
  vector<pair<string, ull>> ips;                       // os 10 que mais pediram
  vector<pair<int, ull>> status;
};
static atomic<int> conexoes{0};
static mutex sesmx;
struct Sessao { time_t exp; string ip; };
static map<string, Sessao> sessoes;  // sha256(token) -> sessao: o token em si nunca fica guardado
static MetSnap metricasSnap() {
  MetSnap m;
  m.reqs = mReqs; m.bytesIn = mBytesIn; m.bytesOut = mBytesOut; m.authFalhas = mAuthFalhas; m.limitados = mLimitados; m.e4 = mE4; m.e5 = mE5;
  m.leituras = mLeituras; m.escritas = mEscritas; m.estaticos = mEstaticos; m.semResposta = mSemResposta; m.logins = mLogins; m.origemNegada = mOrigemNegada;
  m.replEnv = replEnviados; m.replFalh = replFalhas; m.replDesc = replDescartados;
  m.conexoes = conexoes; m.pico = mPicoCon;
  m.uptime = (long)(time(0) - inicioT);
  procRecursos(m.cpu, m.rssMb);
  dirSizeCache(m.disco);
  { lock_guard<mutex> l(qmx); m.fila = outq.size(); }
  { lock_guard<mutex> l(sesmx); time_t n = time(0); for (auto& kv : sessoes) if (kv.second.exp > n) m.sessoes++; }
  { lock_guard<mutex> l(pmx); m.peersTotal = peers.size(); for (auto& p : peers) if (peerFalhas[p] < 3) m.peersOnline++; }
  map<string, array<ull, 3>> cc;
  { lock_guard<mutex> l(smx); m.nCols = cols.size(); for (auto& kv : cols) { m.nDocs += kv.second.size(); cc[kv.first][0] = kv.second.size(); } }
  {
    lock_guard<mutex> l(mmx);
    time_t agora = time(0) / 60;
    m.serieReq.assign(60, 0);
    m.serieErr.assign(60, 0);
    for (auto& b : mBuckets) { long idx = 59 - (long)(agora - b.min); if (idx >= 0 && idx < 60) { m.serieReq[idx] = b.reqs; m.serieErr[idx] = b.erros; } }
    for (auto& kv : mCols) { cc[kv.first][1] = kv.second[0]; cc[kv.first][2] = kv.second[1]; }
    vector<pair<string, ull>> ips(mIps.begin(), mIps.end());
    sort(ips.begin(), ips.end(), [](const pair<string, ull>& a, const pair<string, ull>& b) { return a.second > b.second; });
    if (ips.size() > 10) ips.resize(10);
    m.ips = ips;
    for (auto& kv : mStatus) m.status.push_back(kv);
    size_t n = min<size_t>(mLatN, 512);
    if (n) {
      vector<float> v(mLat, mLat + n);
      double soma = 0;
      for (float x : v) soma += x;
      m.latMed = soma / n;
      sort(v.begin(), v.end());
      m.latP95 = v[min(n - 1, (size_t)(n * 0.95))];
    }
  }
  for (auto& kv : cc) m.colunas.push_back(kv);
  sort(m.colunas.begin(), m.colunas.end(), [](const pair<string, array<ull, 3>>& a, const pair<string, array<ull, 3>>& b) { return a.second[1] + a.second[2] > b.second[1] + b.second[2]; });
  return m;
}
static string fnum(double v) { char b[32]; snprintf(b, sizeof b, "%.2f", v); return b; }
static string metricasJson(const MetSnap& m) {
  ostringstream o;
  o << "{\"versao\":\"" << VERSAO << "\",\"uptime\":" << m.uptime << ",\"requisicoes\":" << m.reqs << ",\"leituras\":" << m.leituras << ",\"escritas\":" << m.escritas << ",\"estaticos\":" << m.estaticos
    << ",\"erros4xx\":" << m.e4 << ",\"erros5xx\":" << m.e5 << ",\"semResposta\":" << m.semResposta << ",\"limitados429\":" << m.limitados << ",\"authFalhas\":" << m.authFalhas
    << ",\"logins\":" << m.logins << ",\"origemNegada\":" << m.origemNegada << ",\"sessoes\":" << m.sessoes << ",\"bytesRecebidos\":" << m.bytesIn << ",\"bytesEnviados\":" << m.bytesOut
    << ",\"conexoes\":" << m.conexoes << ",\"picoConexoes\":" << m.pico << ",\"cpuPct\":" << fnum(m.cpu) << ",\"memoriaMb\":" << fnum(m.rssMb) << ",\"discoBytes\":" << m.disco
    << ",\"latenciaMediaMs\":" << fnum(m.latMed) << ",\"latenciaP95Ms\":" << fnum(m.latP95) << ",\"colecoes\":" << m.nCols << ",\"documentos\":" << m.nDocs
    << ",\"replicacao\":{\"pcs\":" << m.peersTotal << ",\"online\":" << m.peersOnline << ",\"fila\":" << m.fila << ",\"enviados\":" << m.replEnv << ",\"falhas\":" << m.replFalh << ",\"descartados\":" << m.replDesc << "}";
  o << ",\"porMinuto\":[";
  for (size_t i = 0; i < m.serieReq.size(); i++) o << (i ? "," : "") << m.serieReq[i];
  o << "],\"errosPorMinuto\":[";
  for (size_t i = 0; i < m.serieErr.size(); i++) o << (i ? "," : "") << m.serieErr[i];
  o << "],\"porColecao\":{";
  bool f = true;
  for (auto& c : m.colunas) { o << (f ? "" : ",") << "\"" << jesc(c.first) << "\":{\"documentos\":" << c.second[0] << ",\"leituras\":" << c.second[1] << ",\"escritas\":" << c.second[2] << "}"; f = false; }
  o << "},\"topIps\":[";
  for (size_t i = 0; i < m.ips.size(); i++) o << (i ? "," : "") << "{\"ip\":\"" << jesc(m.ips[i].first) << "\",\"requisicoes\":" << m.ips[i].second << "}";
  o << "],\"status\":{";
  for (size_t i = 0; i < m.status.size(); i++) o << (i ? "," : "") << "\"" << m.status[i].first << "\":" << m.status[i].second;
  o << "}}";
  return o.str();
}
static string humano(ull b) {
  char t[32];
  if (b >= (1ULL << 30)) snprintf(t, sizeof t, "%.1f GB", b / 1073741824.0);
  else if (b >= (1ULL << 20)) snprintf(t, sizeof t, "%.1f MB", b / 1048576.0);
  else if (b >= 1024) snprintf(t, sizeof t, "%.1f KB", b / 1024.0);
  else snprintf(t, sizeof t, "%llu B", b);
  return t;
}

// ---------------------------------------------------------------- sessoes do painel (a chave nunca fica guardada no navegador)
static string sessaoCria(const string& ip) {
  string tok = hexrand(64);  // 64 caracteres hex = 256 bits (hexrand(n) devolve n caracteres de 4 bits)
  lock_guard<mutex> l(sesmx);
  time_t n = time(0);
  for (auto it = sessoes.begin(); it != sessoes.end();) { if (it->second.exp <= n) it = sessoes.erase(it); else ++it; }
  while (sessoes.size() >= 50) {  // descarta a mais antiga
    auto velho = sessoes.begin();
    for (auto it = sessoes.begin(); it != sessoes.end(); ++it) if (it->second.exp < velho->second.exp) velho = it;
    sessoes.erase(velho);
  }
  sessoes[sha256hex(tok)] = Sessao{n + (time_t)cfg.sessaoHoras * 3600, ip};
  return tok;
}
static bool sessaoValida(const string& tok, const string& ip) {
  if (tok.size() != 64) return false;
  lock_guard<mutex> l(sesmx);
  auto it = sessoes.find(sha256hex(tok));
  if (it == sessoes.end()) return false;
  if (it->second.exp <= time(0)) { sessoes.erase(it); return false; }
  return it->second.ip == ip;  // o token so vale para o IP que fez login
}
static void sessaoRevoga(const string& tok) { lock_guard<mutex> l(sesmx); sessoes.erase(sha256hex(tok)); }

// ---------------------------------------------------------------- conteudo embutido: SDK e painel grafico
static const char* SDK_JS = R"JS(/* nodo.js 3.0.0 - biblioteca do site para o servidor Nodo. Servida pelo proprio Nodo em /nodo.js */
(function () {
  "use strict";
  var cur = document.currentScript;
  var base = location.origin;
  try { if (cur && cur.src) base = new URL(cur.src).origin; } catch (e) {}
  var RE_NOME = /^[A-Za-z0-9][A-Za-z0-9_-]{0,63}$/;   // colecao: nao comeca com _ (esses nomes sao internos do Nodo)
  var RE_ID = /^[A-Za-z0-9_-]{1,64}$/;
  var RE_CAMPO = /^[A-Za-z0-9_.-]{1,64}$/;

  // Todo erro do Nodo e um NodoErro: .message (texto para mostrar), .status (HTTP, 0 = sem resposta),
  // .codigo (texto estavel para o seu codigo testar), .rede (true = nao chegou ao servidor).
  function NodoErro(msg, info) {
    var e = new Error(msg);
    info = info || {};
    e.name = "NodoErro";
    e.status = info.status || 0;
    e.codigo = info.codigo || "erro";
    e.rede = !!info.rede;
    e.tentarDeNovo = !!info.tentarDeNovo;
    return e;
  }
  function rejeita(msg, codigo) { return Promise.reject(NodoErro(msg, { codigo: codigo })); }
  function avisaChave() {
    var h = location.hostname;
    if (N.chave && !/^(localhost|127\.|\[?::1\]?$)/.test(h))
      console.warn("[Nodo] ATENCAO: a chave de administrador esta no codigo desta pagina. Qualquer visitante pode copia-la. Use Nodo.login() apenas em ferramentas suas.");
  }

  // Nunca manda chave nem token por http:// puro fora da sua rede (so https, localhost, redes privadas e .local).
  function canalSeguro(s) {
    try {
      var u = new URL(s);
      if (u.protocol === "https:") return true;
      return /^(localhost|\[?::1\]?|127\.\d+\.\d+\.\d+|10\.\d+\.\d+\.\d+|192\.168\.\d+\.\d+|172\.(1[6-9]|2\d|3[01])\.\d+\.\d+|[^.]+\.local)$/.test(u.hostname);
    } catch (e) { return false; }
  }

  var N = window.Nodo = {
    versao: "3.0.0",
    servidor: base,
    chave: null,       // chave de administrador (so em ferramentas SUAS; prefira Nodo.login)
    token: null,       // token de sessao obtido com Nodo.login (vale por algumas horas, so para o seu IP)
    timeout: 15000,    // ms ate desistir de uma requisicao
    tentativas: 2,     // novas tentativas, so em leituras (GET), para falhas de rede e 502/503/504
    Erro: NodoErro,
    configurar: function (o) {
      o = o || {};
      if (o.servidor != null) {
        var u;
        try { u = new URL(String(o.servidor)); } catch (e) { throw NodoErro("endereco do servidor invalido: " + o.servidor, { codigo: "servidor_invalido" }); }
        if (u.protocol !== "http:" && u.protocol !== "https:") throw NodoErro("o servidor precisa ser http:// ou https://", { codigo: "servidor_invalido" });
        N.servidor = u.origin;
      }
      if (o.chave !== undefined) N.chave = o.chave ? String(o.chave) : null;
      if (o.token !== undefined) N.token = o.token ? String(o.token) : null;
      if (o.timeout != null) N.timeout = Math.max(1000, Number(o.timeout) || 15000);
      if (o.tentativas != null) N.tentativas = Math.min(5, Math.max(0, Number(o.tentativas) || 0));
      avisaChave();
      return N;
    },
    esc: function (t) {
      return String(t == null ? "" : t).replace(/[&<>"']/g, function (c) {
        return { "&": "&amp;", "<": "&lt;", ">": "&gt;", "\"": "&quot;", "'": "&#39;" }[c];
      });
    },
    _f: function (m, p, b) {
      var maxTent = m === "GET" ? N.tentativas : 0, n = 0;
      function uma() {
        var h = { Accept: "application/json" };
        var corpo;
        if (b !== undefined) {
          try { corpo = JSON.stringify(b); } catch (e) { return rejeita("os dados nao puderam virar JSON (referencia circular?)", "dados_invalidos"); }
          if (corpo === undefined) return rejeita("dados invalidos", "dados_invalidos");
          h["Content-Type"] = "application/json";
        }
        if ((N.token || N.chave) && !canalSeguro(N.servidor)) return rejeita("o Nodo nao envia a chave por http:// fora da sua rede. Use https://", "exige_https");
        if (N.token) h.Authorization = "Bearer " + N.token;
        else if (N.chave) h["X-Nodo-Key"] = N.chave;
        var ctl = typeof AbortController !== "undefined" ? new AbortController() : null, tm = null;
        if (ctl) tm = setTimeout(function () { ctl.abort(); }, N.timeout);
        return fetch(N.servidor + p, { method: m, headers: h, body: corpo, mode: "cors", credentials: "omit", cache: "no-store", signal: ctl ? ctl.signal : undefined }).then(function (r) {
          return r.text().then(function (t) {
            clearTimeout(tm);
            var ct = (r.headers.get("content-type") || "").toLowerCase(), ehJson = ct.indexOf("json") >= 0, d = null;
            if (t) {
              try { d = JSON.parse(t); } catch (e) {
                // pagina de erro de proxy/CDN, HTML, texto... nunca devolvemos isso como se fosse dado
                throw NodoErro(r.ok ? "o servidor respondeu algo que nao e JSON valido" : "erro do servidor (HTTP " + r.status + ")", { status: r.status, codigo: r.ok ? "resposta_invalida" : "http_" + r.status, tentarDeNovo: r.status === 502 || r.status === 503 || r.status === 504 });
              }
              if (r.ok && !ehJson) throw NodoErro("o servidor respondeu com um tipo de conteudo inesperado", { status: r.status, codigo: "resposta_invalida" });
            }
            if (!r.ok) throw NodoErro((d && d.erro) || "erro HTTP " + r.status, { status: r.status, codigo: (d && d.codigo) || "http_" + r.status, tentarDeNovo: r.status === 502 || r.status === 503 || r.status === 504 });
            return d;
          });
        }, function (e) {
          clearTimeout(tm);
          var abortou = e && e.name === "AbortError";
          throw NodoErro(abortou ? "o servidor demorou demais para responder" : "nao foi possivel falar com o servidor Nodo em " + N.servidor + ". Ele esta ligado? O endereco, o HTTPS e a lista 'origens' do nodo.conf estao corretos?", { codigo: abortou ? "tempo_esgotado" : "rede", rede: true, tentarDeNovo: true });
        });
      }
      function ciclo() {
        return uma().catch(function (e) {
          if (e && e.tentarDeNovo && n < maxTent) { n++; return new Promise(function (ok) { setTimeout(ok, 300 * n * n); }).then(ciclo); }
          throw e;
        });
      }
      return ciclo();
    },
    // Entra como administrador sem deixar a chave guardada: troca a chave por um token de sessao (so em memoria desta pagina).
    login: function (chave) {
      if (!chave) return rejeita("informe a chave", "chave_ausente");
      if (!canalSeguro(N.servidor)) return rejeita("o Nodo nao envia a chave por http:// fora da sua rede. Use https://", "exige_https");
      var antes = N.chave, antesT = N.token;
      N.chave = null; N.token = null;
      return N._f("POST", "/api/_login", { chave: String(chave) }).then(function (d) {
        if (!d || typeof d.token !== "string") throw NodoErro("resposta de login invalida", { codigo: "resposta_invalida" });
        N.token = d.token;
        return d;
      }, function (e) { N.chave = antes; N.token = antesT; throw e; });
    },
    logout: function () {
      var t = N.token;
      N.token = null; N.chave = null;
      return t ? N._f("POST", "/api/_logout", {}).catch(function () { return null; }) : Promise.resolve(null);
    },
    saude: function () { return N._f("GET", "/api/_saude"); },
    colecao: function (nome) {
      if (typeof nome !== "string" || !RE_NOME.test(nome)) throw NodoErro("nome de colecao invalido: use letras, numeros, _ e - (ate 64), sem comecar com _", { codigo: "colecao_invalida" });
      var u = "/api/" + nome;
      function comId(id, fn) {
        if (typeof id !== "string" || !RE_ID.test(id)) return rejeita("id invalido: use o _id devolvido pelo Nodo", "id_invalido");
        return fn(u + "/" + id);
      }
      function objeto(d) {
        if (d === null || typeof d !== "object" || Array.isArray(d)) return rejeita("envie um objeto, por exemplo { nome: \"Maria\" }", "dados_invalidos");
        return null;
      }
      return {
        listar: function (f) {
          var q = new URLSearchParams();
          if (f != null) {
            if (typeof f !== "object") return rejeita("os filtros precisam ser um objeto", "filtro_invalido");
            var ks = Object.keys(f);
            for (var i = 0; i < ks.length; i++) {
              var k = ks[i], v = f[k];
              if (!RE_CAMPO.test(k)) return rejeita("nome de filtro invalido: " + k, "filtro_invalido");
              if (v == null) continue;
              if (typeof v === "object") return rejeita("o filtro '" + k + "' precisa ser texto, numero ou booleano", "filtro_invalido");
              if (k === "limite" || k === "pular") {
                var num = Number(v);
                if (!isFinite(num) || num < 0 || Math.floor(num) !== num) return rejeita("'" + k + "' precisa ser um numero inteiro, zero ou maior", "filtro_invalido");
                v = num;
              }
              q.append(k, String(v));
            }
          }
          var qs = q.toString();
          return N._f("GET", u + (qs ? "?" + qs : "")).then(function (d) {
            if (!Array.isArray(d)) throw NodoErro("o servidor nao devolveu uma lista", { codigo: "resposta_invalida" });
            return d;
          });
        },
        obter: function (id) { return comId(id, function (p) { return N._f("GET", p); }); },
        criar: function (d) { return objeto(d) || N._f("POST", u, d); },
        atualizar: function (id, d) { return objeto(d) || comId(id, function (p) { return N._f("PUT", p, d); }); },
        remover: function (id) { return comId(id, function (p) { return N._f("DELETE", p); }); }
      };
    }
  };
})();
)JS";

static const char* ADMIN_HTML = R"HTML(<!doctype html><html lang="pt-BR"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Nodo 3</title>
<style>
:root{color-scheme:light dark;
--fg:#0f1530;--mut:#56607f;--ac:#1d6bf0;--ac2:#4aa3ff;--ok:#12945a;--er:#d6342c;--wa:#c46a00;
--bg1:#e4ecfb;--bg2:#f1e8f8;--o1:#7dc3ff;--o2:#ffb3d4;--o3:#b4a2ff;
--gl-a:rgba(255,255,255,.66);--gl-b:rgba(255,255,255,.28);--edge:rgba(255,255,255,.78);--edge2:rgba(255,255,255,.22);
--shadow:0 14px 34px -16px rgba(34,52,120,.45);--solid:#f4f6fc;--field:rgba(255,255,255,.55);--hair:rgba(40,56,120,.14)}
@media(prefers-color-scheme:dark){:root{--fg:#e9edfb;--mut:#a3acc9;--ac:#5b9bff;--ac2:#8fc0ff;--ok:#41d596;--er:#ff7a72;--wa:#ffb04d;
--bg1:#070b1a;--bg2:#0d0a22;--o1:#1f56e0;--o2:#7d2fe0;--o3:#0a8fb4;
--gl-a:rgba(255,255,255,.12);--gl-b:rgba(255,255,255,.04);--edge:rgba(255,255,255,.30);--edge2:rgba(255,255,255,.07);
--shadow:0 18px 40px -18px rgba(0,0,0,.8);--solid:#151a30;--field:rgba(255,255,255,.07);--hair:rgba(255,255,255,.12)}}
*{box-sizing:border-box}
html{background:var(--bg1)}
body{margin:0;font:15px/1.45 ui-rounded,"SF Pro Rounded","Segoe UI Variable Text","Segoe UI",system-ui,sans-serif;color:var(--fg);min-height:100vh;display:grid;grid-template-columns:272px 1fr;gap:16px;padding:16px;
background:radial-gradient(60vmax 50vmax at 8% 6%,var(--o1) 0,transparent 62%),radial-gradient(55vmax 45vmax at 96% 12%,var(--o3) 0,transparent 60%),radial-gradient(60vmax 50vmax at 70% 104%,var(--o2) 0,transparent 62%),linear-gradient(160deg,var(--bg1),var(--bg2));background-attachment:fixed}
aside,.tabs,.stat,table,svg.g,#login div{background:linear-gradient(140deg,var(--gl-a),var(--gl-b));border:1px solid var(--edge2);border-top-color:var(--edge);border-left-color:var(--edge);
box-shadow:inset 0 1px 0 var(--edge),inset 0 -1px 0 var(--edge2),var(--shadow);-webkit-backdrop-filter:blur(26px) saturate(170%);backdrop-filter:blur(26px) saturate(170%)}
@supports not ((backdrop-filter:blur(1px)) or (-webkit-backdrop-filter:blur(1px))){aside,.tabs,.stat,table,svg.g,#login div,.doc{background:var(--solid)}}
aside{position:sticky;top:16px;height:calc(100vh - 32px);overflow:auto;border-radius:26px;padding:18px}
main{padding:6px 4px 24px;max-width:1000px;min-width:0}
h1{font-size:22px;letter-spacing:-.02em;margin:0 0 4px}h2{font-size:24px;letter-spacing:-.02em;margin:4px 0 12px}
h3{font-size:13px;font-weight:650;color:var(--mut);margin:20px 0 6px}
.mut{color:var(--mut);font-size:13px;word-break:break-all;white-space:pre-line}
button,input,textarea,select{font:inherit;color:inherit;background:var(--field);border:1px solid var(--edge2);border-top-color:var(--edge);border-radius:14px;padding:8px 12px;box-shadow:inset 0 1px 2px rgba(20,30,80,.08)}
input,textarea,select{-webkit-backdrop-filter:blur(10px);backdrop-filter:blur(10px)}
button{cursor:pointer;color:#fff;border:1px solid rgba(255,255,255,.45);background:linear-gradient(180deg,var(--ac2),var(--ac));box-shadow:inset 0 1px 0 rgba(255,255,255,.65),0 8px 18px -8px var(--ac);font-weight:600;transition:transform .12s ease,filter .12s ease}
button:hover{filter:brightness(1.06)}button:active{transform:scale(.97)}
button.s{background:linear-gradient(180deg,var(--gl-a),var(--gl-b));color:var(--fg);border:1px solid var(--edge2);border-top-color:var(--edge);box-shadow:inset 0 1px 0 var(--edge);font-weight:500}
:focus-visible{outline:2px solid var(--ac);outline-offset:2px}
.tabs{display:inline-flex;gap:4px;margin-bottom:18px;flex-wrap:wrap;padding:5px;border-radius:999px}
.tabs button{background:transparent;color:var(--fg);border:1px solid transparent;box-shadow:none;border-radius:999px;padding:7px 16px;font-weight:550}
.tabs button.on{color:var(--fg);background:linear-gradient(180deg,var(--gl-a),var(--gl-b));border-color:var(--edge);box-shadow:inset 0 1px 0 var(--edge),0 4px 12px -6px rgba(20,30,90,.5)}
.col{display:flex;justify-content:space-between;padding:8px 12px;border-radius:12px;cursor:pointer;border:1px solid transparent}
.col:hover{background:var(--field)}.col.on{background:linear-gradient(180deg,var(--gl-a),var(--gl-b));border-color:var(--edge);box-shadow:inset 0 1px 0 var(--edge)}
.doc{background:linear-gradient(140deg,var(--gl-a),var(--gl-b));border:1px solid var(--edge2);border-top-color:var(--edge);border-radius:18px;padding:12px 14px;margin:10px 0;display:flex;gap:10px;align-items:flex-start;box-shadow:inset 0 1px 0 var(--edge),var(--shadow)}
.doc pre{margin:0;flex:1;white-space:pre-wrap;word-break:break-word;font:13px ui-monospace,"SF Mono",Menlo,Consolas,monospace}
textarea{width:100%;height:90px;font:13px ui-monospace,"SF Mono",Menlo,Consolas,monospace;resize:vertical}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(160px,1fr));gap:12px;margin-bottom:16px}
.stat{border-radius:20px;padding:12px 14px;position:relative;overflow:hidden}
.stat b{display:block;font-size:22px;letter-spacing:-.02em;margin-top:2px}.stat span{font-size:12px;color:var(--mut)}
.stat.er b{color:var(--er)}.stat.wa b{color:var(--wa)}
.stat::after,.doc::after,table::after{content:"";position:absolute;inset:0;border-radius:inherit;pointer-events:none;background:radial-gradient(220px circle at var(--mx,-50%) var(--my,-50%),rgba(255,255,255,.34),transparent 62%);opacity:var(--hl,0);transition:opacity .2s}
.doc{position:relative}
table{border-collapse:separate;border-spacing:0;width:100%;border-radius:18px;margin:6px 0 16px;overflow:hidden;position:relative}
th,td{padding:8px 12px;text-align:left;border-bottom:1px solid var(--hair);font-size:13px}tr:last-child td{border-bottom:0}th{color:var(--mut);font-weight:600}
svg.g{width:100%;height:120px;border-radius:18px}
#login{position:fixed;inset:0;display:flex;align-items:center;justify-content:center;z-index:10;background:radial-gradient(60vmax 50vmax at 10% 8%,var(--o1) 0,transparent 62%),radial-gradient(55vmax 45vmax at 94% 14%,var(--o3) 0,transparent 60%),radial-gradient(60vmax 50vmax at 70% 104%,var(--o2) 0,transparent 62%),linear-gradient(160deg,var(--bg1),var(--bg2))}
#login div{padding:28px;border-radius:28px;width:min(360px,92vw);display:grid;gap:12px}
.row{display:flex;gap:8px;flex-wrap:wrap;align-items:center;margin:8px 0}.row>*{flex:0 0 auto}
@media(max-width:760px){body{grid-template-columns:1fr;padding:10px}aside{position:static;height:auto}}
@media(prefers-reduced-motion:reduce){*{transition:none!important}}
</style></head><body>
<div id="login"><div><h1>Nodo</h1><span class="mut">Cole a chave do arquivo nodo.conf. Ela troca por um acesso temporário e não fica guardada neste navegador.</span><input id="k" type="password" placeholder="chave" autocomplete="off"><button id="bEntrar">Entrar</button><span id="le" class="mut"></span></div></div>
<aside><h1>Nodo</h1><div class="mut" id="st">...</div>
<h3>Endereços</h3><div class="mut" id="ips"></div>
<h3>Coleções</h3><div id="cols"></div><p><button class="s" id="bNova">+ Nova coleção</button></p>
<h3>PCs na rede</h3><div class="mut" id="peers">nenhum</div><p><input id="pi" placeholder="ip:porta" style="width:100%"></p><button class="s" id="bPeer">Conectar PC</button>
<p><button class="s" id="bSair">Sair</button></p></aside>
<main>
<div class="tabs"><button data-t="dados" class="on">Dados</button><button data-t="monitor">Monitor de uso</button></div>
<section id="t-dados"><h2 id="t">Selecione uma coleção</h2><div id="new" style="display:none"><textarea id="nj" placeholder='{"nome":"Maria","idade":30}'></textarea><p><button id="bCriar">Adicionar documento</button> <span id="ne" class="mut"></span></p></div><div id="docs"></div></section>
<section id="t-monitor" style="display:none"><h2>Monitor de uso</h2><div class="grid" id="cards"></div>
<h3>Requisições por minuto (última hora) — vermelho = erros</h3><svg class="g" id="graf" viewBox="0 0 600 120" preserveAspectRatio="none"></svg>
<h3>Coleções</h3><table id="tcol"></table><h3>Quem mais pediu (IP)</h3><table id="tip"></table><p class="mut" id="mmeta"></p></section>

</main>
<script>
const $=s=>document.querySelector(s);let T=sessionStorage.t||"",cur=null,aba="dados";
function el(t,c,x){const e=document.createElement(t);if(c)e.className=c;if(x!==undefined)e.textContent=x;return e}
async function api(m,p,b){let r;try{r=await fetch(p,{method:m,headers:{'Authorization':'Bearer '+T,'Content-Type':'application/json'},body:b})}catch(e){throw new Error('sem conexão com o servidor')}
const t=await r.text();let d=null;try{d=t?JSON.parse(t):null}catch(e){if(!r.ok)throw new Error('erro '+r.status);throw new Error('resposta que não é JSON')}
if(r.status==401){sair();throw new Error(d&&d.erro||'sessão expirada')}
if(!r.ok)throw new Error(d&&d.erro||r.status);return d}
function sair(){T='';sessionStorage.removeItem('t');$('#login').style.display='flex'}
async function entrar(){const k=$('#k').value.trim();if(!k)return;$('#le').textContent='';
try{const r=await fetch('/api/_login',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({chave:k})});const d=await r.json();
if(!r.ok)throw new Error(d.erro||r.status);T=d.token;sessionStorage.t=T;$('#k').value='';load()}catch(e){$('#le').textContent=e.message}}
async function sairBtn(){try{await api('POST','/api/_logout','{}')}catch(e){}sair()}
async function load(){if(!T){$('#login').style.display='flex';return}
try{const s=await api('GET','/api/_status');$('#login').style.display='none';
$('#st').textContent='v'+s.versao+' · porta '+s.porta+' · online há '+Math.round(s.uptime/60)+' min';
$('#ips').textContent=s.ips.map(i=>'http://'+i+':'+s.porta).join('\n')||'localhost';
$('#peers').textContent=s.peers.length?s.peers.join('\n'):'nenhum';
const c=await api('GET','/api/_colecoes');const box=$('#cols');box.innerHTML='';
Object.keys(c).sort().forEach(n=>{const d=el('div','col'+(n==cur?' on':''));d.append(el('span','',n),el('span','mut',c[n]));d.onclick=()=>{cur=n;mostra('dados');load()};box.append(d)});
if(aba=='dados'&&cur)await docs();if(aba=='monitor')await monitor()}catch(e){$('#le').textContent=e.message}}
async function docs(){$('#t').textContent=cur;$('#new').style.display='block';const l=await api('GET','/api/'+encodeURIComponent(cur)+'?limite=100');const b=$('#docs');b.innerHTML='';
l.forEach(d=>{const r=el('div','doc');r.append(el('pre','',JSON.stringify(d,null,2)));const x=el('button','s','Apagar');x.onclick=async()=>{if(confirm('Apagar documento?')){try{await api('DELETE','/api/'+encodeURIComponent(cur)+'/'+encodeURIComponent(d._id));load()}catch(e){alert(e.message)}}};r.append(x);b.append(r)})}
function nova(){const n=prompt('Nome da coleção (letras, números, _ e -)');if(n&&/^[A-Za-z0-9][A-Za-z0-9_-]{0,63}$/.test(n)){cur=n;mostra('dados');$('#t').textContent=n;$('#new').style.display='block';$('#docs').innerHTML=''}}
async function criar(){if(!cur)return;try{await api('POST','/api/'+encodeURIComponent(cur),$('#nj').value);$('#nj').value='';$('#ne').textContent='';load()}catch(e){$('#ne').textContent=e.message}}
async function addp(){try{await api('POST','/api/_peers',$('#pi').value.trim());$('#pi').value='';load()}catch(e){alert(e.message)}}
function mostra(a){aba=a;['dados','monitor'].forEach(x=>{$('#t-'+x).style.display=x==a?'block':'none'});document.querySelectorAll('.tabs button').forEach(b=>b.classList.toggle('on',b.dataset.t==a));if(a=='monitor')monitor()}
function fmt(n){return Number(n).toLocaleString('pt-BR')}
function bytes(b){const u=['B','KB','MB','GB'];let i=0;b=Number(b);while(b>=1024&&i<3){b/=1024;i++}return b.toFixed(i?1:0)+' '+u[i]}
function dur(s){const h=Math.floor(s/3600),m=Math.floor(s%3600/60);return h?h+' h '+m+' min':m+' min'}
function stat(box,rot,val,cls){const d=el('div','stat'+(cls?' '+cls:''));d.append(el('span','',rot),el('b','',val));box.append(d)}
async function monitor(){try{const m=await api('GET','/api/_metricas');const b=$('#cards');b.innerHTML='';
const min=m.porMinuto.slice(-5).reduce((a,x)=>a+x,0)/5;
stat(b,'Requisições (total)',fmt(m.requisicoes));stat(b,'Por minuto (média 5 min)',min.toFixed(1));stat(b,'Leituras / escritas',fmt(m.leituras)+' / '+fmt(m.escritas));
stat(b,'Erros 4xx',fmt(m.erros4xx),m.erros4xx?'wa':'');stat(b,'Erros 5xx',fmt(m.erros5xx),m.erros5xx?'er':'');stat(b,'Limite excedido (429)',fmt(m.limitados429),m.limitados429?'wa':'');
stat(b,'Chaves erradas',fmt(m.authFalhas),m.authFalhas?'er':'');stat(b,'Origens recusadas',fmt(m.origemNegada),m.origemNegada?'wa':'');stat(b,'Conexões agora / pico',m.conexoes+' / '+m.picoConexoes);
stat(b,'Latência média / p95',m.latenciaMediaMs+' / '+m.latenciaP95Ms+' ms');stat(b,'CPU',m.cpuPct<0?'n/d':m.cpuPct+' %');stat(b,'Memória',m.memoriaMb<0?'n/d':m.memoriaMb+' MB');
stat(b,'Banco em disco',bytes(m.discoBytes));stat(b,'Documentos',fmt(m.documentos)+' em '+m.colecoes+' coleções');stat(b,'Tráfego enviado / recebido',bytes(m.bytesEnviados)+' / '+bytes(m.bytesRecebidos));
stat(b,'PCs (online/total)',m.replicacao.online+'/'+m.replicacao.pcs);stat(b,'Fila de replicação',m.replicacao.fila,m.replicacao.fila>100?'wa':'');stat(b,'Online há',dur(m.uptime));
const g=$('#graf');g.innerHTML='';const mx=Math.max(1,...m.porMinuto),ns='http://www.w3.org/2000/svg';
m.porMinuto.forEach((v,i)=>{const h=v/mx*110,r=document.createElementNS(ns,'rect');r.setAttribute('x',i*10+1);r.setAttribute('y',118-h);r.setAttribute('width',8);r.setAttribute('height',h);r.setAttribute('fill','#4f46e5');g.append(r);
const e=m.errosPorMinuto[i];if(e){const he=e/mx*110,q=document.createElementNS(ns,'rect');q.setAttribute('x',i*10+1);q.setAttribute('y',118-he);q.setAttribute('width',8);q.setAttribute('height',he);q.setAttribute('fill','#dc2626');g.append(q)}});
tab('#tcol',['Coleção','Documentos','Leituras','Escritas'],Object.keys(m.porColecao).map(n=>[n,fmt(m.porColecao[n].documentos),fmt(m.porColecao[n].leituras),fmt(m.porColecao[n].escritas)]));
tab('#tip',['IP','Requisições'],m.topIps.map(x=>[x.ip,fmt(x.requisicoes)]));
$('#mmeta').textContent='Versão '+m.versao+' · sessões do painel ativas: '+m.sessoes+' · replicação: '+m.replicacao.enviados+' enviados, '+m.replicacao.falhas+' falhas, '+m.replicacao.descartados+' descartados. Os números zeram quando o servidor reinicia.'}catch(e){$('#le').textContent=e.message}}
function tab(sel,cab,linhas){const t=$(sel);t.innerHTML='';const h=el('tr');cab.forEach(c=>h.append(el('th','',c)));t.append(h);
if(!linhas.length){const r=el('tr'),d=el('td','mut','sem dados ainda');d.colSpan=cab.length;r.append(d);t.append(r)}
linhas.forEach(l=>{const r=el('tr');l.forEach(c=>r.append(el('td','',c)));t.append(r)})}
document.querySelectorAll('.tabs button').forEach(b=>b.onclick=()=>mostra(b.dataset.t));
$('#bEntrar').onclick=entrar;$('#k').onkeydown=e=>{if(e.key=='Enter')entrar()};$('#bNova').onclick=nova;$('#bPeer').onclick=addp;$('#bCriar').onclick=criar;$('#bSair').onclick=sairBtn;
load();setInterval(()=>{if(T)load()},5000);
document.addEventListener('pointermove',ev=>{const c=ev.target.closest&&ev.target.closest('.stat,.doc,table');if(!c)return;const r=c.getBoundingClientRect();c.style.setProperty('--mx',(ev.clientX-r.left)+'px');c.style.setProperty('--my',(ev.clientY-r.top)+'px');c.style.setProperty('--hl','1')});document.addEventListener('pointerout',ev=>{const c=ev.target.closest&&ev.target.closest('.stat,.doc,table');if(c)c.style.setProperty('--hl','0')});
</script></body></html>
)HTML";

// ---------------------------------------------------------------- servidor HTTP
struct Req {
  string method, path, body, ip, origin;
  map<string, string> q, h;
  bool https = false, insegura = false, bloq = false, viaProxy = false;
};
static thread_local size_t tlBytesIn = 0;
static void sendAll(int c, const string& s) {
  size_t o = 0;
  while (o < s.size()) {
    nodo_ssize_t n = send(c, s.data() + o, s.size() - o, 0);
    if (n <= 0) return;
    o += n;
    tlBytesOut += n;
  }
}
static const char* statusText(int s) {
  switch (s) {
    case 200: return "OK"; case 201: return "Created"; case 204: return "No Content"; case 400: return "Bad Request";
    case 401: return "Unauthorized"; case 403: return "Forbidden"; case 404: return "Not Found"; case 405: return "Method Not Allowed";
    case 409: return "Conflict"; case 413: return "Payload Too Large"; case 429: return "Too Many Requests"; case 500: return "Internal Server Error";
    case 503: return "Service Unavailable"; default: return "Error";
  }
}
static string cabecalho(int st, const string& ctype, size_t len, bool admin = false) {
  tlStatus = st;
  string r = "HTTP/1.1 " + to_string(st) + " " + statusText(st) + "\r\nContent-Type: " + ctype + "\r\nContent-Length: " + to_string(len);
  // CORS: so devolvemos Access-Control-Allow-Origin para origens que a rota aceita (decidido em route())
  if (!tlOrigem.empty()) r += tlCoringa ? "\r\nAccess-Control-Allow-Origin: *" : "\r\nAccess-Control-Allow-Origin: " + tlOrigem + "\r\nVary: Origin";
  else if (!cfg.origens.empty()) r += "\r\nVary: Origin";
  r += "\r\nAccess-Control-Expose-Headers: Retry-After\r\nX-Content-Type-Options: nosniff\r\nReferrer-Policy: strict-origin-when-cross-origin"
       "\r\nPermissions-Policy: camera=(), microphone=(), geolocation=()";
  r += admin ? "\r\nX-Frame-Options: DENY" : "\r\nX-Frame-Options: SAMEORIGIN";
  r += (admin || tlCorsPriv) ? "\r\nCross-Origin-Resource-Policy: same-origin" : "\r\nCross-Origin-Resource-Policy: cross-origin";
  if (tlHttps) r += "\r\nStrict-Transport-Security: max-age=31536000";
  if (admin) r += "\r\nCache-Control: no-store\r\nContent-Security-Policy: default-src 'self'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; connect-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'none'";
  else if (ctype.rfind("application/json", 0) == 0) r += "\r\nCache-Control: no-store";
  if (!tlExtra.empty()) { r += "\r\n" + tlExtra; tlExtra.clear(); }
  return r + "\r\nConnection: close\r\n\r\n";
}
static void reply(int c, int st, const string& ctype, const string& body, bool head = false, bool admin = false) {
  string r = cabecalho(st, ctype, body.size(), admin);
  if (!head) r += body;
  sendAll(c, r);
}
static string codigoPadrao(int st) {
  switch (st) {
    case 400: return "requisicao_invalida"; case 401: return "nao_autorizado"; case 403: return "proibido"; case 404: return "nao_encontrado";
    case 405: return "metodo_nao_permitido"; case 409: return "conflito"; case 413: return "grande_demais"; case 429: return "muitas_requisicoes";
    default: return "erro";
  }
}
// Toda resposta de erro e JSON: {"erro":"texto para mostrar","codigo":"id_estavel","status":400}
static void jerr(int c, int st, const string& m, const string& codigo = "") {
  reply(c, st, "application/json; charset=utf-8", "{\"erro\":\"" + jesc(m) + "\",\"codigo\":\"" + (codigo.empty() ? codigoPadrao(st) : codigo) + "\",\"status\":" + to_string(st) + "}");
}
static void jok(int c, int st, const string& b) { reply(c, st, "application/json; charset=utf-8", b); }
static string jdecode(const string& s) {  // desfaz os escapes de um texto JSON (entre aspas ja removidas)
  string o;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] != '\\' || i + 1 >= s.size()) { o += s[i]; continue; }
    char e = s[++i];
    switch (e) {
      case 'n': o += '\n'; break; case 't': o += '\t'; break; case 'r': o += '\r'; break; case 'b': o += '\b'; break; case 'f': o += '\f'; break;
      case 'u': {
        if (i + 4 >= s.size()) break;
        unsigned cp = (unsigned)strtoul(s.substr(i + 1, 4).c_str(), nullptr, 16);
        i += 4;
        if (cp >= 0xD800 && cp < 0xDC00 && i + 6 < s.size() && s[i + 1] == '\\' && s[i + 2] == 'u') {  // par substituto
          unsigned lo = (unsigned)strtoul(s.substr(i + 3, 4).c_str(), nullptr, 16);
          if (lo >= 0xDC00 && lo < 0xE000) { cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); i += 6; }
        }
        if (cp < 0x80) o += (char)cp;
        else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
        else { o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 0x3F)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
        break;
      }
      default: o += e;
    }
  }
  return o;
}
// le um campo (texto ou booleano) de um objeto JSON
static string campoJson(const string& body, const char* nome) {
  string m = minify(body);
  vector<pair<string, string>> p;
  if (!jsonValido(m) || !jpairs(m, p)) return "";
  for (auto& kv : p) if (kv.first == nome) return kv.second.size() >= 2 && kv.second[0] == '"' ? jdecode(unq(kv.second)) : kv.second;
  return "";
}
static bool soDigitos(const string& s) {
  if (s.empty() || s.size() > 9) return false;
  for (char c : s) if (!isdigit((unsigned char)c)) return false;
  return true;
}

static string m401(const Req& r, const char* padrao) {
  if (r.bloq) return "muitas tentativas erradas; aguarde 15 minutos";
  if (r.insegura) return "a chave so e aceita por HTTPS ou rede local";
  return padrao;
}
static string tokenDe(const Req& r) {
  auto ia = r.h.find("authorization");
  if (ia != r.h.end() && ia->second.size() > 7 && lower(ia->second.substr(0, 7)) == "bearer ") return trim(ia->second.substr(7));
  auto it = r.h.find("x-nodo-token");
  return it != r.h.end() ? it->second : "";
}
// Autentica por token de sessao (Authorization: Bearer ...) ou pela chave (X-Nodo-Key). Erros contam para o bloqueio por IP.
static bool authOk(Req& r) {
  string tok = tokenDe(r);
  auto ik = r.h.find("x-nodo-key");
  if (tok.empty() && ik == r.h.end()) return false;
  if (rlBloqueado("auth|" + r.ip, 10, 900)) { r.bloq = true; return false; }
  if (!(r.https || ipLocal(r.ip) || cfg.adminRemoto)) { r.insegura = true; return false; }
  bool ok = false;
  if (!tok.empty()) ok = sessaoValida(tok, r.ip);
  if (!ok && ik != r.h.end()) {
    const string &a = ik->second, &b = cfg.chave;
    unsigned d = a.size() != b.size();
    for (size_t i = 0; i < min(a.size(), b.size()); i++) d |= (unsigned)(a[i] ^ b[i]);
    ok = d == 0;
  }
  if (!ok) {
    mAuthFalhas++;
    rlRegistra("auth|" + r.ip);
    if (rlBloqueado("auth|" + r.ip, 10, 900)) logln("[seguranca] IP bloqueado por 15 min apos 10 credenciais erradas: " + r.ip);
    dormirMs(250);
  }  // 10 erros em 15 min bloqueiam o IP
  return ok;
}
static string mime(const string& p) {
  static map<string, string> m = {{"html", "text/html; charset=utf-8"}, {"htm", "text/html; charset=utf-8"}, {"css", "text/css; charset=utf-8"},
    {"js", "application/javascript; charset=utf-8"}, {"mjs", "application/javascript; charset=utf-8"}, {"json", "application/json"}, {"png", "image/png"}, {"jpg", "image/jpeg"},
    {"jpeg", "image/jpeg"}, {"gif", "image/gif"}, {"svg", "image/svg+xml"}, {"webp", "image/webp"}, {"ico", "image/x-icon"},
    {"txt", "text/plain; charset=utf-8"}, {"woff2", "font/woff2"}, {"woff", "font/woff"}, {"mp4", "video/mp4"}, {"pdf", "application/pdf"},
    {"wasm", "application/wasm"}, {"webmanifest", "application/manifest+json"}, {"map", "application/json"}};
  size_t d = p.rfind('.');
  if (d == string::npos) return "application/octet-stream";
  auto it = m.find(lower(p.substr(d + 1)));
  return it == m.end() ? "application/octet-stream" : it->second;
}
static bool nomeInterno(const string& n) {  // arquivos do proprio Nodo/segredos: nunca servir
  string l = lower(n);
  static const char* fixos[] = {"nodo.conf", "nodo-dados", "nodo", "nodo.exe", "iniciar.sh", "parar.sh", "iniciar.bat", "parar.bat", "nodo_core.hpp", "plataforma.hpp", "prompt-nodo.md"};
  for (auto f : fixos) if (l == f) return true;
  static const char* ext[] = {".regras", ".bak", ".log", ".conf", ".tmp", ".pid", ".pem", ".key", ".env"};
  for (auto e : ext) { size_t k = strlen(e); if (l.size() >= k && l.compare(l.size() - k, k, e) == 0) return true; }
  return false;
}
static void serveStatic(const Req& r, int c) {
  tlTipo = 's';
  string p = r.path;
  if (p.empty() || p[0] != '/') return jerr(c, 400, "caminho invalido");
  if (p.back() == '/') p += "index.html";
  for (char ch : p) if (ch == 0 || ch == '\\' || ch == ':' || (unsigned char)ch < 0x20) return jerr(c, 403, "proibido");
  for (auto& sg : split(p, '/')) {
    if (sg.empty()) continue;
    if (sg[0] == '.' || sg.back() == '.' || sg.back() == ' ') return jerr(c, 403, "proibido");
    if (nomeInterno(sg)) return jerr(c, 404, "nao encontrado");
  }
  error_code ec;
  fs::path root = fs::canonical(fs::current_path(ec), ec);
  if (ec) return jerr(c, 404, "nao encontrado");
  fs::path full = fs::canonical(root / fs::u8path(p.substr(1)), ec);
  if (ec) return jerr(c, 404, "nao encontrado");
  fs::path rel = full.lexically_relative(root);
  if (rel.empty() || *rel.begin() == "..") return jerr(c, 403, "proibido");  // link simbolico que sai da pasta
  for (const auto& part : rel) if (nomeInterno(part.u8string())) return jerr(c, 404, "nao encontrado");
  if (!fs::is_regular_file(full, ec)) return jerr(c, 404, "nao encontrado");
  uintmax_t sz = fs::file_size(full, ec);
  if (ec) return jerr(c, 404, "nao encontrado");
  ifstream f(full, ios::binary);
  if (!f) return jerr(c, 404, "nao encontrado");
  string h = cabecalho(200, mime(p), (size_t)sz);
  if (r.method == "HEAD") { sendAll(c, h); return; }
  sendAll(c, h);
  char buf[65536];
  while (f) { f.read(buf, sizeof buf); streamsize n = f.gcount(); if (n <= 0) break; sendAll(c, string(buf, (size_t)n)); }
}
// Um PC que fala conosco com a chave certa e anuncia sua porta passa a ser um par (so em rede local/Tailscale).
static void aprendePeer(const Req& r) {
  auto pt = r.h.find("x-nodo-porta"), id = r.h.find("x-nodo-id");
  if (pt == r.h.end() || id == r.h.end() || id->second == meuId || !soDigitos(pt->second) || pt->second.size() > 5 || !ipLocal(r.ip)) return;
  addPeer(r.ip + ":" + pt->second, true);
}
static void apiPrivada(Req& r, int c, const string& col) {
  tlTipo = 'o';
  if (col == "_saude" && (r.method == "GET" || r.method == "HEAD")) return jok(c, 200, "{\"ok\":true}");
  if (col == "_login" && r.method == "POST") {
    string k = campoJson(r.body, "chave");
    r.h.erase("x-nodo-token"); r.h.erase("authorization");
    if (!k.empty()) r.h["x-nodo-key"] = k;
    if (!authOk(r)) return jerr(c, 401, m401(r, "chave invalida"), r.bloq ? "bloqueado" : r.insegura ? "exige_https" : "chave_invalida");
    string t = sessaoCria(r.ip);
    mLogins++;
    return jok(c, 200, "{\"token\":\"" + t + "\",\"horas\":" + to_string(cfg.sessaoHoras) + ",\"expiraEm\":" + to_string((long long)time(0) + (long long)cfg.sessaoHoras * 3600) + "}");
  }
  if (!authOk(r)) return jerr(c, 401, m401(r, "chave invalida ou ausente"), r.bloq ? "bloqueado" : r.insegura ? "exige_https" : "chave_invalida");
  if (col == "_logout" && r.method == "POST") { string t = tokenDe(r); if (!t.empty()) sessaoRevoga(t); return jok(c, 200, "{\"ok\":true}"); }
  if (col == "_status" && r.method == "GET") {
    ostringstream o;
    o << "{\"versao\":\"" << VERSAO << "\",\"porta\":" << cfg.porta << ",\"uptime\":" << (time(0) - inicioT) << ",\"ips\":[";
    auto ips = localIps();
    for (size_t i = 0; i < ips.size(); i++) o << (i ? "," : "") << "\"" << ips[i] << "\"";
    o << "],\"peers\":[";
    { lock_guard<mutex> l(pmx); bool f = true; for (auto& p : peers) { o << (f ? "" : ",") << "\"" << jesc(p) << "\""; f = false; } }
    o << "]}";
    return jok(c, 200, o.str());
  }
  if (col == "_metricas" && r.method == "GET") return jok(c, 200, metricasJson(metricasSnap()));
  if (col == "_colecoes" && r.method == "GET") {
    ostringstream o;
    o << "{";
    lock_guard<mutex> l(smx);
    bool f = true;
    for (auto& kv : cols) { o << (f ? "" : ",") << "\"" << kv.first << "\":" << kv.second.size(); f = false; }
    o << "}";
    return jok(c, 200, o.str());
  }

  if (col == "_deploy" && r.method == "POST") {  // webhook de deploy: git pull no projeto (exige a chave)
    static mutex dmx;
    lock_guard<mutex> dl(dmx);
    string o = deployHook ? deployHook() : "deploy indisponivel nesta versao";
    logln("[deploy] " + (o.size() > 200 ? o.substr(0, 200) : o));
    return jok(c, 200, "{\"ok\":true,\"saida\":\"" + jesc(o) + "\"}");
  }
  if (col == "_peers" && r.method == "POST") {
    string hp = trim(r.body);
    if (!hp.empty() && hp[0] == '{') { hp = trim(campoJson(hp, "peer")); }
    if (hp.empty()) return jerr(c, 400, "informe ip:porta", "peer_invalido");
    if (hp.find(':') == string::npos) hp += ":7070";
    if (!peerValido(hp)) return jerr(c, 400, "endereco invalido: use ip:porta (ex.: 192.168.0.20:7070)", "peer_invalido");
    addPeer(hp, true);
    return jok(c, 200, "{\"ok\":true}");
  }
  if (col == "_replicar" && r.method == "POST") {
    aprendePeer(r);
    bool mudou = applyLine(trim(r.body));
    return jok(c, 200, string("{\"ok\":true,\"aplicado\":") + (mudou ? "true" : "false") + "}");
  }
  if (col == "_dump" && r.method == "GET") {
    aprendePeer(r);
    long long desde = r.q.count("desde") ? atoll(r.q["desde"].c_str()) : 0;
    string o;
    lock_guard<mutex> l(smx);
    for (auto& cc : cols) for (auto& d : cc.second) if (desde <= 0 || docT(d.second) >= desde) o += "P\t" + cc.first + "\t" + d.first + "\t" + d.second + "\n";
    for (auto& cc : tomb) for (auto& d : cc.second) if (desde <= 0 || d.second >= desde) o += "D\t" + cc.first + "\t" + d.first + "\t" + to_string(d.second) + "\n";
    return reply(c, 200, "text/plain; charset=utf-8", o);
  }
  jerr(c, 404, "rota desconhecida", "rota_desconhecida");
}
static void sugerirRegra(const string& col, const string& body, bool faltaEscrita) {  // diz no Registro exatamente o que por no nodo.regras
  static mutex mx; static map<string, time_t> ult;
  if (!colOk(col)) return;
  { lock_guard<mutex> l(mx); if (ult.size() > 200) ult.clear(); time_t n = time(0); auto& u = ult[col]; if (u && n - u < 60) return; u = n; }
  vector<pair<string, string>> ps; string m = minify(body), campos;
  if (jsonValido(m) && jpairs(m, ps)) for (auto& kv : ps) if (campoNomeOk(kv.first) && kv.first[0] != '_' && campos.size() < 300) campos += (campos.empty() ? "" : ",") + kv.first + ":200";
  logln("[regras] um visitante tentou gravar em '" + col + "' e foi recusado. Para liberar, ponha no nodo.regras: " + (faltaEscrita ? "escrita_publica=" + col + "  e  " : "") + "campos." + col + "=" + (campos.empty() ? "campo1:100,campo2:500" : campos) + "  (ajuste os limites; nao precisa reiniciar)");
}
static void api(Req& r, int c) {
  auto parts = split(r.path.substr(5), '/');  // r.path ja vem decodificado uma unica vez (nao decodificar de novo)
  if (parts.size() > 2) return jerr(c, 404, "rota desconhecida", "rota_desconhecida");
  string col = parts[0];
  string id = parts.size() > 1 ? parts[1] : "";
  if (!col.empty() && col[0] == '_') return apiPrivada(r, c, col);
  if (!colOk(col)) return jerr(c, 400, "nome de colecao invalido: use letras, numeros, _ e - (ate 64 caracteres)", "colecao_invalida");
  if (parts.size() > 1 && !idOk(id)) return jerr(c, 400, "id invalido: so letras, numeros, _ e - (ate 64 caracteres)", "id_invalido");
  tlCol = col;
  bool auth = authOk(r);
  if (!auth && r.method == "GET" && !rlPermite("r|" + r.ip, cfg.limiteLeitura, 60)) { tlExtra = "Retry-After: 60"; return jerr(c, 429, "muitas requisicoes; tente em instantes", "muitas_requisicoes"); }
  bool pubR = cfg.leitura.count(col) > 0, pubW = cfg.escrita.count(col) > 0;
  if (r.method == "GET") {
    tlTipo = 'l';
    if (!auth && !pubR) return jerr(c, 401, m401(r, "leitura exige chave"), r.bloq ? "bloqueado" : r.insegura ? "exige_https" : "leitura_exige_chave");
    if (!id.empty()) {
      string d;
      if (!storeGet(col, id, d)) return jerr(c, 404, "documento nao encontrado", "documento_nao_encontrado");
      return jok(c, 200, d);
    }
    size_t lim = 200, skip = 0;
    vector<pair<string, string>> filt;
    if (r.q.size() > 20) return jerr(c, 400, "parametros demais na consulta", "parametro_invalido");
    for (auto& kv : r.q) {
      if (kv.first == "limite" || kv.first == "pular") {
        if (!soDigitos(kv.second)) return jerr(c, 400, "'" + kv.first + "' precisa ser um numero inteiro, zero ou maior", "parametro_invalido");
        size_t v = strtoul(kv.second.c_str(), nullptr, 10);
        if (kv.first == "limite") lim = min<size_t>(v, 5000); else skip = v;
      } else {
        if (!campoNomeOk(kv.first)) return jerr(c, 400, "nome de filtro invalido", "filtro_invalido");
        filt.push_back(kv);
      }
    }
    string o = "[";
    size_t n = 0, seen = 0;
    {
      lock_guard<mutex> l(smx);
      auto it = cols.find(col);
      if (it != cols.end())
        for (auto& d : it->second) {
          bool ok = true;
          if (!filt.empty()) {
            vector<pair<string, string>> ps;
            jpairs(d.second, ps);
            for (auto& f : filt) {
              bool hit = false;
              for (auto& kv : ps) if (kv.first == f.first && unq(kv.second) == f.second) hit = true;
              if (!hit) { ok = false; break; }
            }
          }
          if (!ok) continue;
          if (seen++ < skip) continue;
          if (n >= lim) break;
          o += (n++ ? "," : "") + d.second;
        }
    }
    return jok(c, 200, o + "]");
  }
  if (r.method == "POST" && id.empty()) {
    tlTipo = 'e';
    if (!auth && !pubW) sugerirRegra(col, r.body, true);
    if (!auth && !pubW) return jerr(c, 401, m401(r, "escrita exige chave"), r.bloq ? "bloqueado" : r.insegura ? "exige_https" : "escrita_exige_chave");
    size_t limCol = cfg.maxDocsCol.count(col) ? cfg.maxDocsCol[col] : cfg.maxDocs;
    if (!auth) {
      if (!cfg.escritaLivre && !cfg.campos.count(col)) sugerirRegra(col, r.body, false);
      if (!cfg.escritaLivre && !cfg.campos.count(col)) return jerr(c, 403, "esta colecao aceita escrita publica mas nao tem campos.<colecao> no nodo.conf; por seguranca o Nodo recusa", "campos_obrigatorios");
      if (r.body.size() > cfg.maxDocBytes) return jerr(c, 413, "documento grande demais (max " + to_string(cfg.maxDocBytes) + " bytes)", "documento_grande");
      if (!rlPermite("w|" + r.ip, cfg.limiteEscrita, 60)) { tlExtra = "Retry-After: 60"; return jerr(c, 429, "muitos envios; aguarde um minuto", "muitas_requisicoes"); }
      string ce;
      if (!camposOk(col, r.body, ce)) return jerr(c, 400, ce, ce.find("limite de") != string::npos ? "campo_grande" : ce.rfind("campo nao permitido", 0) == 0 ? "campo_nao_permitido" : "json_invalido");
    }
    if (trim(r.body).empty()) return jerr(c, 400, "corpo vazio: envie um objeto JSON", "json_invalido");
    string id2 = newId(), doc;
    if (!makeDoc(r.body, id2, doc)) return jerr(c, 400, "corpo precisa ser um objeto JSON valido (ex.: {\"nome\":\"Maria\"})", "json_invalido");
    if (storeApply(col, id2, doc, true, false, limCol) == SA_CHEIO) return jerr(c, 413, "colecao cheia (limite de " + to_string(limCol) + " documentos)", "colecao_cheia");
    return jok(c, 201, doc);
  }
  if ((r.method == "PUT" || r.method == "DELETE") && !id.empty()) {
    tlTipo = 'e';
    if (!auth) return jerr(c, 401, m401(r, "alterar/apagar exige chave"), r.bloq ? "bloqueado" : r.insegura ? "exige_https" : "escrita_exige_chave");
    string old;
    if (!storeGet(col, id, old)) return jerr(c, 404, "documento nao encontrado", "documento_nao_encontrado");
    if (r.method == "DELETE") { storeDel(col, id, true); return jok(c, 200, "{\"ok\":true}"); }
    string doc;
    long long t = max(agoraMs(), docT(old) + 1);  // o _t nunca anda para tras, mesmo com o relogio ajustado
    if (!makeDoc(r.body, id, doc, t)) return jerr(c, 400, "corpo precisa ser um objeto JSON valido", "json_invalido");
    storeApply(col, id, doc, true, false, 0);
    return jok(c, 200, doc);
  }
  jerr(c, 405, "metodo nao permitido", "metodo_nao_permitido");
}
static bool rotaPrivada(const string& p) { return p.rfind("/api/_", 0) == 0 || p == "/admin" || p == "/admin/" || p == "/guia" || p == "/guia/"; }
static bool mesmaOrigem(const Req& r, const string& origin) {
  auto h = r.h.find("host");
  size_t p = origin.find("://");
  return h != r.h.end() && p != string::npos && lower(origin.substr(p + 3)) == lower(h->second);
}
// 0 = recusa, 1 = libera para qualquer site ("*"), 2 = libera so esta origem. Rotas privadas (painel, /api/_*) nunca usam "*".
static int corsDecide(const Req& r, bool privada) {
  if (mesmaOrigem(r, r.origin)) return 2;
  if (cfg.origens.count(r.origin)) return 2;
  if (!privada && (cfg.origens.empty() || cfg.origens.count("*"))) return 1;
  return 0;
}
static string hostSemPorta(string h) {
  h = lower(trim(h));
  if (!h.empty() && h[0] == '[') { size_t e = h.find(']'); return e == string::npos ? h : h.substr(0, e + 1); }
  size_t p = h.rfind(':');
  if (p != string::npos && h.find(':') == p) h = h.substr(0, p);
  return h;
}
static string hostDeUrl(const string& u) {
  size_t p = u.find("://");
  string h = p == string::npos ? u : u.substr(p + 3);
  size_t b = h.find_first_of("/?#");
  if (b != string::npos) h.resize(b);
  return hostSemPorta(h);
}
static bool terminaCom(const string& s, const string& suf) { return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0; }
// Acesso direto (sem proxy): so aceita Host que nao possa ser um site de terceiros apontado para este PC (DNS rebinding).
static bool hostPermitido(const Req& r) {
  auto it = r.h.find("host");
  if (it == r.h.end() || trim(it->second).empty()) return true;  // HTTP/1.0 e ferramentas simples
  string h = hostSemPorta(it->second);
  if (h.empty()) return false;
  if (h[0] == '[') return true;                                   // IPv6 literal
  unsigned n;
  if (ipv4Num(h, n)) return true;                                 // IP literal nunca e um nome controlado por terceiros
  if (h == "localhost" || terminaCom(h, ".localhost") || terminaCom(h, ".local") || terminaCom(h, ".ts.net")) return true;
  if (h.find('.') == string::npos) return true;                   // nome de uma palavra (rede local)
  if (cfg.hostsPermitidos.count(h)) return true;
  for (auto& o : cfg.origens) if (hostDeUrl(o) == h) return true;
  for (const string* u : {&cfg.tsUrl, &cfg.dominio}) if (!u->empty() && (hostDeUrl(*u) == h || "www." + hostDeUrl(*u) == h)) return true;
  return false;
}
static void route(Req& r, int c) {
  if (!r.viaProxy && !hostPermitido(r)) {
    mOrigemNegada++;
    return jerr(c, 421, "este nome de host nao esta autorizado (veja 'hosts_permitidos' e 'origens' no nodo.conf)", "host_negado");
  }
  bool priv = rotaPrivada(r.path);
  tlCorsPriv = priv;
  if (!r.origin.empty()) {
    int d = corsDecide(r, priv);
    if (d == 0) {  // vale tambem para POST/PUT/DELETE: sem isto o navegador executaria a escrita e so esconderia a resposta
      mOrigemNegada++;
      return jerr(c, 403, "este site nao tem permissao para usar o servidor (veja 'origens' no nodo.conf)", "origem_negada");
    }
    tlOrigem = r.origin;
    tlCoringa = d == 1;
  }
  if (r.method == "OPTIONS") {
    if (!r.origin.empty()) tlExtra = "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\nAccess-Control-Allow-Headers: Content-Type, X-Nodo-Key, Authorization\r\nAccess-Control-Max-Age: 600";
    return reply(c, 204, "text/plain", "");
  }
  if (r.path == "/admin" || r.path == "/admin/") return reply(c, 200, "text/html; charset=utf-8", ADMIN_HTML, r.method == "HEAD", true);
  if (r.path == "/guia" || r.path == "/guia/") {
    if (!ipLocal(r.ip)) return jerr(c, 404, "nao encontrado");  // o guia so abre no PC ou na rede local
    return reply(c, 200, "text/html; charset=utf-8", GUIA_HTML, r.method == "HEAD", true);
  }
  if (r.path == "/nodo.js") { tlTipo = 's'; return reply(c, 200, "application/javascript; charset=utf-8", SDK_JS, r.method == "HEAD"); }
  if (r.path.rfind("/api/", 0) == 0) return api(r, c);
  if (r.method != "GET" && r.method != "HEAD") return jerr(c, 405, "metodo nao permitido");
  serveStatic(r, c);
}
static mutex cimx;
static map<string, int> conIp;
static void atender(int c, const string& peerIp) {
  sockTimeout(c, 10);
  time_t t0 = time(0);
  string buf;
  char tmp[8192];
  size_t he = string::npos;
  while (he == string::npos && buf.size() < 16384) {
    if (time(0) - t0 > 10) return;  // protege contra conexoes lentas (slowloris)
    nodo_ssize_t n = recv(c, tmp, sizeof tmp, 0);
    if (n <= 0) return;
    buf.append(tmp, n);
    he = buf.find("\r\n\r\n");
  }
  if (he == string::npos) return jerr(c, 400, "cabecalho grande demais", "cabecalho_invalido");
  Req r;
  r.ip = peerIp;
  tlIp = peerIp;
  tlBytesIn = buf.size();
  istringstream hs(buf.substr(0, he));
  string line, target, ver;
  getline(hs, line);
  istringstream rl(line);
  rl >> r.method >> target >> ver;
  static const char* metodos[] = {"GET", "HEAD", "POST", "PUT", "DELETE", "OPTIONS"};
  bool metOk = false;
  for (auto m : metodos) if (r.method == m) metOk = true;
  if (!metOk) return jerr(c, 405, "metodo nao permitido");
  if (ver.rfind("HTTP/1.", 0) != 0) return jerr(c, 400, "versao de HTTP nao suportada", "requisicao_invalida");
  if (target.empty() || target[0] != '/' || target.size() > 4096) return jerr(c, 400, "requisicao invalida");
  {  // cabecalhos so com CRLF: um LF solto poderia esconder um cabecalho de um proxy que le o bloco de outro jeito
    const string hb = buf.substr(0, he);
    for (size_t i = 0; i < hb.size(); i++) {
      if (hb[i] == 0 || (hb[i] == '\n' && (i == 0 || hb[i - 1] != '\r')) || (hb[i] == '\r' && (i + 1 >= hb.size() || hb[i + 1] != '\n')))
        return jerr(c, 400, "cabecalhos precisam terminar em CRLF", "requisicao_invalida");
    }
  }
  static const set<string> unicos = {"host", "origin", "x-forwarded-for", "x-forwarded-proto", "authorization", "x-nodo-key", "x-nodo-token", "x-nodo-id", "x-nodo-porta", "transfer-encoding", "expect", "content-length"};
  int nh = 0;
  while (getline(hs, line) && nh++ < 100) {
    size_t p = line.find(':');
    if (p == string::npos) continue;
    string nome = lower(trim(line.substr(0, p))), val = trim(line.substr(p + 1));
    auto ja = r.h.find(nome);
    if (ja != r.h.end() && unicos.count(nome) && ja->second != val)  // repetido e diferente: evita request smuggling e IP forjado
      return jerr(c, 400, nome == "content-length" ? string("Content-Length repetido") : "cabecalho repetido: " + curto(nome, 40), "requisicao_invalida");
    r.h[nome] = val;
  }
  if (ehProxy(peerIp)) {  // so confia nos cabecalhos do proxy local (ou dos enderecos de proxies_confiaveis)
    r.viaProxy = r.h.count("x-forwarded-for") > 0;
    string xf = r.h.count("x-forwarded-for") ? r.h["x-forwarded-for"] : "";
    if (!xf.empty()) { auto v = split(xf, ','); string ult = trim(v.back()); if (ult.size() < 46) r.ip = ult; }
    r.https = (r.h.count("x-forwarded-proto") && lower(r.h["x-forwarded-proto"]) == "https") || r.h.count("tailscale-funnel-request");
  }
  tlIp = r.ip;
  tlHttps = r.https;
  if (r.h.count("origin")) {
    r.origin = r.h["origin"];
    bool bom = r.origin.size() <= 300;
    for (char ch : r.origin) if ((unsigned char)ch < 0x21 || ch == 0x7f) bom = false;
    if (!bom) return jerr(c, 400, "cabecalho Origin invalido", "requisicao_invalida");
  }
  size_t qm = target.find('?');
  r.path = urldec(target.substr(0, qm));
  for (char ch : r.path) if ((unsigned char)ch < 0x20 || ch == 0x7f) return jerr(c, 400, "caminho invalido", "requisicao_invalida");
  if (qm != string::npos)
    for (auto& kv : split(target.substr(qm + 1), '&')) {
      size_t e = kv.find('=');
      if (e != string::npos && r.q.size() < 64) r.q[urldec(kv.substr(0, e))] = urldec(kv.substr(e + 1));
    }
  if (r.h.count("transfer-encoding")) return jerr(c, 400, "transfer-encoding nao suportado", "requisicao_invalida");
  size_t cl = 0;
  if (r.h.count("content-length")) {
    const string& v = r.h["content-length"];
    if (v.empty() || v.size() > 10) return jerr(c, 400, "Content-Length invalido", "requisicao_invalida");
    for (char ch : v) if (!isdigit((unsigned char)ch)) return jerr(c, 400, "Content-Length invalido", "requisicao_invalida");
    cl = strtoul(v.c_str(), nullptr, 10);
  }
  if (cl > MAX_BODY) return jerr(c, 413, "corpo grande demais (max 1MB)", "grande_demais");
  r.body = buf.substr(he + 4);
  if (r.body.size() < cl && r.h.count("expect") && lower(r.h["expect"]) == "100-continue") sendAll(c, "HTTP/1.1 100 Continue\r\n\r\n");
  while (r.body.size() < cl) {
    if (time(0) - t0 > 30) return;
    nodo_ssize_t n = recv(c, tmp, sizeof tmp, 0);
    if (n <= 0) break;
    r.body.append(tmp, n);
    tlBytesIn += n;
  }
  if (r.body.size() < cl) return jerr(c, 400, "corpo incompleto: a conexao caiu antes de chegar tudo", "corpo_incompleto");
  if (r.body.size() > cl) r.body.resize(cl);
  if (r.path.rfind("/api/", 0) == 0 && r.method != "GET" && r.method != "OPTIONS") logln("[api] " + r.method + " " + r.path.substr(0, 100));
  route(r, c);
  shutdown(c, SHUT_WR);
}
static void handle(int c, string ip) {
  auto t0 = chrono::steady_clock::now();
  try { shared_lock<shared_mutex> rl(regrasMx); atender(c, ip); } catch (...) { if (tlStatus == 0) { try { jerr(c, 500, "erro interno", "erro_interno"); } catch (...) {} } }
  double ms = chrono::duration<double, milli>(chrono::steady_clock::now() - t0).count();
  metricaRegistra(tlIp.empty() ? ip : tlIp, tlStatus, tlBytesIn, tlBytesOut, ms, tlCol, tlTipo);
  cerrar(c);
  { lock_guard<mutex> l(cimx); if (--conIp[ip] <= 0) conIp.erase(ip); }
  conexoes--;
}


// ---------------------------------------------------------------- configuracao do projeto (nodo.conf)
static string joinSet(const set<string>& s) {
  string o;
  for (auto& x : s) o += (o.empty() ? "" : ",") + x;
  return o;
}
static bool enderecoIpv4Ok(const string& e) { in_addr a; return inet_pton(AF_INET, e.c_str(), &a) == 1; }
static bool origemOk(const string& o) {  // https://meusite.com, http://localhost:3000, * ou null
  if (o == "*" || o == "null") return true;
  size_t p = o.find("://");
  if (p == string::npos || (o.substr(0, p) != "http" && o.substr(0, p) != "https") || o.size() > 200) return false;
  string h = o.substr(p + 3);
  if (h.empty()) return false;
  for (char ch : h) if (!isalnum((unsigned char)ch) && ch != '.' && ch != '-' && ch != ':' && ch != '[' && ch != ']') return false;
  return true;
}
// nodo.conf = segredos e endereco (chave, porta, escutar...). nodo.regras = regras publicas do projeto (sem segredos, vai para o Git, recarrega sozinho).
static bool chaveDeRegra(const string& k) {
  static const set<string> ok = {"leitura_publica", "escrita_publica", "origens", "limite_escrita", "limite_leitura", "max_campo", "max_docs", "max_doc_bytes"};
  return ok.count(k) || k.rfind("campos.", 0) == 0 || k.rfind("max_docs.", 0) == 0;
}
static string hostSemPortaCfg(string h) { h = lower(trim(h)); size_t p = h.find("://"); if (p != string::npos) h = h.substr(p + 3); size_t b = h.find_first_of("/?#"); if (b != string::npos) h.resize(b); size_t c = h.rfind(':'); if (c != string::npos && h.find(':') == c) h.resize(c); return h; }
static void lerCfgArquivo(Cfg& C, const char* arq, bool soRegras) {
  ifstream f(arq);
  if (!f) return;
  string l;
  while (getline(f, l)) {
    if (!l.empty() && l.back() == '\r') l.pop_back();
    if (l.empty() || l[0] == '#') continue;
    size_t p = l.find('=');
    if (p == string::npos) continue;
    string k = trim(l.substr(0, p)), v = trim(l.substr(p + 1));
    if (soRegras && !chaveDeRegra(k)) { logln("[regras] '" + k.substr(0, 40) + "' so vale no nodo.conf; ignorado em nodo.regras"); continue; }
    if (k == "porta") C.porta = atoi(v.c_str());
    else if (k == "escutar") {
      if (enderecoIpv4Ok(v)) { C.escutar = v; C.escutarArquivo = v; }
      else logln("[config] escutar=" + v + " nao e um endereco IPv4 valido; usando 127.0.0.1 (so este PC)");
    }
    else if (k == "chave") C.chave = v;
    else if (k == "descoberta") C.descoberta = (v == "sim");
    else if (k == "proxy_confiavel") C.proxyConfiavel = (v == "sim");
    else if (k == "hosts_permitidos") { for (auto& h : split(v, ',')) { string x = hostSemPortaCfg(h); if (!x.empty()) C.hostsPermitidos.insert(x); } }
    else if (k == "proxies_confiaveis") { for (auto& e : split(v, ',')) { string x = trim(e); if (!x.empty() && x.size() < 50) C.proxies.push_back(x); } }
    else if (k == "admin_remoto") C.adminRemoto = (v == "sim");
    else if (k == "escrita_livre") C.escritaLivre = (v == "sim");
    else if (k == "origens") {
      for (auto& x : split(v, ',')) {
        string o = trim(x);
        while (o.size() > 1 && o.back() == '/') o.pop_back();
        if (o.empty()) continue;
        if (origemOk(o)) C.origens.insert(o); else logln("[config] origem ignorada (formato invalido): " + o.substr(0, 60));
      }
    }
    else if (k == "git_remoto" || k == "git_branch" || k == "ts_url" || k == "dominio" || k == "vercel_projeto" || k == "vercel_team") {
      bool bom = v.size() <= 300;
      for (char ch : v) if (!isalnum((unsigned char)ch) && !strchr("@:/._~+=-", ch)) bom = false;
      if (bom) (k == "git_remoto" ? C.gitRemoto : k == "git_branch" ? C.gitBranch : k == "ts_url" ? C.tsUrl : k == "dominio" ? C.dominio : k == "vercel_projeto" ? C.vercelProjeto : C.vercelTeam) = v;
    }
    else if (k == "limite_leitura") C.limiteLeitura = max(1, atoi(v.c_str()));
    else if (k == "sync_segundos") C.syncSegundos = min(3600, max(5, atoi(v.c_str())));
    else if (k == "sessao_horas") C.sessaoHoras = min(168, max(1, atoi(v.c_str())));
    else if (k == "max_campo") C.maxCampoPadrao = min(60000, max(1, atoi(v.c_str())));
    else if (k.rfind("campos.", 0) == 0 && colOk(k.substr(7))) {
      auto& m = C.campos[k.substr(7)];
      for (auto& x : split(v, ',')) {
        string it = trim(x);
        if (it.empty()) continue;
        int lim = -1;
        size_t d = it.find(':');
        if (d != string::npos) { lim = atoi(it.substr(d + 1).c_str()); it = trim(it.substr(0, d)); }
        if (!campoNomeOk(it) || it[0] == '_') { logln("[config] campo ignorado em " + k + ": " + it.substr(0, 40)); continue; }
        m[it] = lim > 0 ? min(lim, 60000) : -1;  // -1 = usa max_campo (resolvido abaixo, pois max_campo pode vir depois)
      }
    }
    else if (k.rfind("max_docs.", 0) == 0 && colOk(k.substr(9))) C.maxDocsCol[k.substr(9)] = (size_t)max(1, atoi(v.c_str()));
    else if (k == "limite_escrita") C.limiteEscrita = max(1, atoi(v.c_str()));
    else if (k == "max_docs") C.maxDocs = max(1, atoi(v.c_str()));
    else if (k == "max_doc_bytes") C.maxDocBytes = max(64, atoi(v.c_str()));
    else if (k == "leitura_publica") { for (auto& x : split(v, ',')) if (!trim(x).empty() && colOk(trim(x))) C.leitura.insert(trim(x)); }
    else if (k == "escrita_publica") { for (auto& x : split(v, ',')) if (!trim(x).empty() && colOk(trim(x))) C.escrita.insert(trim(x)); }
  }
}
static bool carregarTudo(Cfg& C) {
  lerCfgArquivo(C, "nodo.conf", false);
  lerCfgArquivo(C, "nodo.regras", true);
  for (auto& c : C.campos) for (auto& kv : c.second) if (kv.second < 0) kv.second = C.maxCampoPadrao;
  if (const char* e = getenv("NODO_ESCUTAR")) if (enderecoIpv4Ok(e)) C.escutar = e;  // Docker: o contêiner escuta em 0.0.0.0 sem mexer no nodo.conf do seu disco
  return C.chave.size() >= 32 && C.porta > 0 && C.porta < 65536;
}
static string assinaturaRegras() {
  string o;
  for (const char* a : {"nodo.conf", "nodo.regras"}) { struct stat st; o += stat(a, &st) == 0 ? to_string((long long)st.st_mtime) + ":" + to_string((long long)st.st_size) + "|" : "-|"; }
  return o;
}
static string assinaturaCfg;
static bool loadCfg() {
  cfg = Cfg();
  { ifstream t("nodo.conf"); if (!t) return false; }
  bool ok = carregarTudo(cfg);
  assinaturaCfg = assinaturaRegras();
  return ok;
}
static string camposTexto(const Cfg& c) {
  string o;
  for (auto& kv : c.campos) {
    o += "campos." + kv.first + "=";
    bool f = true;
    for (auto& cp : kv.second) { o += (f ? "" : ",") + cp.first + ":" + to_string(cp.second); f = false; }
    o += "\n";
  }
  for (auto& kv : c.maxDocsCol) o += "max_docs." + kv.first + "=" + to_string(kv.second) + "\n";
  return o;
}
static string joinVec(const vector<string>& v) { string o; for (auto& x : v) o += (o.empty() ? "" : ",") + x; return o; }
static string confTexto(int porta, const string& escutar, const string& chave, const string& leit, const string& esc, const Cfg& c) {
  return "# Configuracao do Nodo. NUNCA publique este arquivo.\nporta=" + to_string(porta) +
         "\n# 127.0.0.1 = so neste PC (use isto atras de um proxy HTTPS); 0.0.0.0 = acessivel na rede (so em rede local confiavel)\nescutar=" + escutar + "\nchave=" + chave +
         "\n# colecoes que qualquer visitante do site pode LER (separe por virgula)\nleitura_publica=" + leit +
         "\n# colecoes onde visitantes podem so CRIAR documentos (ex.: formularios). Exigem campos.<colecao> abaixo\nescrita_publica=" + esc +
         "\n# sites autorizados a usar o servidor pelo navegador (CORS), ex.: https://meusite.com,https://www.meusite.com . Vazio = qualquer site, so em colecoes publicas\norigens=" + joinSet(c.origens) +
         "\n# busca automatica de outros PCs Nodo na rede local (sim/nao). Deixe 'nao' em servidores publicos\ndescoberta=" + string(c.descoberta ? "sim" : "nao") +
         "\n# sim = o Nodo esta atras de Caddy/nginx/Cloudflare no mesmo PC e confia em X-Forwarded-For/Proto\nproxy_confiavel=" + string(c.proxyConfiavel ? "sim" : "nao") +
         "\n# sim = aceita a chave por HTTP puro vindo da internet (NAO recomendado; prefira HTTPS)\nadmin_remoto=" + string(c.adminRemoto ? "sim" : "nao") +
         "\n# sim = permite escrita publica em colecao SEM campos.<colecao> (NAO recomendado)\nescrita_livre=" + string(c.escritaLivre ? "sim" : "nao") +
         "\n# envios de visitantes por minuto, por IP\nlimite_escrita=" + to_string(c.limiteEscrita) +
         "\n# horas de validade do acesso temporario do painel (/admin)\nsessao_horas=" + to_string(c.sessaoHoras) +
         "\n# de quantos em quantos segundos puxar as mudancas dos PCs conectados (replicacao)\nsync_segundos=" + to_string(c.syncSegundos) +
         "\n# --- publicacao (preenchido pelo app; sem segredos) ---\ngit_remoto=" + c.gitRemoto + "\ngit_branch=" + c.gitBranch + "\nts_url=" + c.tsUrl + "\ndominio=" + c.dominio + "\nvercel_projeto=" + c.vercelProjeto + "\nvercel_team=" + c.vercelTeam +
         "\n# nomes (com ponto) aceitos no cabecalho Host em acesso direto, alem de localhost, IPs, nomes da rede local e dos sites em origens/dominio. Ex.: meusite.com\nhosts_permitidos=" + joinSet(c.hostsPermitidos) +
         "\n# quem, alem deste PC, pode informar o IP real do visitante (proxy). Ex.: 172.16.0.0/12 para o Docker. So com proxy_confiavel=sim\nproxies_confiaveis=" + joinVec(c.proxies) +
         "\n# leituras publicas por minuto, por IP\nlimite_leitura=" + to_string(c.limiteLeitura) +
         "\n# campos.<colecao>=nome:80,email:120,mensagem:1000 : unicos campos que visitantes podem enviar, cada um com o maximo de caracteres\n# max_docs.<colecao>=N : limite de documentos so dessa colecao\n" + camposTexto(c) +
         "\n# maximo de caracteres de um campo sem ':N', maximo de documentos por colecao e de bytes por documento enviado por visitante\nmax_campo=" + to_string(c.maxCampoPadrao) + "\nmax_docs=" + to_string(c.maxDocs) + "\nmax_doc_bytes=" + to_string(c.maxDocBytes) + "\n";
}
static bool saveCfg() { return writePrivado("nodo.conf", confTexto(cfg.porta, cfg.escutarArquivo.empty() ? cfg.escutar : cfg.escutarArquivo, cfg.chave, joinSet(cfg.leitura), joinSet(cfg.escrita), cfg)); }
static void recarregarRegras() {
  { ifstream t("nodo.conf"); if (!t) { logln("[regras] nodo.conf nao encontrado; regras mantidas"); return; } }
  Cfg n;
  carregarTudo(n);
  if (n.chave != cfg.chave || n.porta != cfg.porta || n.escutar != cfg.escutar) logln("[regras] chave, porta ou endereco mudaram no nodo.conf: reinicie o Nodo para valerem (as regras ja foram atualizadas)");
  {
    unique_lock<shared_mutex> lk(regrasMx);
    cfg.leitura = n.leitura; cfg.escrita = n.escrita; cfg.campos = n.campos; cfg.maxDocsCol = n.maxDocsCol; cfg.origens = n.origens;
    cfg.limiteEscrita = n.limiteEscrita; cfg.limiteLeitura = n.limiteLeitura; cfg.maxDocs = n.maxDocs; cfg.maxDocBytes = n.maxDocBytes; cfg.maxCampoPadrao = n.maxCampoPadrao;
  }
  logln("[regras] recarregadas sem reiniciar. leitura publica: " + (n.leitura.empty() ? string("(nenhuma)") : joinSet(n.leitura)) + " | escrita publica: " + (n.escrita.empty() ? string("(nenhuma)") : joinSet(n.escrita)));
}
static void vigiaRegras() {  // olha nodo.conf e nodo.regras a cada 2 s; recarrega quando o arquivo muda e ja esta estavel
  string ultimo;
  while (true) {
    this_thread::sleep_for(chrono::seconds(2));
    if (!rodando) continue;
    string s = assinaturaRegras();
    if (s != assinaturaCfg && s == ultimo) { assinaturaCfg = s; try { recarregarRegras(); } catch (...) {} }
    ultimo = s;
  }
}

// ---------------------------------------------------------------- controle do servidor (ligar/desligar sem reiniciar o programa)
static atomic<bool> acceptVivo{false};
static void backupSeNecessario();
struct Achado { char nivel; string msg; };  // 'O' ok, 'A' aviso, 'X' alerta
static vector<Achado> auditoria();
static bool ipLoopback(const string& e) { return e.rfind("127.", 0) == 0; }
// Confere a configuracao ANTES de abrir a porta. Devolve um erro (e nao liga) so para combinacoes realmente perigosas; o resto vira aviso no Registro.
static string prechecagem() {
  if (cfg.chave.size() < 32) return "A chave do nodo.conf e curta demais. Gere outra: nodo chave";
  if (cfg.adminRemoto && !ipLoopback(cfg.escutar)) return "Recusado: admin_remoto=sim com escutar=" + cfg.escutar + " aceitaria a chave em HTTP puro pela rede. Use HTTPS (Tailscale/Caddy) e admin_remoto=nao.";
  if (!ipLoopback(cfg.escutar)) logln("[seguranca] ATENCAO: o Nodo vai escutar em " + cfg.escutar + " (acessivel por outros aparelhos da rede). Para so este PC use escutar=127.0.0.1.");
  int n = 0;
  for (auto& a : auditoria()) if (a.nivel != 'O' && n++ < 8) logln(string(a.nivel == 'X' ? "[ALERTA] " : "[aviso] ") + a.msg);
  return "";
}
static string startServer() {
  if (rodando) return "";
  redeInit();
  { string pe = prechecagem(); if (!pe.empty()) return pe; }
#ifndef _WIN32
  signal(SIGPIPE, SIG_IGN);
  umask(077);  // banco e logs so legiveis pelo dono
#endif
  if (meuId.empty()) meuId = hexrand(8);
  nodoMkdir(cfg.dados.c_str(), 0700);
  loadStore();
  { lock_guard<mutex> l(pmx); peers.clear(); peerFalhas.clear(); peerSync.clear(); }
  { string pt; if (readFile(cfg.dados + "/peers.txt", pt)) for (auto& l : split(pt, '\n')) if (peerValido(trim(l))) addPeer(trim(l), false); }
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return "Nao foi possivel criar o socket de rede.";
  sockReuse(fd);
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_port = htons(cfg.porta);
  if (inet_pton(AF_INET, cfg.escutar.c_str(), &a.sin_addr) != 1) { cerrar(fd); return "Endereco 'escutar' invalido no nodo.conf: " + cfg.escutar; }
  if (::bind(fd, (sockaddr*)&a, sizeof a) < 0 || listen(fd, 256) < 0) {
    cerrar(fd);
    return "Nao foi possivel abrir a porta " + to_string(cfg.porta) + ". Ela ja esta em uso? Troque a porta em Configuracoes.";
  }
  inicioT = time(0);
  rodando = true;
  acceptVivo = true;
  static atomic<bool> disc{false};
  if (cfg.descoberta && !disc.exchange(true)) thread(discovery).detach();
  static atomic<bool> vig{false};
  if (!vig.exchange(true)) thread(vigiaRegras).detach();
  static atomic<bool> repl{false};
  if (!repl.exchange(true)) { thread(replWorker).detach(); thread(antiEntropia).detach(); }
  thread([fd] {
    while (rodando) {
      if (!esperaLeitura(fd, 400)) continue;
      sockaddr_in ca{};
      socklen_t cal = sizeof ca;
      int c = accept(fd, (sockaddr*)&ca, &cal);
      if (c < 0) continue;
      char ipb[64] = "?";
      inet_ntop(AF_INET, &ca.sin_addr, ipb, sizeof ipb);
      string ip = ipb;
      bool ok = conexoes < 256;
      if (ok) { lock_guard<mutex> l(cimx); if (conIp[ip] >= (ehProxy(ip) ? 200 : 32)) ok = false; else conIp[ip]++; }  // limite por IP
      if (!ok) { cerrar(c); continue; }
      int agoraCon = ++conexoes, pico = mPicoCon;
      while (agoraCon > pico && !mPicoCon.compare_exchange_weak(pico, agoraCon)) {}
      thread(handle, c, ip).detach();
    }
    cerrar(fd);
    acceptVivo = false;
  }).detach();
  static atomic<bool> bkVivo{false};
  if (!bkVivo.exchange(true)) thread([] {
    while (rodando) { backupSeNecessario(); for (int i = 0; i < 3600 && rodando; i++) dormirMs(1000); }
    bkVivo = false;
  }).detach();
  logln("[nodo] servidor ligado na porta " + to_string(cfg.porta));
  return "";
}
static void stopServer() {
  if (!rodando) return;
  rodando = false;
  for (int i = 0; i < 50 && acceptVivo; i++) dormirMs(100);
  logln("[nodo] servidor desligado");
}
static string openProject(const string& pasta) {
  error_code ec;
  fs::current_path(fs::u8path(pasta), ec);
  if (ec) return "Pasta nao encontrada.";
  if (!loadCfg()) return "NOCONF";
  nodoMkdir(cfg.dados.c_str(), 0700);
  return "";
}

// ---------------------------------------------------------------- preparar uma pasta de projeto
static string selfPath(const string& a0) {
#ifdef _WIN32
  char b[MAX_PATH];
  DWORD n = GetModuleFileNameA(nullptr, b, MAX_PATH);
  return n ? string(b, n) : "";
#else
  error_code ec;
  if (a0.find('/') != string::npos) { auto q = fs::canonical(fs::u8path(a0), ec); if (!ec) return q.u8string(); }
  if (const char* pe = getenv("PATH"))
    for (auto& d : split(pe, ':')) {
      string p = d + "/" + a0;
      if (access(p.c_str(), X_OK) == 0) { auto q = fs::canonical(fs::u8path(p), ec); if (!ec) return q.u8string(); }
    }
  return "";
#endif
}
struct IntegrarRes { bool ok = false; bool novo = false; string chave, msg; };
// cli=true: copia o programa 'nodo' e scripts iniciar.sh/parar.sh para a pasta. O app com janela usa cli=false.
static const char* REGRAS_MODELO = "# nodo.regras - regras PUBLICAS deste projeto (sem segredos). Pode ir para o Git junto com o site.\n# O Nodo recarrega este arquivo sozinho em ate 2 segundos: nao precisa reiniciar.\n# A chave, a porta e o endereco continuam so no nodo.conf.\n#\n# leitura_publica=produtos,mural\n# escrita_publica=contatos\n# campos.contatos=nome:80,email:120,mensagem:1000\n# max_docs.contatos=5000\n# origens=https://meusite.com\n";
static IntegrarRes integrarPasta(const string& pasta, const string& a0, bool cli) {
  IntegrarRes R;
  error_code ec;
  fs::create_directories(fs::u8path(pasta), ec);
  if (!fs::is_directory(fs::u8path(pasta), ec)) { R.msg = "Pasta invalida: " + pasta; return R; }
  nodoMkdir((pasta + "/nodo-dados").c_str(), 0700);
  string conf = pasta + "/nodo.conf";
  R.novo = !fs::exists(fs::u8path(conf), ec);
  if (R.novo) {
    R.chave = hexrand(48);
    Cfg padrao;
    writePrivado(conf, confTexto(7070, "127.0.0.1", R.chave, "", "", padrao));
  } else {
    string c;
    readFile(conf, c);
    for (auto& l : split(c, '\n')) if (l.rfind("chave=", 0) == 0) R.chave = trim(l.substr(6));
  }
  writeFile(pasta + "/nodo.js", SDK_JS);
  { string rg = pasta + "/nodo.regras"; if (!fs::exists(fs::u8path(rg), ec)) writeFile(rg, REGRAS_MODELO); }
  string feito = "nodo.js (biblioteca do site), nodo.conf (chave), nodo-dados/ (banco)";
  if (cli) {
    string me = selfPath(a0);
#ifdef _WIN32
    string dst = pasta + "/nodo.exe";
#else
    string dst = pasta + "/nodo";
#endif
    bool same = !me.empty() && fs::equivalent(fs::u8path(dst), fs::u8path(me), ec);
    if (!me.empty() && !same) { fs::remove(fs::u8path(dst), ec); fs::copy_file(fs::u8path(me), fs::u8path(dst), ec); nodoChmod(dst.c_str(), 0755); }
#ifdef _WIN32
    writeFile(pasta + "/iniciar.bat", "@echo off\r\ncd /d \"%~dp0\"\r\nnodo.exe iniciar .\r\n");
    writeFile(pasta + "/parar.bat", "@echo off\r\ncd /d \"%~dp0\"\r\nnodo.exe parar .\r\n");
    feito += ", nodo.exe (programa), iniciar.bat, parar.bat";
#else
    writeFile(pasta + "/iniciar.sh", "#!/bin/sh\ncd \"$(dirname \"$0\")\" && ./nodo iniciar .\n");
    writeFile(pasta + "/parar.sh", "#!/bin/sh\ncd \"$(dirname \"$0\")\" && ./nodo parar .\n");
    nodoChmod((pasta + "/iniciar.sh").c_str(), 0755);
    nodoChmod((pasta + "/parar.sh").c_str(), 0755);
    feito += ", nodo (programa), iniciar.sh, parar.sh";
#endif
  }
  string gi;
  readFile(pasta + "/.gitignore", gi);
  if (gi.find("nodo.conf") == string::npos) writeFile(pasta + "/.gitignore", gi + (gi.empty() || gi.back() == '\n' ? "" : "\n") + "nodo.conf\nnodo-dados/\n" + (cli ? "nodo\nnodo.exe\n" : ""));
  string html;
  if (readFile(pasta + "/index.html", html) && html.find("nodo.js") == string::npos) {
    writeFile(pasta + "/index.html.bak", html);
    size_t pos = html.rfind("</body>");
    if (pos == string::npos) pos = html.rfind("</BODY>");
    string tag = "<script src=\"nodo.js\"></script>\n";
    if (pos == string::npos) html += "\n" + tag; else html.insert(pos, tag);
    writeFile(pasta + "/index.html", html);
    feito += "; <script src=\"nodo.js\"> inserido no index.html (copia de seguranca: index.html.bak)";
  }
  R.ok = true;
  R.msg = feito;
  return R;
}

// ---------------------------------------------------------------- utilidades do banco para a interface (listar, exportar, importar)
static vector<pair<string, size_t>> storeCols() {
  lock_guard<mutex> l(smx);
  vector<pair<string, size_t>> o;
  for (auto& kv : cols) o.push_back({kv.first, kv.second.size()});
  return o;
}
static vector<pair<string, string>> storeDocs(const string& col) {
  lock_guard<mutex> l(smx);
  vector<pair<string, string>> o;
  auto it = cols.find(col);
  if (it != cols.end()) for (auto& d : it->second) o.push_back({d.first, d.second});
  return o;
}
static bool storeExport(const string& path) {
  string o = "{";
  lock_guard<mutex> l(smx);
  bool f = true;
  for (auto& c : cols) {
    o += string(f ? "" : ",") + "\n\"" + c.first + "\":[";
    bool g = true;
    for (auto& d : c.second) { o += string(g ? "" : ",") + "\n  " + d.second; g = false; }
    o += "\n]";
    f = false;
  }
  o += "\n}\n";
  return writePrivado(path, o);
}
// Importa {"colecao":[{...},{...}]}. Devolve quantos documentos entraram (-1 se o arquivo e invalido).
static int storeImport(const string& txt) {
  vector<pair<string, string>> top;
  if (!jpairs(minify(txt), top)) return -1;
  int n = 0;
  for (auto& c : top) {
    if (!colOk(c.first) || c.second.empty() || c.second[0] != '[') continue;
    size_t i = skipWs(c.second, 1);
    while (i < c.second.size() && c.second[i] != ']') {
      size_t e = skipVal(c.second, i);
      if (e == string::npos) break;
      string raw = c.second.substr(i, e - i), id = newId(), doc;
      vector<pair<string, string>> ps;
      if (jpairs(raw, ps)) {
        for (auto& kv : ps) if (kv.first == "_id" && idOk(unq(kv.second))) id = unq(kv.second);
        if (makeDoc(raw, id, doc)) { storePut(c.first, id, doc, true); n++; }
      }
      i = skipWs(c.second, e);
      if (i < c.second.size() && c.second[i] == ',') i = skipWs(c.second, i + 1);
    }
  }
  return n;
}
static vector<string> peersList() {
  lock_guard<mutex> l(pmx);
  return vector<string>(peers.begin(), peers.end());
}

// ---------------------------------------------------------------- backups e auditoria de seguranca
static string backupAgora() {
  string dir = cfg.dados + "/backups";
  nodoMkdir(dir.c_str(), 0700);
  char t[32];
  time_t n = time(0);
  tm lt{};
#ifdef _WIN32
  localtime_s(&lt, &n);
#else
  localtime_r(&n, &lt);
#endif
  strftime(t, sizeof t, "%Y%m%d-%H%M%S", &lt);
  string path = dir + "/backup-" + t + ".json";
  if (!storeExport(path)) return "";
  vector<string> arq;  // mantem os 7 mais recentes
  error_code ec;
  for (auto& e : fs::directory_iterator(fs::u8path(dir), ec)) {
    string nm = e.path().filename().u8string();
    if (nm.rfind("backup-", 0) == 0) arq.push_back(nm);
  }
  sort(arq.begin(), arq.end());
  while (arq.size() > 7) { fs::remove(fs::u8path(dir + "/" + arq.front()), ec); arq.erase(arq.begin()); }
  return path;
}
static void backupSeNecessario() {
  string dir = cfg.dados + "/backups";
  error_code ec;
  fs::file_time_type novo{};
  bool achou = false;
  for (auto& e : fs::directory_iterator(fs::u8path(dir), ec)) {
    auto tm_ = fs::last_write_time(e.path(), ec);
    if (!achou || tm_ > novo) { novo = tm_; achou = true; }
  }
  if (achou && fs::file_time_type::clock::now() - novo < chrono::hours(24)) return;
  string p = backupAgora();
  if (!p.empty()) logln("[backup] salvo em " + p);
}
static vector<Achado> auditoria() {
  vector<Achado> o;
  auto add = [&](char n, const string& m) { o.push_back({n, m}); };
  if (cfg.chave.size() >= 32) add('O', "Chave de administrador forte (" + to_string(cfg.chave.size() * 4) + " bits).");
  else add('X', "Chave curta demais. Rode: nodo chave");
#ifndef _WIN32
  struct stat st;
  if (stat("nodo.conf", &st) == 0) {
    if (st.st_mode & 077) add('X', "nodo.conf pode ser lido por outros usuarios. Rode: chmod 600 nodo.conf");
    else add('O', "nodo.conf so e legivel por voce.");
  }
#endif
  if (cfg.escutar.rfind("127.", 0) == 0) add('O', "Nodo escuta so neste PC (use um proxy HTTPS para publicar).");
  else add('A', "Nodo escuta em toda a rede (escutar=" + cfg.escutar + "). Para internet: escutar=127.0.0.1 + proxy HTTPS (Caddy/Tailscale) e firewall so nas portas 80/443.");
  if (cfg.adminRemoto) add('X', "admin_remoto=sim: a chave trafega em HTTP puro pela internet. Volte para nao e use HTTPS.");
  else add('O', "A chave so e aceita por HTTPS ou rede local; o painel troca a chave por um token temporario (" + to_string(cfg.sessaoHoras) + " h).");
  if (cfg.proxyConfiavel && cfg.escutar.rfind("127.", 0) != 0) add('X', "proxy_confiavel=sim mas o Nodo escuta fora do PC: qualquer um pode acessar a porta direto e forjar o IP e o HTTPS dos cabecalhos.");
  if (cfg.descoberta) add('A', "descoberta=sim: so use em rede local confiavel, nunca em servidor publico.");
  else add('O', "Busca automatica de PCs desligada.");
  if (cfg.escritaLivre) add('X', "escrita_livre=sim: visitantes podem enviar QUALQUER campo nas colecoes de escrita publica. Defina campos.<colecao> e volte para nao.");
  for (auto& c : cfg.escrita) {
    if (cfg.campos.count(c)) {
      string lista;
      for (auto& kv : cfg.campos[c]) lista += (lista.empty() ? "" : ", ") + kv.first + "(" + to_string(kv.second) + ")";
      add('O', "Escrita publica em '" + c + "' restrita aos campos: " + lista);
    } else if (cfg.escritaLivre) add('A', "Escrita publica em '" + c + "' aceita qualquer campo.");
    else add('X', "Escrita publica em '" + c + "' SEM campos permitidos: o Nodo vai recusar os envios. Defina: campos." + c + "=nome:80,texto:1000");
    size_t lim = cfg.maxDocsCol.count(c) ? cfg.maxDocsCol[c] : cfg.maxDocs;
    if (lim > 50000) add('A', "A colecao '" + c + "' aceita ate " + to_string(lim) + " documentos de visitantes. Reduza com max_docs." + c + "=5000 (um robo pode encher o disco).");
  }
  for (auto& c : cfg.leitura) add('A', "Qualquer visitante pode LER a colecao '" + c + "'. Confirme que nao ha dados privados nela.");
  if (cfg.origens.empty()) {
    if (!cfg.escrita.empty() || !cfg.leitura.empty()) add('A', "origens vazio: qualquer site da internet pode ler/enviar para as colecoes publicas pelo navegador do visitante. Defina origens=https://seusite.com");
  } else if (cfg.origens.count("*")) add('A', "origens inclui '*': qualquer site pode usar as colecoes publicas.");
  else add('O', "CORS restrito aos sites: " + joinSet(cfg.origens));
  string gi;
  readFile(".gitignore", gi);
  if (fs::exists(".git") && gi.find("nodo.conf") == string::npos) add('X', "A pasta usa git e o nodo.conf nao esta no .gitignore: a chave pode vazar.");
  error_code ec;
  bool temBackup = fs::exists(fs::u8path(cfg.dados + "/backups"), ec);
  add(temBackup ? 'O' : 'A', temBackup ? "Ha backups automaticos em nodo-dados/backups." : "Nenhum backup ainda. Rode: nodo backup");
  if (!cfg.tsUrl.empty() || cfg.proxyConfiavel) add('O', "Publicacao com HTTPS configurada (" + (cfg.tsUrl.empty() ? string("proxy confiavel") : cfg.tsUrl) + ").");
  else add('A', "Lembrete: o Nodo nao faz HTTPS sozinho. Para a internet, use Tailscale Funnel ou ponha o Caddy na frente (veja SEGURANCA.md).");
  if (!peersList().empty()) add('A', "Ha PCs conectados na replicacao: a chave viaja entre eles em HTTP. So conecte pela rede local ou pelo Tailscale (criptografado).");
  return o;
}

