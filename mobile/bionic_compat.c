// libultra's bcopy/bzero (PR/os.h), which bionic does not export
#include <stddef.h>

extern void *memmove(void *dst, const void *src, size_t n);
extern void *memset(void *dst, int c, size_t n);

void bcopy(const void *src, void *dst, size_t n)
{
    memmove(dst, src, n);
}

void bzero(void *dst, size_t n)
{
    memset(dst, 0, n);
}
