#include <windows.h>
#include <stddef.h>

int memcmp(const void *a, const void *b, size_t n)
{
    const BYTE *pa = (const BYTE *)a;
    const BYTE *pb = (const BYTE *)b;
    while (n--) {
        if (*pa != *pb)
            return (int)*pa - (int)*pb;
        pa++;
        pb++;
    }
    return 0;
}

void *memcpy(void *dst, const void *src, size_t n)
{
    BYTE *d = (BYTE *)dst;
    const BYTE *s = (const BYTE *)src;
    while (n--)
        *d++ = *s++;
    return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
    BYTE *d = (BYTE *)dst;
    const BYTE *s = (const BYTE *)src;
    if (d == s || !n)
        return dst;
    if (d < s) {
        while (n--)
            *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--)
            *--d = *--s;
    }
    return dst;
}

void *memset(void *dst, int c, size_t n)
{
    BYTE *p = (BYTE *)dst;
    while (n--)
        *p++ = (BYTE)c;
    return dst;
}
