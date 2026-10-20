// Touch controls and the launcher's gamepad mapping, as player 1 (OpenTouch Portable* API)
//
// The Portable* calls come from the touch thread and only set the state below; the game
// thread reads it once per controller poll through IN_ANDROID (port/src/input_pc.c).

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

#include <SDL.h>
#include <android/log.h>

#include "game_interface.h"
#include "port_act_defs.h"

#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "GOLDENEYE", __VA_ARGS__))

// N64 pad bits (include/PR/os.h); that header can't be mixed with the bionic ones here
#define N64_A       0x8000
#define N64_B       0x4000
#define N64_Z       0x2000
#define N64_START   0x1000
#define N64_DUP     0x0800
#define N64_DDOWN   0x0400
#define N64_DLEFT   0x0200
#define N64_DRIGHT  0x0100
#define N64_L       0x0020
#define N64_R       0x0010

// Port actions (joy_port.c), looked up by their midas.ini names
enum
{
    ACT_USE,
    ACT_RELOAD,
    ACT_NEXT_WEAPON,
    ACT_PREV_WEAPON,
    ACT_NEXT_INV,
    ACT_CROUCH_HOLD,
    ACT_CROUCH_TOGGLE,
    ACT_WATCH_OBJECTIVES,
    ACT_COUNT
};

static const char *const actNames[ACT_COUNT] = {
    "use", "reload", "next_weapon", "prev_weapon", "next_inventory", "crouch_hold", "crouch_toggle", "watch_objectives"
};

extern int32_t portJoyPortBindCount(void);
extern const char *portJoyPortBindName(int32_t i);
extern void portJoyPortPadDown(int32_t pad, int32_t act);

static volatile uint32_t padButtons;   // N64 bits held
static volatile uint32_t menuButtons;  // N64 bits held, menu arrows only
static volatile uint32_t actsHeld;     // 1 << ACT_*
static volatile int backPolls;         // polls left of a back button press

// Pressed since the last poll, so a tap shorter than a poll still counts
static uint32_t padPressed, menuPressed, actsPressed;

static volatile float forwardMove, sideMove;
static volatile float lookYawJoy, lookPitchJoy;
static volatile float lookYawMouse, lookPitchMouse;

static volatile int inGame;   // set on the game thread, read by the touch thread

static void setBit(volatile uint32_t *mask, uint32_t *pressed, int state, uint32_t bit)
{
    if (state)
    {
        *mask |= bit;
        __atomic_or_fetch(pressed, bit, __ATOMIC_RELEASE);
    }
    else
        *mask &= ~bit;
}

int PortableKeyEvent(int state, int code, int unicode)
{
    extern int SDL_SendKeyboardKey(Uint8 state, SDL_Scancode scancode);

    SDL_SendKeyboardKey(state ? SDL_PRESSED : SDL_RELEASED, (SDL_Scancode)code);
    return 0;
}

void PortableAction(int state, int action)
{
    if (action >= PORT_ACT_MENU_UP && action <= PORT_ACT_MENU_ABORT)
    {
        switch (action)
        {
            case PORT_ACT_MENU_UP:      setBit(&menuButtons, &menuPressed, state, N64_DUP); break;
            case PORT_ACT_MENU_DOWN:    setBit(&menuButtons, &menuPressed, state, N64_DDOWN); break;
            case PORT_ACT_MENU_LEFT:    setBit(&menuButtons, &menuPressed, state, N64_DLEFT); break;
            case PORT_ACT_MENU_RIGHT:   setBit(&menuButtons, &menuPressed, state, N64_DRIGHT); break;
            case PORT_ACT_MENU_SELECT:
            case PORT_ACT_MENU_CONFIRM: setBit(&menuButtons, &menuPressed, state, N64_A); break;
            case PORT_ACT_MENU_BACK:
            case PORT_ACT_MENU_ABORT:   setBit(&menuButtons, &menuPressed, state, N64_B); break;
        }
        return;
    }

    switch (action)
    {
        case PORT_ACT_ATTACK:        setBit(&padButtons, &padPressed, state, N64_Z); break;
        case PORT_ACT_ZOOM_IN:       setBit(&padButtons, &padPressed, state, N64_R); break; // aim mode
        case PORT_ACT_ALT_ATTACK:    setBit(&padButtons, &padPressed, state, N64_L); break;
        case PORT_ACT_N64_BUTTON_A:  setBit(&padButtons, &padPressed, state, N64_A); break;
        case PORT_ACT_N64_BUTTON_B:  setBit(&padButtons, &padPressed, state, N64_B); break;
        case PORT_ACT_USE:           setBit(&actsHeld, &actsPressed, state, 1 << ACT_USE); break;
        case PORT_ACT_RELOAD:        setBit(&actsHeld, &actsPressed, state, 1 << ACT_RELOAD); break;
        case PORT_ACT_NEXT_WEP:      setBit(&actsHeld, &actsPressed, state, 1 << ACT_NEXT_WEAPON); break;
        case PORT_ACT_PREV_WEP:      setBit(&actsHeld, &actsPressed, state, 1 << ACT_PREV_WEAPON); break;
        case PORT_ACT_INVEN:         setBit(&actsHeld, &actsPressed, state, 1 << ACT_NEXT_INV); break;
        case PORT_ACT_DOWN:          setBit(&actsHeld, &actsPressed, state, 1 << ACT_CROUCH_HOLD); break;
        case PORT_ACT_TOGGLE_CROUCH: setBit(&actsHeld, &actsPressed, state, 1 << ACT_CROUCH_TOGGLE); break;
        case PORT_ACT_MAP:           setBit(&actsHeld, &actsPressed, state, 1 << ACT_WATCH_OBJECTIVES); break;
    }
}

void PortableBackButton(void)
{
    backPolls = 4;
}

void PortableMouseButton(int state, int button, float dx, float dy)
{
}

void PortableMouse(float dx, float dy)
{
}

void PortableMouseAbs(float x, float y)
{
}

void PortableCommand(const char *cmd)
{
}

void PortableAutomapControl(float zoom, float x, float y)
{
}

int PortableShowKeyboard(void)
{
    return 0;
}

bool PortableSetAlwaysRun(bool run)
{
    return false;
}

static float clampUnit(float v)
{
    return v > 1.0f ? 1.0f : (v < -1.0f ? -1.0f : v);
}

void PortableMoveFwd(float fwd)
{
    forwardMove = clampUnit(fwd);
}

void PortableMoveSide(float strafe)
{
    sideMove = clampUnit(strafe);
}

void PortableMove(float fwd, float strafe)
{
    PortableMoveFwd(fwd);
    PortableMoveSide(strafe);
}

void PortableLookPitch(int mode, float pitch)
{
    switch (mode)
    {
        case LOOK_MODE_MOUSE:    lookPitchMouse += pitch; break;
        case LOOK_MODE_JOYSTICK: lookPitchJoy = pitch; break;
    }
}

void PortableLookYaw(int mode, float yaw)
{
    switch (mode)
    {
        case LOOK_MODE_MOUSE:    lookYawMouse += yaw; break;
        case LOOK_MODE_JOYSTICK: lookYawJoy = yaw; break;
    }
}

// The port prints much of its diagnostics (demo exits, shader lists...) with printf/fprintf(stderr)
static void *stdioToLogcat(void *arg)
{
    FILE *in = fdopen((int)(intptr_t)arg, "r");
    char line[1024];

    while (in != NULL && fgets(line, sizeof(line), in) != NULL)
    {
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n')
            line[len - 1] = '\0';
        __android_log_write(ANDROID_LOG_INFO, "GOLDENEYE", line);
    }
    return NULL;
}

static void redirectStdio(void)
{
    int fds[2];
    pthread_t thread;

    if (pipe(fds) != 0)
        return;

    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    dup2(fds[1], STDOUT_FILENO);
    dup2(fds[1], STDERR_FILENO);
    close(fds[1]);

    if (pthread_create(&thread, NULL, stdioToLogcat, (void *)(intptr_t)fds[0]) == 0)
        pthread_detach(thread);
}

void PortableInit(int argc, const char **argv)
{
    extern int main_mobile(int argc, char **argv);

    redirectStdio();
    main_mobile(argc, (char **)argv);
}

touchscreemode_t PortableGetScreenMode()
{
    return inGame ? TS_GAME : TS_MENU;
}

static int32_t actIndex[ACT_COUNT];

static void findActs(void)
{
    static int done;
    int32_t n = portJoyPortBindCount();

    if (done)
        return;
    done = 1;

    for (int a = 0; a < ACT_COUNT; a++)
    {
        actIndex[a] = -1;
        for (int32_t i = 0; i < n; i++)
        {
            const char *name = portJoyPortBindName(i);
            if (name != NULL && strcmp(name, actNames[a]) == 0)
                actIndex[a] = i;
        }
        if (actIndex[a] < 0)
            LOGI("No port action '%s'", actNames[a]);
    }
}

static int32_t toN64(float v)
{
    return (int32_t)(clampUnit(v) * 80.0f);
}

// Game thread, once per controller poll. Sticks are N64 units (+/-80, up and right positive).
void IN_ANDROID(int liveGame, uint16_t *buttons, int32_t *moveX, int32_t *moveY, int32_t *lookX, int32_t *lookY)
{
    uint32_t b = padButtons | __atomic_exchange_n(&padPressed, 0, __ATOMIC_ACQUIRE);
    uint32_t acts = actsHeld | __atomic_exchange_n(&actsPressed, 0, __ATOMIC_ACQUIRE);
    uint32_t menuTaps = __atomic_exchange_n(&menuPressed, 0, __ATOMIC_ACQUIRE);

    inGame = liveGame;
    findActs();

    if (backPolls > 0)
    {
        backPolls--;
        b |= liveGame ? N64_START : N64_B;
    }

    if (!liveGame)
    {
        uint32_t m = menuButtons | menuTaps;

        b |= m;
        // Menus that move a pointer take the stick, not the D-pad
        if (m & N64_DUP)    *moveY = 64;
        if (m & N64_DDOWN)  *moveY = -64;
        if (m & N64_DLEFT)  *moveX = -64;
        if (m & N64_DRIGHT) *moveX = 64;
    }

    *buttons |= (uint16_t)b;

    if (forwardMove != 0.0f || sideMove != 0.0f)
    {
        *moveX = toN64(sideMove);
        *moveY = toN64(forwardMove);
    }

    *lookX = toN64(-lookYawJoy);
    *lookY = toN64(-lookPitchJoy);

    for (int a = 0; a < ACT_COUNT; a++)
    {
        if ((acts & (1u << a)) && actIndex[a] >= 0)
            portJoyPortPadDown(0, actIndex[a]);
    }
}

// Game thread, once per sim tick: the touch look drag in mouse counts since the last tick
void IN_ANDROID_MouseLook(float *dx, float *dy)
{
    *dx = -lookYawMouse * 3000.0f; // the touch layer turns left for positive yaw, as Quake does
    *dy = lookPitchMouse * 1500.0f;
    lookYawMouse = 0.0f;
    lookPitchMouse = 0.0f;
}
