#define DEBUG_SOCKET
#define DEBUG_IP "192.168.1.218"
#define DEBUG_PORT 19023

#include "ps4.h"
#include "network.h"
#include "pthread.h"

#define NZERO(_x_) memset(&(_x_), 0, sizeof(_x_))
#define NPORT 19020

void installPackage(char* pkgname) {
  
}

void* downloadThread(void* psock) {
  char buff[4096];
  char name[32];
  int sock, bsread, pkgfd;
  
  sock = ((int)((intptr_t)psock));
  bsread = -1;
  
  NZERO(name);
  NZERO(buff);
  
  snprintf(name, sizeof(name) - 1, "/user/home/nik_pkg_%d.pkg", sock + 100);
  pkgfd = open(name, O_WRONLY, O_CREAT | O_TRUNC);
  if (pkgfd < 0) return (void*)pkgfd;
  
  printf_debug("[listenthr:download] getting pkg, name = %s\n", name);
  
  while (1) {
    bsread = sceNetRecv(sock, buff, sizeof(buff), 0);
    if (bsread <= 0) break;
    write(pkgfd, buff, bsread);
  }
  
  close(pkgfd);
  pkgfd = -1;
  sceNetShutdown(sock, 2 /* SHUT_RDWR */);
  sceNetSocketClose(sock);
  sock = -1;
  
  // install it :3
  printf_debug("[listenthr:download] installing pkg, name = %s\n", name);
  installPackage(name);
  
  return (void*)0;
}

void* listenThread(void* unused) {
  int sock, ok;
  ScePthread downloadTd;
  struct in_addr sin;
  
  NZERO(sin);
  sin.sin_len = sizeof(sin);
  sin.sin_family = AF_INET;
  sin.sin_port = htons(NPORT);
  sin.sin_addr.s_addr = IN_ADDR_ANY;
  ok = 0;
  sock = -1;
  
  // make a listener socket
  ok = sock = sceNetSocket("nik:listensck", AF_INET, SOCK_STREAM, 0);
  if (ok < 0) goto l_err;
  ok = sceNetBind(sock, &sin, sizeof(sin));
  if (ok < 0) goto l_err;
  
  while (1) {
    // listen
    ok = sceNetListen(sock, 4096);
    printf_debug("[listenthr:accept] listen = %d\n", ok);
    if (ok < 0) goto l_err;
    // make a socket
    ok = connsock = sceNetAccept(sock, &sin, sizeof(sin));
    printf_debug("[listenthr:accept] socket = %d\n", ok);
    if (ok < 0) goto l_err;
    // spin a thread
    ok = scePthreadCreate(&downloadTd, NULL, &downloadThread, (void*)ok, "nik:getthr");
    printf_debug("[listenthr:accept] ok = %d\n", ok);
    if (ok < 0) goto l_err;
  }
  
l_err:
  printf_debug("[listenthr:shutdown] shutting down...\n");
  sceNetShutdown(sock, 2 /* SHUT_RDWR */);
  sceNetSocketClose(sock);
  sock = -1;
  printf_debug("[listenthr:shutdown] done.\n");
  return (void*)ok;
}

int _main(struct thread *td) {
  ScePthread listenTd;
  void* threadRet;
  int ok;
  UNUSED(td);

  initKernel();
  initLibc();

#ifdef DEBUG_SOCKET
  initNetwork();
  DEBUG_SOCK = SckConnect(DEBUG_IP, DEBUG_PORT);
#endif

  jailbreak();
  initSysUtil();
  initNetwork();
  initPthread();
  ok = -1;
  
  printf_notification("Starting...");
  threadRet = NULL;
  ok = scePthreadCreate(&listenTd, NULL, &listenThread, NULL, "nik:listenthr");
  printf_debug("[main]: Started = %d\n", ok);
  ok = scePthreadJoin(&listenTd, &threadRet);
  printf_debug("[main]: Return code = %d, 0x%llX.", ok, ((long long)((intptr_t)threadRet)));
  
  printf_notification("Goodbye.");

#ifdef DEBUG_SOCKET
  printf_debug("Closing socket...\n");
  SckClose(DEBUG_SOCK);
#endif

  return 0;
}
