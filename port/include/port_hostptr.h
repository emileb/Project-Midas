/**
 * PORT_IS_HOST_ADDR(v): is this 32-bit word a real host pointer, as opposed to
 * an N64 segment word (0x0Sxxxxxx) or a file offset?
 *
 * On PC, host pointers always live at or above 0x10000000: the Windows image is
 * rebased there, the Linux image sits at 0x08048000 (segments 8/9 are never
 * installed), and both arenas are pinned at 0x20000000+.
 *
 * The original Xbox can't follow that rule: the XBE base is forced to
 * 0x00010000, so the whole image (assets and static buffers) overlaps the N64
 * segment 0/1 word range. There the test is "above 0x10000000 OR inside the
 * image" (xbox_sys.c reads the image size from the XBE header at boot). Heap
 * allocations never hold display-list data, and every other display-list
 * allocation goes through the arenas. See docs/port/xbox-port.md.
 *
 * 64-bit builds: every host pointer is above 4 GB (macOS arm64 reserves the low
 * 4 GB as __PAGEZERO; Android arm64 maps code, heap and mmap far higher), so a
 * word that fits in 32 bits is a segment word or an offset.
 */
#ifndef PORT_HOSTPTR_H
#define PORT_HOSTPTR_H

#include <stdint.h>

#ifdef GE_XBOX
#ifdef __cplusplus
extern "C" {
#endif
int portXboxIsHostAddr(unsigned int v);
#ifdef __cplusplus
}
#endif
#define PORT_IS_HOST_ADDR(v) (portXboxIsHostAddr((unsigned int) (v)))
#elif UINTPTR_MAX > 0xFFFFFFFFu
#define PORT_IS_HOST_ADDR(v) ((uintptr_t) (v) > 0xFFFFFFFFu)
#else
#define PORT_IS_HOST_ADDR(v) ((unsigned int) (v) >= 0x10000000u)
#endif

#endif
