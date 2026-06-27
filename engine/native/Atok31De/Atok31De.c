/*
 * ATOK31TIP.DLL imports ordinals 5-9 from Atok31De.dll.
 * This stub is only meant to satisfy the loader so we can reach
 * ATOK31TIP.DLL's registration path and inspect the TSF/COM keys.
 */

typedef void *HINSTANCE;
typedef void *LPVOID;
typedef unsigned long DWORD;
typedef int BOOL;

#ifndef WINAPI
#define WINAPI __attribute__((stdcall))
#endif

static int WINAPI stub(void)
{
    return 0;
}

int WINAPI ord5(void) { return stub(); }
int WINAPI ord6(void) { return stub(); }
int WINAPI ord7(void) { return stub(); }
int WINAPI ord8(void) { return stub(); }
int WINAPI ord9(void) { return stub(); }

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)instance;
    (void)reason;
    (void)reserved;
    return 1;
}
