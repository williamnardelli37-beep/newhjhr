#pragma once
// Nodo - camada de plataforma: mesmo codigo no Mac, Linux e Windows (MinGW).
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>
#include <chrono>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <windows.h>
#include <bcrypt.h>
#include <io.h>
#include <direct.h>
#include <sys/stat.h>
#define SHUT_WR SD_SEND
static inline void cerrar(int fd) { closesocket(fd); }
static inline int nodoChmod(const char*, int) { return 0; }
static inline int nodoMkdir(const char* p, int) { return _mkdir(p); }
static inline bool esperaLeitura(int fd, int ms) { WSAPOLLFD p; p.fd = fd; p.events = POLLRDNORM; p.revents = 0; return WSAPoll(&p, 1, ms) > 0; }
static inline void setInt(int fd, int lvl, int opt, int v) { setsockopt(fd, lvl, opt, (const char*)&v, sizeof v); }
static inline void redeInit() { static bool ok = false; if (!ok) { WSADATA w; WSAStartup(MAKEWORD(2, 2), &w); ok = true; } }
static inline void sockTimeout(int fd, int seg) {
  DWORD ms = seg * 1000;
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&ms, sizeof ms);
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (const char*)&ms, sizeof ms);
}
static inline void sockReuse(int fd) { (void)fd; }  // no Windows SO_REUSEADDR permitiria roubar a porta: nao usar
static inline bool bytesAleatorios(unsigned char* p, size_t n) { return BCryptGenRandom(nullptr, p, (ULONG)n, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0; }
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
static inline void cerrar(int fd) { ::close(fd); }
static inline int nodoChmod(const char* p, int m) { return ::chmod(p, m); }
static inline int nodoMkdir(const char* p, int m) { return ::mkdir(p, m); }
static inline bool esperaLeitura(int fd, int ms) { pollfd p; p.fd = fd; p.events = POLLIN; p.revents = 0; return poll(&p, 1, ms) > 0; }
static inline void setInt(int fd, int lvl, int opt, int v) { setsockopt(fd, lvl, opt, &v, sizeof v); }
static inline void redeInit() {}
static inline void sockTimeout(int fd, int seg) {
  timeval tv{seg, 0};
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
}
static inline void sockReuse(int fd) { int on = 1; setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on); }
static inline bool bytesAleatorios(unsigned char* p, size_t n) {
  FILE* f = fopen("/dev/urandom", "rb");
  if (!f) return false;
  bool ok = fread(p, 1, n, f) == n;
  fclose(f);
  return ok;
}
#endif
static inline void dormirMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
namespace fs = std::filesystem;
