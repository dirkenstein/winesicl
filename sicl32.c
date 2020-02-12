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

WINE_DEFAULT_DEBUG_CHANNEL(sicl);

BOOL WINAPI DllMain (HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    TRACE("%p,%x,%p\n", hinstDLL, fdwReason, lpvReserved);

    switch (fdwReason)
    {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(hinstDLL);
            //g_startedEvent = CreateEventA(NULL,TRUE,TRUE,NULL);
            break;
        case DLL_PROCESS_DETACH:
            if (lpvReserved) break;
            //CloseHandle(g_startedEvent);
            break;
    }

    return TRUE;
}

INST SICLAPI iopen(char _far *addr) {
  TRACE(" open %s \n", addr);
  return ibfindA(addr);
}

int SICLAPI iclose(INST id){
  TRACE(" id %d \n", id);
  return 0;
}

INST SICLAPI igetintfsess(INST id){
  TRACE(" id %d \n", id);
  return 2;
}

int SICLAPI iwrite (
   INST id,
   char _far *buf,
   unsigned long datalen,
   int endi,
   unsigned long _far *actual
){
  TRACE(" id %d \n", id);
  return 0;

}
int SICLAPI iread (
   INST id,
   char _far *buf,
   unsigned long bufsize,
   int _far *reason,
   unsigned long _far *actual
){
  TRACE(" id %d \n", id);
  return 0;
}
int SICLAPI ireadstb(INST id,unsigned char _far *stb){
  TRACE(" id %d \n", id);
  return 0;
}
int SICLAPI itimeout(INST id,long tval){
  TRACE(" id %d \n", id);
  return 0;
}
int SICLAPI iclear(INST id){
  TRACE(" id %d \n", id);
  return 0;
}
int SICLAPI ihint(INST id,int hint){
  TRACE(" id %d \n", id);
  return 0;
}
int SICLAPI igpibbusstatus (INST id, int request, int _far *result){
  TRACE(" id %d \n", id);
  return 0;
}
int SICLAPI igpibppoll (INST id, unsigned int _far *result){
  TRACE(" id %d \n", id);
  return 0;
}
int SICLAPI igpibsendcmd (INST id, char _far *buf, int length) {
  TRACE(" id %d \n", id);
  return 0;
}
int SICLAPI ionerror(errorproc_t errcall){
  TRACE(" errproc %p \n", errcall);
  return 0;
}
 /* process cleanup for Win 3.1*/
int SICLAPI  _siclcleanup(void) {
  TRACE(" \n");
  return 0;
} 
