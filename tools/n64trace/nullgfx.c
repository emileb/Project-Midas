/*
 * nullgfx - a mupen64plus video plugin that draws nothing but completes each
 * display list the way a real plugin does at its final full-sync: by flagging
 * the RDP ("DP") interrupt. Without it the core never raises DP_INT after a
 * gfx task and games that wait for RDP completion (GoldenEye) hang.
 */
#include <string.h>

#define M64P_PLUGIN_PROTOTYPES 1
#include "m64p_types.h"
#include "m64p_common.h"
#include "m64p_plugin.h"

#define MI_INTR_DP 0x20

static GFX_INFO s_info;

EXPORT m64p_error CALL PluginStartup(m64p_dynlib_handle core, void *ctx, void (*debug)(void *, int, const char *))
{
    (void) core; (void) ctx; (void) debug;
    return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL PluginShutdown(void)
{
    return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL PluginGetVersion(m64p_plugin_type *type, int *version, int *api, const char **name, int *caps)
{
    if (type) *type = M64PLUGIN_GFX;
    if (version) *version = 0x010000;
    if (api) *api = 0x020200;
    if (name) *name = "nullgfx";
    if (caps) *caps = 0;
    return M64ERR_SUCCESS;
}

EXPORT int CALL InitiateGFX(GFX_INFO info)
{
    s_info = info;
    return 1;
}

EXPORT void CALL ProcessDList(void)
{
    *s_info.MI_INTR_REG |= MI_INTR_DP;
}

EXPORT void CALL ProcessRDPList(void)
{
    *s_info.MI_INTR_REG |= MI_INTR_DP;
    s_info.CheckInterrupts();
}

EXPORT void CALL ChangeWindow(void) {}
EXPORT void CALL MoveScreen(int x, int y) { (void) x; (void) y; }
EXPORT void CALL RomClosed(void) {}
EXPORT int CALL RomOpen(void) { return 1; }
EXPORT void CALL ShowCFB(void) {}
EXPORT void CALL UpdateScreen(void) {}
EXPORT void CALL ViStatusChanged(void) {}
EXPORT void CALL ViWidthChanged(void) {}
EXPORT void CALL ReadScreen2(void *dest, int *w, int *h, int front) { (void) dest; (void) front; if (w) *w = 0; if (h) *h = 0; }
EXPORT void CALL SetRenderingCallback(void (*cb)(int)) { (void) cb; }
EXPORT void CALL ResizeVideoOutput(int w, int h) { (void) w; (void) h; }
EXPORT void CALL FBRead(unsigned int addr) { (void) addr; }
EXPORT void CALL FBWrite(unsigned int addr, unsigned int size) { (void) addr; (void) size; }
EXPORT void CALL FBGetFrameBufferInfo(void *p) { if (p) memset(p, 0, sizeof(FrameBufferInfo) * 6); }
