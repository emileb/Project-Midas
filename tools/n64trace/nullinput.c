/*
 * nullinput - a mupen64plus input plugin with controller 1 plugged in and
 * nothing pressed, so a game that requires a controller (GoldenEye's legal
 * screen) runs its attract sequence headless.
 */
#include <stdint.h>
#include <string.h>

#define M64P_PLUGIN_PROTOTYPES 1
#include "m64p_types.h"
#include "m64p_common.h"
#include "m64p_plugin.h"

EXPORT m64p_error CALL PluginStartup(m64p_dynlib_handle core, void *ctx, void (*debug)(void *, int, const char *))
{
    (void) core; (void) ctx; (void) debug;
    return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL PluginShutdown(void) { return M64ERR_SUCCESS; }

EXPORT m64p_error CALL PluginGetVersion(m64p_plugin_type *type, int *version, int *api, const char **name, int *caps)
{
    if (type) *type = M64PLUGIN_INPUT;
    if (version) *version = 0x010000;
    if (api) *api = 0x020100;
    if (name) *name = "nullinput";
    if (caps) *caps = 0;
    return M64ERR_SUCCESS;
}

EXPORT void CALL InitiateControllers(CONTROL_INFO info)
{
    int i;
    for (i = 0; i < 4; i++) {
        info.Controls[i].Present = (i == 0);
        info.Controls[i].RawData = 0;
        info.Controls[i].Plugin = PLUGIN_NONE;
        info.Controls[i].Type = CONT_TYPE_STANDARD;
    }
}

EXPORT void CALL GetKeys(int control, BUTTONS *keys) { (void) control; keys->Value = 0; }
EXPORT void CALL ControllerCommand(int control, unsigned char *cmd) { (void) control; (void) cmd; }
EXPORT void CALL ReadController(int control, unsigned char *cmd) { (void) control; (void) cmd; }
EXPORT int CALL RomOpen(void) { return 1; }
EXPORT void CALL RomClosed(void) {}
EXPORT void CALL SDL_KeyDown(int mod, int sym) { (void) mod; (void) sym; }
EXPORT void CALL SDL_KeyUp(int mod, int sym) { (void) mod; (void) sym; }
EXPORT void CALL SendVRUWord(uint16_t length, uint16_t *word, uint8_t lang) { (void) length; (void) word; (void) lang; }
EXPORT void CALL SetMicState(int state) { (void) state; }
EXPORT void CALL ReadVRUResults(uint16_t *e, uint16_t *n, uint16_t *m, uint16_t *v, uint16_t *l, uint16_t *w) { *e = *n = *m = *v = *l = 0; (void) w; }
EXPORT void CALL ClearVRUWords(uint8_t length) { (void) length; }
EXPORT void CALL SetVRUWordMask(uint8_t length, uint8_t *mask) { (void) length; (void) mask; }
