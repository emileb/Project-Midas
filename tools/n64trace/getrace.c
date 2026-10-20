/*
 * getrace - run GoldenEye (US) headless in mupen64plus with its debugger and
 * log what the N64 does during an attract demo, for comparison with the port's
 * GE_DEMODIAG / GE_RNGTRACE output. See README.md.
 *
 * usage: getrace [options] baserom.u.z64
 *   --demo N          attract demo to play, 0-13 (ramrom_table order)   [0]
 *   --stop T          stop after demo tick T                            [60]
 *   --trace A-B       log the caller of every RNG draw in ticks A..B
 *   --break ADDR,...  at each hit during the trace ticks, log a0-a3/ra/sp
 *                     and the three floats behind any pointer argument
 *   --stack ADDR      at the first traced RNG draw whose caller is in
 *                     [ADDR, ADDR+0x4000), list code addresses on the stack
 *   --peek ADDR,...   at each seed check in the trace ticks, print the 32-bit
 *                     words at these KSEG0 addresses (game globals)
 *   --jals N          at each --break hit, list the jal targets in the N
 *                     instructions after ra (finds the address of a function
 *                     the caller calls next, e.g. one with no address in the
 *                     decomp); reads only ra's own page, which is resident
 *   --core/--rsp/--data PATH   override the libraries built by build.sh
 *
 * Output (stdout), one event per line:
 *   n64: demo tick T draws=D seed=S   at each seed check (same numbering as
 *                                     the port's "ge: demo tick" lines)
 *   n64: rng tick T from RA           a traced RNG draw
 *   n64: break PC tick T ...          a --break hit
 *
 * Never read TLB-mapped memory with the debugger: GoldenEye demand-pages its
 * 0x7F code, and a read of a page that is not resident raises a TLB refill
 * exception in the game. Everything here reads KSEG0 RAM only.
 */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define M64P_CORE_PROTOTYPES 1
#include "m64p_types.h"
#include "m64p_common.h"
#include "m64p_frontend.h"
#include "m64p_config.h"
#include "m64p_debugger.h"

/* GoldenEye US addresses (decomp: src/random.s, src/game/ramromreplay.c) */
#define A_RANDOM_GET_NEXT 0x7000A450u
#define A_RANDOM_SEED     0x80024460u
#define A_REPLAY_HANDLER  0x7F0C0080u /* ramrom_replay_handler: per-block seed check */
#define A_SELECT_DEMO     0x7F0C0970u /* select_ramrom_to_play */
#define N_DEMOS 14
#define MAX_BREAKS 16

#ifdef __APPLE__
#define SO ".dylib"
#else
#define SO ".so"
#endif

static ptr_CoreDoCommand pCoreDoCommand;
static ptr_DebugSetRunState pDebugSetRunState;
static ptr_DebugStep pDebugStep;
static ptr_DebugMemRead32 pDebugMemRead32;
static ptr_DebugMemRead64 pDebugMemRead64;
static ptr_DebugMemWrite64 pDebugMemWrite64;
static ptr_DebugGetCPUDataPtr pDebugGetCPUDataPtr;
static ptr_DebugBreakpointCommand pDebugBreakpointCommand;

static int s_demo = 0, s_stop = 60, s_traceFrom = -1, s_traceTo = -1;
static uint32_t s_breaks[MAX_BREAKS];
static int s_nbreaks;
static uint32_t s_stackLo;
static int s_stackDone;
static int s_jals;
static uint32_t s_peeks[MAX_BREAKS];
static int s_npeeks;

static int s_active, s_checks, s_draws, s_stopping, s_vi;

static int isRam(uint32_t a)
{
    return a >= 0x80000000u && a < 0x80800000u && (a & 3) == 0;
}

static float ramFloat(uint32_t a)
{
    union { uint32_t u; float f; } v;
    v.u = pDebugMemRead32(a);
    return v.f;
}

static long long *cpuRegs(void)
{
    return (long long *) pDebugGetCPUDataPtr(M64P_CPU_REG_REG);
}

/* the tick the current draws belong to: they are counted up to the next seed check */
static int curTick(void)
{
    return s_checks + 1;
}

static int tracing(void)
{
    return s_active && curTick() >= s_traceFrom && curTick() <= s_traceTo;
}

static uint64_t n64RandMix(uint64_t seed)
{
    uint64_t t = ((seed << 63) >> 31) | ((seed << 31) >> 32);
    t ^= (seed << 44) >> 32;
    return t ^ ((t >> 20) & 0xfff);
}

static void addBreak(uint32_t addr)
{
    m64p_breakpoint b;
    memset(&b, 0, sizeof(b));
    b.address = addr;
    b.endaddr = addr;
    b.flags = M64P_BKP_FLAG_ENABLED | M64P_BKP_FLAG_EXEC;
    if (pDebugBreakpointCommand(M64P_BKP_CMD_ADD_STRUCT, 0, &b) < 0) {
        fprintf(stderr, "getrace: breakpoint at %08x failed\n", addr);
    }
}

static void dbgInit(void)
{
    int i, off = 0;
    addBreak(A_RANDOM_GET_NEXT);
    addBreak(A_REPLAY_HANDLER);
    addBreak(A_SELECT_DEMO);
    for (i = 0; i < s_nbreaks; i++) {
        addBreak(s_breaks[i]);
    }
    pCoreDoCommand(M64CMD_CORE_STATE_SET, M64CORE_SPEED_LIMITER, &off);
}

static void onRandom(void)
{
    long long *r = cpuRegs();
    uint32_t ra = (uint32_t) r[31];
    uint32_t sp = (uint32_t) r[29], x;

    if (!s_active) {
        return;
    }
    s_draws++;
    if (!tracing()) {
        return;
    }
    printf("n64: rng tick %d from %08x\n", curTick(), ra);
    if (s_stackLo && !s_stackDone && ra >= s_stackLo && ra < s_stackLo + 0x4000 && isRam(sp)) {
        s_stackDone = 1;
        for (x = sp; x < sp + 0x400 && isRam(x); x += 4) {
            uint32_t w = pDebugMemRead32(x);
            if ((w >= 0x7F000000u && w < 0x7F100000u) || (w >= 0x70000000u && w < 0x70020000u)) {
                printf("n64: stack %08x: %08x\n", x, w);
            }
        }
    }
}

static void onBreak(uint32_t pc)
{
    long long *r = cpuRegs();
    int i;

    if (!tracing()) {
        return;
    }
    printf("n64: break %08x tick %d a0=%08x a1=%08x a2=%08x a3=%08x ra=%08x sp=%08x\n", pc, curTick(),
           (uint32_t) r[4], (uint32_t) r[5], (uint32_t) r[6], (uint32_t) r[7], (uint32_t) r[31], (uint32_t) r[29]);
    for (i = 4; i <= 7; i++) {
        uint32_t a = (uint32_t) r[i];
        if (isRam(a) && isRam(a + 8)) {
            printf("n64:   a%d -> (%g %g %g)\n", i - 4, ramFloat(a), ramFloat(a + 4), ramFloat(a + 8));
        }
    }
    if (s_jals > 0) {
        /* the caller is executing, so ra's 4KB page is mapped; stay inside it */
        uint32_t ra = (uint32_t) r[31], x;
        for (x = ra; x < ra + 4u * (uint32_t) s_jals && (x & ~0xfffu) == (ra & ~0xfffu); x += 4) {
            uint32_t op = pDebugMemRead32(x);
            if ((op >> 26) == 3) { /* jal */
                printf("n64:   %08x: jal %08x\n", x, (x & 0xf0000000u) | ((op & 0x03ffffffu) << 2));
            }
        }
    }
}

static void dbgUpdate(unsigned int pc)
{
    int i;

    switch (pc) {
    case A_SELECT_DEMO: {
        /* make the next draw pick the wanted demo; it restores the recorded seed */
        uint64_t s = 1;
        if (s_active) {
            /* attract mode is choosing the next demo: ours ended. On the N64 a
             * demo ends at the end of the recording or at its first seed
             * mismatch (ramromFadeToTitle), unlike the port's tolerant mode */
            printf("n64: demo ended after tick %d (end of recording, or the N64's own seed check failed)\n", s_checks);
            if (!s_stopping) {
                s_stopping = 1;
                pCoreDoCommand(M64CMD_STOP, 0, NULL);
            }
            break;
        }
        while ((uint32_t) n64RandMix(s) % N_DEMOS != (uint32_t) s_demo) {
            s++;
        }
        pDebugMemWrite64(A_RANDOM_SEED, s);
        s_active = 1;
        s_checks = 0;
        s_draws = 0;
        printf("n64: demo %d selected\n", s_demo);
        break;
    }
    case A_REPLAY_HANDLER:
        if (s_active) {
            s_checks++;
            printf("n64: demo tick %d draws=%d seed=%016llx\n", s_checks, s_draws,
                   (unsigned long long) pDebugMemRead64(A_RANDOM_SEED));
            s_draws = 0;
            if (s_npeeks > 0 && s_checks >= s_traceFrom && s_checks <= s_traceTo) {
                int k;
                printf("n64: peek tick %d", s_checks);
                for (k = 0; k < s_npeeks; k++) {
                    printf(" %08x=%08x", s_peeks[k], pDebugMemRead32(s_peeks[k]));
                }
                printf("\n");
            }
            if (s_checks >= s_stop && !s_stopping) {
                s_stopping = 1;
                pCoreDoCommand(M64CMD_STOP, 0, NULL);
            }
        }
        break;
    case A_RANDOM_GET_NEXT:
        onRandom();
        break;
    default:
        for (i = 0; i < s_nbreaks; i++) {
            if (pc == s_breaks[i]) {
                onBreak(pc);
            }
        }
        break;
    }
    fflush(stdout);
    pDebugSetRunState(M64P_DBG_RUNSTATE_RUNNING);
    pDebugStep();
}

static void dbgVi(void)
{
    if (++s_vi % 3600 == 0) {
        fprintf(stderr, "getrace: vi %d, demo %s, tick %d\n", s_vi, s_active ? "running" : "not started", s_checks);
    }
}

static void debugCb(void *ctx, int level, const char *msg)
{
    (void) ctx;
    if (level <= M64MSG_WARNING) {
        fprintf(stderr, "m64p[%d]: %s\n", level, msg);
    }
}

#define SYM(h, name) ((ptr_##name) dlsym(h, #name))

static void *loadPlugin(void *core, const char *path, m64p_plugin_type type)
{
    void *h = dlopen(path, RTLD_NOW);
    if (!h) {
        fprintf(stderr, "getrace: dlopen %s: %s\n", path, dlerror());
        exit(1);
    }
    ((ptr_PluginStartup) dlsym(h, "PluginStartup"))(core, NULL, debugCb);
    if (SYM(core, CoreAttachPlugin)(type, h) != M64ERR_SUCCESS) {
        fprintf(stderr, "getrace: attaching %s failed\n", path);
        exit(1);
    }
    return h;
}

int main(int argc, char **argv)
{
    char dir[1024], core_path[1100], rsp_path[1100], data_path[1100], gfx_path[1100], in_path[1100];
    const char *rom_path = NULL;
    void *core;
    FILE *f;
    long size;
    unsigned char *rom;
    m64p_handle sec;
    int i, one = 1, zero = 0;
    char *slash;

    /* defaults: the libraries build.sh puts beside this executable */
    snprintf(dir, sizeof(dir), "%s", argv[0]);
    slash = strrchr(dir, '/');
    if (slash) *slash = '\0'; else strcpy(dir, ".");
    snprintf(core_path, sizeof(core_path), "%s/libmupen64plus" SO, dir);
    snprintf(rsp_path, sizeof(rsp_path), "%s/mupen64plus-rsp-hle" SO, dir);
    snprintf(data_path, sizeof(data_path), "%s/data", dir);
    snprintf(gfx_path, sizeof(gfx_path), "%s/nullgfx" SO, dir);
    snprintf(in_path, sizeof(in_path), "%s/nullinput" SO, dir);

    for (i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--demo") && v) { s_demo = atoi(v); i++; }
        else if (!strcmp(a, "--stop") && v) { s_stop = atoi(v); i++; }
        else if (!strcmp(a, "--trace") && v) {
            s_traceFrom = atoi(v);
            s_traceTo = strchr(v, '-') ? atoi(strchr(v, '-') + 1) : s_traceFrom;
            i++;
        }
        else if (!strcmp(a, "--break") && v) {
            const char *p = v;
            while (*p && s_nbreaks < MAX_BREAKS) {
                s_breaks[s_nbreaks++] = (uint32_t) strtoul(p, (char **) &p, 16);
                if (*p == ',') p++;
            }
            i++;
        }
        else if (!strcmp(a, "--stack") && v) { s_stackLo = (uint32_t) strtoul(v, NULL, 16); i++; }
        else if (!strcmp(a, "--jals") && v) { s_jals = atoi(v); i++; }
        else if (!strcmp(a, "--peek") && v) {
            char *e = (char *) v;
            while (*e && s_npeeks < MAX_BREAKS) {
                uint32_t x = (uint32_t) strtoul(e, &e, 16);
                if (isRam(x)) {
                    s_peeks[s_npeeks++] = x;
                } else {
                    fprintf(stderr, "getrace: --peek %08x is not KSEG0 RAM, skipped\n", x);
                }
                if (*e == ',') e++; else break;
            }
            i++;
        }
        else if (!strcmp(a, "--core") && v) { snprintf(core_path, sizeof(core_path), "%s", v); i++; }
        else if (!strcmp(a, "--rsp") && v) { snprintf(rsp_path, sizeof(rsp_path), "%s", v); i++; }
        else if (!strcmp(a, "--data") && v) { snprintf(data_path, sizeof(data_path), "%s", v); i++; }
        else if (a[0] != '-') { rom_path = a; }
        else { fprintf(stderr, "getrace: unknown option %s (see the header of getrace.c)\n", a); return 2; }
    }
    if (!rom_path || s_demo < 0 || s_demo >= N_DEMOS) {
        fprintf(stderr, "usage: %s [--demo N] [--stop T] [--trace A-B] [--break ADDR,...] [--stack ADDR] [--jals N] [--peek ADDR,...] baserom.u.z64\n", argv[0]);
        return 2;
    }

    core = dlopen(core_path, RTLD_NOW | RTLD_GLOBAL);
    if (!core) {
        fprintf(stderr, "getrace: dlopen %s: %s\n", core_path, dlerror());
        return 1;
    }
    pCoreDoCommand = SYM(core, CoreDoCommand);
    pDebugSetRunState = SYM(core, DebugSetRunState);
    pDebugStep = SYM(core, DebugStep);
    pDebugMemRead32 = SYM(core, DebugMemRead32);
    pDebugMemRead64 = SYM(core, DebugMemRead64);
    pDebugMemWrite64 = SYM(core, DebugMemWrite64);
    pDebugGetCPUDataPtr = SYM(core, DebugGetCPUDataPtr);
    pDebugBreakpointCommand = SYM(core, DebugBreakpointCommand);

    if (SYM(core, CoreStartup)(0x020001, dir, data_path, NULL, debugCb, NULL, NULL) != M64ERR_SUCCESS) {
        fprintf(stderr, "getrace: CoreStartup failed\n");
        return 1;
    }
    SYM(core, ConfigOpenSection)("Core", &sec);
    SYM(core, ConfigSetParameter)(sec, "R4300Emulator", M64TYPE_INT, &one); /* cached interpreter */
    SYM(core, ConfigSetParameter)(sec, "EnableDebugger", M64TYPE_BOOL, &one);
    SYM(core, ConfigSetParameter)(sec, "OnScreenDisplay", M64TYPE_BOOL, &zero);

    f = fopen(rom_path, "rb");
    if (!f) {
        perror(rom_path);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    rom = malloc((size_t) size);
    if (!rom || fread(rom, 1, (size_t) size, f) != (size_t) size) {
        fprintf(stderr, "getrace: reading %s failed\n", rom_path);
        return 1;
    }
    fclose(f);
    if (pCoreDoCommand(M64CMD_ROM_OPEN, (int) size, rom) != M64ERR_SUCCESS) {
        fprintf(stderr, "getrace: ROM_OPEN failed\n");
        return 1;
    }

    /* video, input, then RSP (the core expects this order) */
    loadPlugin(core, gfx_path, M64PLUGIN_GFX);
    loadPlugin(core, in_path, M64PLUGIN_INPUT);
    loadPlugin(core, rsp_path, M64PLUGIN_RSP);
    SYM(core, DebugSetCallbacks)(dbgInit, dbgUpdate, dbgVi);

    pCoreDoCommand(M64CMD_EXECUTE, 0, NULL);
    pCoreDoCommand(M64CMD_ROM_CLOSE, 0, NULL);
    return 0;
}
