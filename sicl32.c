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
#include <netinet/tcp.h>



WINE_DEFAULT_DEBUG_CHANNEL(sicl);

static int sfifofd = -1;
static int cfifofd = -1;
static errorproc_t errfunc = NULL;
static int fifo = 1;

static HANDLE ghExcl = NULL;

static CRITICAL_SECTION Excl; 

BOOL WINAPI DllMain (HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    TRACE("%p,%x,%p\n", hinstDLL, fdwReason, lpvReserved);

    switch (fdwReason)
    {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(hinstDLL);
            //g_startedEvent = CreateEventA(NULL,TRUE,TRUE,NULL);
            TRACE("sfifofd=%d cfifofd=%d\n", sfifofd, cfifofd);
             if (!ghExcl) ghExcl = CreateMutexA( 
                  NULL,              // default security attributes
                  FALSE,             // initially not owned
                  NULL); 
             //if (!is_init) {
             // InitializeCriticalSectionAndSpinCount(&Excl, 0x00000400);
             // is_init = 1;
             //}
            break;
        case DLL_PROCESS_DETACH:
            if (lpvReserved) break;
            //CloseHandle(g_startedEvent);
            _siclcleanup();
    	    DeleteCriticalSection(&Excl);
            break;
    }
    return TRUE;
}


static int critsec()
{
  //EnterCriticalSection(&Excl);
  DWORD dwWaitResult = WaitForSingleObject( 
            ghExcl,    // handle to mutex
            INFINITE);  // no time-out interval
 
        switch (dwWaitResult) 
        {
            // The thread got ownership of the mutex
            case WAIT_OBJECT_0: 
                return TRUE; 

            // The thread got ownership of an abandoned mutex
            // The database is in an indeterminate state
            case WAIT_ABANDONED: 
                return FALSE; 
        }
    
}

static int end_critsec(int gotit)
{
  if (gotit) ReleaseMutex(ghExcl);
  //LeaveCriticalSection(&Excl);
  return 0;
}

static int connsock(int port, char * ip)
{
    int sockfd;
    struct sockaddr_in servaddr;

    // socket create and varification
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        TRACE("socket creation failed...\n");
        cfifofd = sfifofd = sockfd;
        return -1;
    }
    else
        TRACE("Socket successfully created..\n");
    //memset(&servaddr, 0, sizeof(struct sockaddr_in));

    // assign IP, PORT
    servaddr.sin_family = AF_INET;
    if (ip) servaddr.sin_addr.s_addr = inet_addr(ip);
    else servaddr.sin_addr.s_addr = inet_addr("127.0.0.1");
    servaddr.sin_port = htons(port);
    
    int flag = 1;
    int result = setsockopt(sockfd,            /* socket affected */
                                 IPPROTO_TCP,     /* set option at TCP level */
                                 TCP_NODELAY,     /* name of option */
                                 &flag,  
                                 sizeof(int));  
    // connect the client socket to server socket
    if (connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) != 0) {
        TRACE("connection with the server failed...\n");
        sockfd = -1;
    } else {
        TRACE("connected to the server..\n");
    }
    return sockfd;
}

static int setfifos (void)
{
  char * conn = malloc(255);
  unsigned bufsz = 255;
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
    TRACE("fifo\n");
    if (sfifofd == -1)
      sfifofd = open("/tmp/ssiclfifo", O_WRONLY);
    if (cfifofd == -1)
      cfifofd = open("/tmp/csiclfifo", O_RDONLY);
  } else {
    if (sfifofd != -1 && cfifofd != -1) return 0;
    if (port == 0 || !addr) { free (conn); return -1;}
    cfifofd = connsock(port, addr);
    sfifofd = cfifofd;
    if (cfifofd == -1) { free (conn); return -1;}
  }
  free(conn);
  return 0;
}

static void wrbuf(char * binbuf, int binlen, const char * fmt, ...)
{
  va_list args;
  int l, n;
  char * wbuf = malloc(8192);  
  va_start(args, fmt);
  l = vsprintf(wbuf, fmt, args);
  va_end(args);
  if (binlen) {
    memcpy(wbuf+l, binbuf, binlen);
    l += binlen;
    wbuf[l] = '\n';
    l++;
  } 
  n=write(sfifofd, wbuf, l);
  TRACE("written %d\n", n);
  if (n <= 0) {
     close(sfifofd);
     sfifofd = -1;
  } 
  if (n != l) {
    WARN("write mismatch %d %d\n", n, l);
  }
  free (wbuf);
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
  int crit = critsec();
  TRACE(" open %s \n", addr);
  if(setfifos() != 0) {
     end_critsec(crit);
     if (errfunc) errfunc(-1, -1);
     return -1;
  }
  wrbuf(NULL, 0, "iopen \"%s\"\n", addr);
  rdbuf(buf, 255);
  retval = atoi(buf);
  end_critsec(crit);
  if (retval < 0 && errfunc) errfunc(-1, retval);
  return retval;
}

int SICLAPI iclose(INST id){
  int retval = 0;
  char buf [255];
  int crit = critsec();
  TRACE(" id %d \n", id);
  if(setfifos() != 0) {
     end_critsec(crit);
     if (errfunc) errfunc(id, -1);
     return -1;
  }
  wrbuf(NULL, 0, "iclose %d\n", id);
  rdbuf(buf, 255);
  retval = atoi(buf);
  end_critsec(crit);
  if (retval < 0 && errfunc) errfunc(id, retval);
  return retval;
}

INST SICLAPI igetintfsess(INST id){
  int retval = 0;
  char buf [255];
  int crit = critsec();
  TRACE(" id %d \n", id);
  if(setfifos() != 0) {
     end_critsec(crit);
     if (errfunc) errfunc(id, -1);
     return -1;
  }
  wrbuf(NULL, 0, "igetinftsess %d\n", id);
  rdbuf(buf, 255);
  retval = atoi(buf);
  end_critsec(crit);
  if (retval < 0 && errfunc) errfunc(id, retval);
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
  int actl;
  char bufc [8192];
  char *ptr; 
  const char *rslp;
  int crit = critsec();
  TRACE(" id %d \n", id);
  if(setfifos() != 0) {
     end_critsec(crit);
     if (errfunc) errfunc(id, -1);
     return -1;
  }
  wrbuf(buf, datalen, "iwrite %d,%d,%d,%d#", id,datalen, endi,datalen);
  rdbuf(bufc, 255);
  retval  = atoi((rslp = strtok_r(bufc, ",", &ptr)) ? rslp : "0");
  actl  = atoi((rslp = strtok_r(NULL, ",", &ptr)) ? rslp : "0");
  if (actual) *actual = actl;
  end_critsec(crit);
  if (retval < 0 && errfunc) errfunc(id, retval);
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
  int rsn, act, blen;
  char bufc [8192];
  char *ptr, *rem, *rslp;
  int crit = critsec();

  TRACE(" id %d \n", id);
  if(setfifos() != 0) {
     end_critsec(crit);
     if (errfunc) errfunc(id, -1);
     return -1;
  }
  wrbuf(NULL, 0, "iread %d,%d\n", id,bufsize);
  act = rdbuf(bufc, 8192);
  ptr = bufc;
  rem = bufc;
  while (*ptr != ',' && *ptr != '\n') ptr++;
  *ptr = 0;
  retval = atoi (rem);
  rem = ptr+1;  
  while (*ptr != ',' && *ptr != '\n') ptr++;
  *ptr = 0;
  rsn = atoi(rem);
  rem = ptr+1;  
  while (*ptr != ',' && *ptr != '\n') ptr++;
  *ptr = 0;
  act = atoi(rem);
  rem = ptr+1;  
  while (*ptr != '#' && *ptr != '\n') ptr++;
  *ptr = 0;
  blen = atoi(rem);
  rslp = rem;
  rem = ptr+1;  
  memcpy (buf, rem, blen);
  TRACE("retval %d rsn %d act %d rem blen  %s %d\n", retval, rsn, act, rslp, blen);
  if (blen == 1) TRACE ("byval = %d\n", (int) *buf); 
  if (blen == 0) WARN("zero blen retval %d rsn %d act %d rem blen %s\n", retval, rsn, act, rslp);
  if (actual) *actual = act;
  if (reason) *reason = rsn;
  end_critsec(crit);
  if (retval < 0 && errfunc) errfunc(id, retval);
  return retval;
}

int SICLAPI ireadstb(INST id,unsigned char _far *stb){
  int retval = 0;
  char buf [255];
  char *ptr;
  const char *rslp;
  int rsl;
  int crit = critsec();
  TRACE(" id %d \n", id);
  if(setfifos() != 0) {
     end_critsec(crit);
     if (errfunc) errfunc(id, -1);
     return -1;
  }
  wrbuf(NULL, 0, "ireadstb %d\n", id);
  rdbuf(buf, 255);
  retval  = atoi(strtok_r(buf, ",", &ptr));
  rslp  = strtok_r(NULL, ",", &ptr);
  if (!rslp) rslp = "0";
  rsl  = atoi(rslp);
  if (stb) *stb  = rsl;
  end_critsec(crit);
  if (retval < 0 && errfunc) errfunc(id, retval);
  return retval;
}
int SICLAPI itimeout(INST id,long tval){
  int retval = 0;
  char buf [255];
  int crit = critsec();
  TRACE(" id %d \n", id);
  if(setfifos() != 0) {
     end_critsec(crit);
     return -1;
  }
  wrbuf(NULL, 0, "itimeout %d,%d\n", id, tval);
  rdbuf(buf, 255);
  retval = atoi(buf);
  end_critsec(crit);
  if (retval < 0) errfunc(id, retval);
  return retval;
}
int SICLAPI iclear(INST id){
  int retval = 0;
  char buf [255];
  int crit = critsec();
  TRACE(" id %d \n", id);
  if(setfifos() != 0) {
     end_critsec(crit);
     if (errfunc) errfunc(id, -1);
     return -1;
  }
  wrbuf(NULL, 0, "iclear %d\n", id);
  rdbuf(buf, 255);
  retval = atoi(buf);
  end_critsec(crit);
  if (retval < 0 && errfunc) errfunc(id, retval);
  return retval;
}
int SICLAPI ihint(INST id,int hint){
  int retval = 0;
  char buf [255];
  int crit = critsec();
  TRACE(" id %d \n", id);
  if(setfifos() != 0) {
     end_critsec(crit);
     if (errfunc) errfunc(id, -1);
     return -1;
  }
  wrbuf(NULL, 0, "ihint %d,%d\n", id, hint);
  rdbuf(buf, 255);
  retval = atoi(buf);
  end_critsec(crit);
  if (retval < 0 && errfunc) errfunc(id, retval);
  return retval;
}
int SICLAPI igpibbusstatus (INST id, int request, int _far *result){
  int retval = 0;
  char buf [255];
  char *ptr;
  const char  *rslp;
  int rsl;
  int crit = critsec();
  TRACE(" id %d \n", id);
  if(setfifos() != 0) {
     end_critsec(crit);
     if (errfunc) errfunc(id, -1);
     return -1;
  }
  wrbuf(NULL, 0, "igpibbusstatus %d,%d\n", id, request);
  rdbuf(buf, 255);
  retval  = atoi(strtok_r(buf, ",", &ptr));
  rslp  = strtok_r(NULL, ",", &ptr);
  if (!rslp) rslp = "0";
  rsl  = atoi(rslp);
  if (result) *result = rsl;
  end_critsec(crit);
  if (retval < 0 && errfunc) errfunc(id, retval);
  return retval;
}

int SICLAPI igpibppoll (INST id, unsigned int _far *result){
  int retval = 0;
  char buf [255];
  char *ptr;
  const char *rslp;
  int rsl;
  int crit = critsec();
  TRACE(" id %d \n", id);
  if(setfifos() != 0) {
     end_critsec(crit);
     if (errfunc) errfunc(id, -1);
     return -1;
  }
  wrbuf(NULL, 0, "igpibppoll %d\n", id);
  rdbuf(buf, 255);
  retval  = atoi(strtok_r(buf, ",", &ptr));
  rslp  = strtok_r(NULL, ",", &ptr);
  if (!rslp) rslp = "0";
  rsl  = atoi(rslp);
  if (result) *result  = rsl;
  end_critsec(crit);
  if (retval < 0 && errfunc) errfunc(id, retval);
  return retval;
}

int SICLAPI igpibsendcmd (INST id, char _far *buf, int length) {
  int retval = 0;
  char bufc [255];
  int crit = critsec();
  TRACE(" id %d \n", id);
  if(setfifos() != 0) {
     end_critsec(crit);
     if (errfunc) errfunc(id, -1);
     return -1;
  }
  wrbuf(buf, length, "igpibsendcmd %d,%d,%d#", id,length,length);
  rdbuf(bufc, 255);
  retval = atoi(bufc);
  end_critsec(crit);
  if (retval < 0 && errfunc) errfunc(id, retval);
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

void I_ERROR_EXIT (int id, int err)
{
  WARN("sicl error %d id %d\n", id, err);
  exit(1);
}

void I_ERROR_NO_EXIT(int id, int err)
{
  WARN("sicl error %d id %d\n", id, err);

}
