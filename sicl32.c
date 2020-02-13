#include "config.h"
#include "wine/port.h"

#include <stdarg.h>
#ifdef HAVE_GPIB_NI4882_H
#include <ni4882.h>
#endif

#include "windef.h"
#include "winbase.h"
#include "wine/debug.h"
#include "sicl.h"
#include "winreg.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdarg.h>
#include <netdb.h>
#include <sys/socket.h>
#include <arpa/inet.h>



WINE_DEFAULT_DEBUG_CHANNEL(sicl);

static int sfifofd = -1;
static int cfifofd = -1;
static errorproc_t errfunc = NULL;
static int fifo = 1;

BOOL WINAPI DllMain (HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    TRACE("%p,%x,%p\n", hinstDLL, fdwReason, lpvReserved);

    switch (fdwReason)
    {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(hinstDLL);
            //g_startedEvent = CreateEventA(NULL,TRUE,TRUE,NULL);
            TRACE("sfifofd=%d cfifofd=%d\n", sfifofd, cfifofd);
            break;
        case DLL_PROCESS_DETACH:
            if (lpvReserved) break;
            //CloseHandle(g_startedEvent);
            _siclcleanup();
            break;
    }
    return TRUE;
}

static int setfifos ()
{
  char conn[255];
  int bufsz = 255;
  int port = 0;
  char * addr = NULL;
  RegGetValueA(HKEY_CURRENT_USER, "SOFTWARE\\Wine\\Gpib", "Connection", RRF_RT_ANY, NULL, (PVOID)conn, &bufsz);
  if (*conn) {
      char * del = strchr(conn, ':');
      if (del) {
        *del = 0;
        addr = conn;
        port = atoi(del +1);
        fifo=0;
      }
  }
  if (fifo) {
    if (sfifofd == -1)
      sfifofd = open("/tmp/ssiclfifo", O_WRONLY);
    if (cfifofd == -1)
      cfifofd = open("/tmp/csiclfifo", O_RDONLY);
   } else {
    if (sfifofd != -1 && cfifofd != -1) return 0;
    if (port == 0 || !addr) return -1;
    int sockfd;
    struct sockaddr_in servaddr;

    // socket create and varification
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        TRACE("socket creation failed...\n");
    }
    else
        TRACE("Socket successfully created..\n");
    bzero(&servaddr, sizeof(servaddr));

    // assign IP, PORT
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = inet_addr(addr);
    servaddr.sin_port = htons(port);

    // connect the client socket to server socket
    if (connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) != 0) {
        TRACE("connection with the server failed...\n");
    }
    else {
        TRACE("connected to the server..\n");
        sockfd = -1;
    }
    cfifofd = sfifofd = sockfd;
  }
  return 0;
}

static void wrbuf(char * buf, const char * fmt, ...)
{
  va_list args;
  int l, n;
  va_start(args, fmt);
  l = vsprintf(buf, fmt, args);
  va_end(args);
  n=write(sfifofd, buf, l);
  TRACE("written %d\n", n);
  if (n <= 0) {
     close(sfifofd);
     sfifofd = -1;
  }
}

int rdbuf(char * buf, int l)
{
  int n;
  n=read (cfifofd, buf, l);
  TRACE("read %d\n", n);
  if (n <= 0) {
     close(cfifofd);
     cfifofd = -1;
     strcpy(buf, "-1");
  } else {
     buf[n] = 0;
  }
  return n;
}

INST SICLAPI iopen(char _far *addr) {
  int retval = 0;
  char buf [255];
  TRACE(" open %s \n", addr);
  setfifos();
  wrbuf(buf, "iopen \"%s\"\n", addr);
  rdbuf(buf, 255);
  retval = atoi(buf);
  return retval;
}

int SICLAPI iclose(INST id){
  int retval = 0;
  char buf [255];
  TRACE(" id %d \n", id);
  setfifos();
  wrbuf(buf, "iclose %d\n", id);
  rdbuf(buf, 255);
  retval = atoi(buf);
  return retval;
}

INST SICLAPI igetintfsess(INST id){
  int retval = 0;
  char buf [255];
  TRACE(" id %d \n", id);
  setfifos();
  wrbuf(buf, "igetinftsess %d\n", id);
  rdbuf(buf, 255);
  retval = atoi(buf);
  return retval;
}

int SICLAPI iwrite (
   INST id,
   char _far *buf,
   unsigned long datalen,
   int endi,
   unsigned long _far *actual
){
  int retval = 0;
  int n = 0;
  int actl;
  char bufc [255];
  char *ptr, *rslp;
  TRACE(" id %d \n", id);
  setfifos();
  wrbuf(bufc, "iwrite %d,%d,%d,%d#", id,datalen, endi,datalen);
  n=write(sfifofd, buf, datalen);
  TRACE("written(bin) %d\n", n);
  if (n > 0  || datalen == 0) n=write(sfifofd, "\n", 1);
  TRACE("written(term) %d\n", n);
  if (n <= 0) {
    close(sfifofd);
    sfifofd = -1;
  }
  rdbuf(bufc, 255);
  retval  = atoi((rslp = strtok_r(bufc, ",", &ptr)) ? rslp : "0");
  actl  = atoi((rslp = strtok_r(NULL, ",", &ptr)) ? rslp : "0");
  if (actual) *actual = actl;
  return retval;
}
int SICLAPI iread (
   INST id,
   char _far *buf,
   unsigned long bufsize,
   int _far *reason,
   unsigned long _far *actual
){
  int retval = 0;
  int n = 0;
  int rsn, act, blen;
  char bufc [8192];
  char *ptr, *rem, *rslp;

  TRACE(" id %d \n", id);
  setfifos();
  wrbuf(bufc, "iread %d,%d\n", id,bufsize);
  act = rdbuf(bufc, 8192);
  retval  = atoi(strtok_r(bufc, ",", &ptr));
  rsn  = atoi((rslp = strtok_r(NULL, ",", &ptr)) ? rslp : "0");
  act  = atoi((rslp = strtok_r(NULL, ",", &ptr)) ? rslp : "0");
  blen  = atoi((rslp = strtok_r(NULL, ",#", &ptr)) ? rslp : "0");
  memcpy (buf, ptr, blen);
  TRACE("rem blen  %d\n", blen);
  if (actual) *actual = act;
  if (reason) *reason = rsn;
  return retval;
}

int SICLAPI ireadstb(INST id,unsigned char _far *stb){
  int retval = 0;
  char buf [255];
  char *ptr, *rslp;
  int rsl;
  TRACE(" id %d \n", id);
  setfifos();
  wrbuf(buf, "ireadstb %d\n", id);
  rdbuf(buf, 255);
  retval  = atoi(strtok_r(buf, ",", &ptr));
  rslp  = strtok_r(NULL, ",", &ptr);
  if (!rslp) rslp = "0";
  rsl  = atoi(rslp);
  if (stb) *stb  = rsl;
  return retval;
}
int SICLAPI itimeout(INST id,long tval){
  int retval = 0;
  char buf [255];
  TRACE(" id %d \n", id);
  setfifos();
  wrbuf(buf, "itimeout %d,%d\n", id, tval);
  rdbuf(buf, 255);
  retval = atoi(buf);
  return retval;
}
int SICLAPI iclear(INST id){
  int retval = 0;
  char buf [255];
  TRACE(" id %d \n", id);
  setfifos();
  wrbuf(buf, "iclear %d\n", id);
  rdbuf(buf, 255);
  retval = atoi(buf);
  return retval;
}
int SICLAPI ihint(INST id,int hint){
  int retval = 0;
  char buf [255];
  TRACE(" id %d \n", id);
  setfifos();
  wrbuf(buf, "ihint %d,%d\n", id, hint);
  rdbuf(buf, 255);
  retval = atoi(buf);
  return retval;
}
int SICLAPI igpibbusstatus (INST id, int request, int _far *result){
  int retval = 0;
  char buf [255];
  char *ptr, *rslp;
  int rsl;
  TRACE(" id %d \n", id);
  setfifos();
  wrbuf(buf, "igpibbusstatus %d,%d\n", id, request);
  rdbuf(buf, 255);
  retval  = atoi(strtok_r(buf, ",", &ptr));
  rslp  = strtok_r(NULL, ",", &ptr);
  if (!rslp) rslp = "0";
  rsl  = atoi(rslp);
  if (result) *result = rsl;
  return retval;
}

int SICLAPI igpibppoll (INST id, unsigned int _far *result){
  int retval = 0;
  char buf [255];
  char *ptr, *rslp;
  int rsl;
  TRACE(" id %d \n", id);
  setfifos();
  wrbuf(buf, "igpibppoll %d\n", id);
  rdbuf(buf, 255);
  retval  = atoi(strtok_r(buf, ",", &ptr));
  rslp  = strtok_r(NULL, ",", &ptr);
  if (!rslp) rslp = "0";
  rsl  = atoi(rslp);
  if (result) *result  = rsl;
  return retval;
}
int SICLAPI igpibsendcmd (INST id, char _far *buf, int length) {
  int retval = 0;
  int n = 0;
  char bufc [255];
  TRACE(" id %d \n", id);
  setfifos();
  wrbuf(bufc, "igpibsendcmd %d,%d,%d#", id,length,length);
  n=write(sfifofd, buf, length);
  TRACE("written(bin) %d\n", n);
  if (n > 0 || length == 0) n=write(sfifofd, "\n", 1);
  TRACE("written(term) %d\n", n);
  if (n <= 0) {
    close(sfifofd);
    sfifofd = -1;
  }
  rdbuf(buf, 255);
  retval = atoi(bufc);
  return retval;
}

int SICLAPI ionerror(errorproc_t errcall){
  TRACE(" errproc %p \n", errcall);
  errfunc = errcall;
  return 0;
}
 /* process cleanup for Win 3.1*/
int SICLAPI  _siclcleanup(void) {
  if (sfifofd >= 0)
    close(sfifofd);
  if (cfifofd >= 0)
    close(cfifofd);
  sfifofd = cfifofd = -1;
  TRACE(" \n");
  return 0;
}
