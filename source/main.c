#define DEBUG_SOCKET
#define DEBUG_IP "192.168.1.218"
#define DEBUG_PORT 19023

#include "ps4.h"
#include "network.h"
#include "pthread.h"

#define NZERO(_x_) memset(&(_x_), 0, sizeof(_x_))
#define NPORT 19020

enum bgft_task_option_t {
	BGFT_TASK_OPTION_NONE = 0x0,
	BGFT_TASK_OPTION_DELETE_AFTER_UPLOAD = 0x1,
	BGFT_TASK_OPTION_INVISIBLE = 0x2,
	BGFT_TASK_OPTION_ENABLE_PLAYGO = 0x4,
	BGFT_TASK_OPTION_FORCE_UPDATE = 0x8,
	BGFT_TASK_OPTION_REMOTE = 0x10,
	BGFT_TASK_OPTION_COPY_CRASH_REPORT_FILES = 0x20,
	BGFT_TASK_OPTION_DISABLE_INSERT_POPUP = 0x40,
	BGFT_TASK_OPTION_DISABLE_CDN_QUERY_PARAM = 0x10000,
};

struct bgft_download_param {
	int user_id;
	int entitlement_type;
	const char* id;
	const char* content_url;
	const char* content_ex_url;
	const char* content_name;
	const char* icon_path;
	const char* sku_id;
	enum bgft_task_option_t option;
	const char* playgo_scenario_id;
	const char* release_date;
	const char* package_type;
	const char* package_sub_type;
	unsigned long package_size;
};

struct bgft_download_param_ex {
	struct bgft_download_param param;
	unsigned int slot;
};

struct bgft_task_progress_internal {
	unsigned int bits;
	int error_result;
	unsigned long length;
	unsigned long transferred;
	unsigned long length_total;
	unsigned long transferred_total;
	unsigned int num_index;
	unsigned int num_total;
	unsigned int rest_sec;
	unsigned int rest_sec_total;
	int preparing_percent;
	int local_copy_percent;
};

#define BGFT_INVALID_TASK_ID (-1)

struct bgft_init_params {
	void* mem;
	unsigned long size;
};

int(*sceBgftServiceIntInit)(struct bgft_init_params* in_initParams);
int(*sceBgftServiceIntDownloadRegisterTaskByStorage)(struct bgft_download_param* in_dlParams, int* out_taskId);
int(*sceBgftServiceIntDownloadStartTask)(int in_taskId);
int libBgft;

void cleanupFiles() {
  char name[64];
  for (int i = 0; i < 1000; ++i) {
    NZERO(name);
    snprintf(name, sizeof(name) - 1, "/user/home/pkg_%d.pkg", i + 100);
    unlink(name);
  }
}

void installPackage(char* pkgname) {
  int tid, ok;
  tid = BGFT_INVALID_TASK_ID;
  ok = 0;
  
  struct bgft_download_param params = {
    .entitlement_type = 5,
    .id = "",
    .content_url = pkgname,
    .content_name = &pkgname[11],
    .icon_path = "",
    .playgo_scenario_id = "0",
    .option = BGFT_TASK_OPTION_DISABLE_CDN_QUERY_PARAM,
  };
  
  ok = sceBgftServiceIntDownloadRegisterTaskByStorage(&params, &tid);
  printf_debug("[listenthr:install] terr1 = %x\n", ok);
  if (ok < 0) return;
  ok = sceBgftServiceIntDownloadStartTask(tid);
  printf_debug("[listenthr:install] terr2 = %x\n", ok);
}

void* downloadThread(void* psock) {
  char buff[8192];
  char name[32];
  int sock, bsread, pkgfd;
  
  sock = ((int)((intptr_t)psock));
  bsread = 0;
  
  NZERO(name);
  NZERO(buff);
  
  snprintf(name, sizeof(name) - 1, "/user/home/pkg_%d.pkg", sock + 100);
  printf_debug("[listenthr:download] name = %s\n", name);
  pkgfd = open(name, O_WRONLY | O_CREAT | O_TRUNC, 0777);
  if (pkgfd < 0) {
    printf_debug("[listenthr:download] err = %d\n", pkgfd);
    *((int*)NULL) = 0;
    return ((void*)((intptr_t)pkgfd));
  }
  
  printf_debug("[listenthr:download] getting pkg, fd = %d, name = %s\n", pkgfd, name);
  
  while (1) {
    bsread = sceNetRecv(sock, buff, sizeof(buff), 0);
    //printf_debug("[listenthr:download] bsread = %d\n", bsread);
    if (bsread <= 0) break;
    //write(pkgfd, buff, bsread);
    
  }
  
  close(pkgfd);
  pkgfd = -1;
  sceNetSocketAbort(sock, 0);
  sceNetSocketClose(sock);
  sock = -1;
  
  // install it :3
  printf_debug("[listenthr:download] installing pkg, name = %s\n", name);
  installPackage(name);
  
  return (void*)0;
}

void* listenThread(void* unused) {
  int sock, ok, connsock;
  unsigned int slen;
  ScePthread downloadTd;
  struct sockaddr_in sin;
  UNUSED(unused);
  
  NZERO(sin);
  sin.sin_len = sizeof(sin);
  sin.sin_family = AF_INET;
  sin.sin_port = htons(NPORT);
  sin.sin_addr.s_addr = IN_ADDR_ANY;
  ok = 0;
  sock = -1;
  
  // make a listener socket
  ok = sceNetSocket("nik:listensck", AF_INET, SOCK_STREAM, 0);
  sock = ok;
  printf_debug("[listenthr:accept] sock = 0x%X\n", sock);
  if (ok < 0) goto l_err;
  ok = sceNetBind(sock, (struct sockaddr*)&sin, sizeof(sin));
  printf_debug("[listenthr:accept] bind = 0x%X\n", ok);
  if (ok < 0) goto l_err;
  
  while (1) {
    // listen
    ok = sceNetListen(sock, 4096);
    printf_debug("[listenthr:accept] listen = 0x%X\n", ok);
    if (ok < 0) goto l_err;
    // make a socket
    ok = sceNetAccept(sock, (struct sockaddr*)&sin, &slen);
    connsock = ok;
    printf_debug("[listenthr:accept] socket = 0x%X\n", connsock);
    if (ok < 0) goto l_err;
    // spin a thread
    ok = scePthreadCreate(&downloadTd, NULL, &downloadThread, ((void*)((intptr_t)connsock)), "nik:getthr");
    printf_debug("[listenthr:accept] ok = 0x%X\n", ok);
    if (ok < 0) goto l_err;
  }
  
l_err:
  printf_debug("[listenthr:shutdown] shutting down...\n");
  sceNetSocketAbort(sock, 0);
  sceNetSocketClose(sock);
  sock = -1;
  printf_debug("[listenthr:shutdown] done.\n");
  return ((void*)((intptr_t)ok));
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
  initModule();
  ok = -1;
  
  printf_notification("Starting...");
  threadRet = NULL;
  cleanupFiles();
  
  // load bgft module and resolve stuff...
  printf_debug("[main] preinit...\n");
  libBgft = sceKernelLoadStartModule("/system/common/lib/libSceBgft.sprx", 0, 0, 0, NULL, NULL);
  printf_debug("[main] bgft = 0x%X\n", libBgft);
  RESOLVE(libBgft, sceBgftServiceIntInit);
  RESOLVE(libBgft, sceBgftServiceIntDownloadRegisterTaskByStorage);
  RESOLVE(libBgft, sceBgftServiceIntDownloadStartTask);
  printf_debug("[main] bgft resolve = 0x%p\n", sceBgftServiceIntInit);
  
  struct bgft_init_params ip = {
    .mem = mmap(NULL, 0x100000, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0),
    .size = 0x100000,
  };
  
  ok = sceBgftServiceIntInit(&ip);
  printf_debug("[main]: bgft init = 0x%X\n", ok);
  
  ok = scePthreadCreate(&listenTd, NULL, &listenThread, NULL, "nik:listenthr");
  printf_debug("[main]: Started = 0x%X\n", ok);
  ok = scePthreadJoin(listenTd, &threadRet);
  printf_debug("[main]: Return code = 0x%X, 0x%llX.\n", ok, ((long long)((intptr_t)threadRet)));
  
  printf_notification("Goodbye.");

#ifdef DEBUG_SOCKET
  printf_debug("Closing socket...\n\n");
  SckClose(DEBUG_SOCK);
#endif

  return 0;
}
