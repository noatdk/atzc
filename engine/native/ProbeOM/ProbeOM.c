#include <windows.h>
void *memcpy(void *d,const void *s,size_t n){unsigned char*a=d;const unsigned char*b=s;while(n--)*a++=*b++;return d;}
static void w(const char*s){DWORD n;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),s,lstrlenA(s),&n,0);}
static void ww(const wchar_t*s){char b[256];int n=WideCharToMultiByte(CP_ACP,0,s,-1,b,256,0,0);DWORD x;if(n>1)WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),b,n-1,&x,0);}
static const wchar_t* const fm[]={L"JsMmfMrpAT2db",L"ATOKNS_JsMmfMrpAT2db",L"JsMmfMrpAT2Db.10",L"JsMmfAtok19rtSharedMemory",L"ATOKNS_JsMmfAtok19rtSharedMemory",L"ATOKNS_JsMmfMrpAT2Db.10",L"JsMmf_ATOK31IB_EXEC_DATA_",L"ATOKNS_JsMmf_ATOK31IB_EXEC_DATA_",L"JsMmfAtok31wLaunchDataMemory"};
static const wchar_t* const mt[]={L"ATOK31OM_APP_MUTEX",L"ATOKNS_ATOK31OM_APP_MUTEX",L"JsMutexMrpAT2Dic.000",L"ATOKNS_JsMutexMrpAT2Dic.000",L"JsMtx_ATOK31ROMATABLE",L"ATOKNS_JsMtx_ATOK31ROMATABLE",L"JsMtxAtok31wSharedMemory"};
int mainCRTStartup(void){
  int i;
  for(i=0;i<9;i++){HANDLE h=OpenFileMappingW(0x0004,FALSE,fm[i]);w("FM  ");ww(fm[i]);w(h?" -> OK\r\n":" -> NULL\r\n");if(h)CloseHandle(h);}
  for(i=0;i<7;i++){HANDLE h=OpenMutexW(0x00100000,FALSE,mt[i]);w("MTX ");ww(mt[i]);w(h?" -> OK\r\n":" -> NULL\r\n");if(h)CloseHandle(h);}
  return 0;
}
