#if !defined(TARGET_N64) && defined(__cplusplus)
/* PC port: this header shadows the toolchain's <string.h>; C++ TUs (fast3d)
 * defer to the real one, which libstdc++ needs. C TUs keep the minimal stub,
 * because the hosted header's bcopy/bzero/bcmp conflict with PR/os.h. */
#include_next <string.h>
#else

#ifndef _STRING_H_
#define _STRING_H_
#include <PR/ultratypes.h>

extern void *memcpy(void *, const void *, size_t);
extern unsigned char *strchr(const unsigned char *, int);
extern size_t strlen(const unsigned char *);

#ifndef TARGET_N64
/* PC port: the rest of what C TUs call; the str* ones match src/str.h
 * (GE's own implementations), the mem* ones come from libc. A missing
 * prototype makes a pointer-returning call truncate on 64-bit. */
extern char *strcpy(char *dst, const char *src);
extern char *strncpy(char *dst, const char *src, size_t n);
extern char *strcat(char *dst, const char *src);
extern int strcmp(const char *str1, const char *str2);
extern int strncmp(const char *str1, const char *str2, size_t n);
extern char *strstr(const char *haystack, const char *needle);
extern char *strrchr(const char *s, int c);
extern size_t strcspn(const char *s, const char *reject);
extern char *strerror(int errnum);
extern void *memset(void *s, int c, size_t n);
extern int memcmp(const void *s1, const void *s2, size_t n);
extern void *memmove(void *dst, const void *src, size_t n);
extern void *memchr(const void *s, int c, size_t n);
#endif

#endif
#endif /* TARGET_N64 */
