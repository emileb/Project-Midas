/**
 * PC-port audio stubs — Phase-4 deferral of the AL synthesizer.
 *
 * The real libultra audio library (src/libultra/audio + src/libultrare) is
 * scheduled for Phase 4. Until then every al* symbol the kept game code
 * references is satisfied here so the game runs SILENT without crashing.
 *
 * Design rules used below (each stub carries its own rationale):
 *  - Functions whose results feed loops/derefs get REAL minimal
 *    implementations (alLink/alUnlink, alHeapInit/alHeapDBAlloc).
 *  - Functions that only drive the (never-running) synthesizer are no-ops.
 *  - Query functions return the value that makes every audited caller take
 *    its benign exit path (alCSPGetState -> AL_STOPPED, alEvtqNextEvent ->
 *    positive delta, alCents2Ratio -> 1.0).
 *  - The SFX sound-bank consumed by snd.c sndPlaySfx() cannot be stubbed
 *    with zeros (it is deref'd 3 levels deep before any al* call), so a
 *    minimal fake bank graph is provided (portPcmBank below) and wired in
 *    via the crafted _sfxctl/_instrumentsctl segment blobs in
 *    port/src/segments.c.
 *
 * Callers audited: src/audi.c (amCreateAudioManager/amHandleFrameMessage),
 * src/music.c (musicSeqPlayerInit/musicTrackXPlay/Stop/ApplySeqpVol),
 * src/snd.c (sndNewPlayerInit/sndPlaySfx/sndSetupSound/sndPlayerVoiceHandler).
 */
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>

#include <ultra64.h>
#include <PR/libaudio.h>

/* ==== AUDIO PHASE 1c ===================================================== *
 * The REAL AL library is now compiled (see CMakeLists' audio block): sl.c
 * provides alGlobals/alInit/alClose/alLink/alUnlink; heapinit/heapalloc
 * the heap; event.c the event queues; synthesizer.c + syn*.c the voices;
 * load/resample/env_wip/save/buses/reverb/drvrNew the filter chain.
 * port/src/audio_pc.c executes the emitted Acmd lists and feeds SDL.
 * The stubs that remain below are ONLY the compressed-MIDI sequencer
 * layer (music phase) and the BE-aware alBnkfNew. */

/* ==== bank file ========================================================== */

/* ==== bank file (REAL, audio phase 1) ==================================== */
/*
 * The _sfxctl/_instrumentsctl segments now carry the REAL SGI .ctl images
 * (byte-exact BIG-ENDIAN, revision 'B1'; see port/src/segments.c). This is
 * bnkf.c's patch walk with the BE->native field conversion folded in: on
 * N64 native IS big-endian so SGI reads fields directly; on PC every
 * multibyte field must be swapped exactly once, then offsets promoted to
 * host pointers (i686 build: 4-byte pointers overwrite 4-byte offsets in
 * place, same as N64).
 *
 * Guards against double work: bank/instrument/sound/wavetable records
 * carry a flags byte (SGI's own revisit guard, reused). Envelopes, ADPCM
 * books and loop records have NO flags field and CAN be shared, so their
 * content-swap is tracked in a visited set; the pointer fields referencing
 * them are per-parent and always patched.
 *
 * Format verified against assets/music/sfx.ctl before writing this:
 * revision B1, bankCount 1, bank @0x5BA0 {instCount 1, sampleRate 22050,
 * instArray[0]=0x5778}, instrument {vol 7F, pan 40, bendRange 200,
 * soundCount 261, soundArray at +16} - STANDARD layout (GE's snd.h "Alt"
 * view reads soundArray at +12 with 1-based sfx indices; the addresses
 * coincide, so both views work off this patch).
 */

/* The .ctl image is the N64 file: big-endian, 4-byte offsets from the file
 * start (wave data offsets from the .tbl). alBnkfNew deserialises it into
 * separately allocated native structs rather than patching it in place, since
 * the libaudio.h structs only overlay the file when pointers are 4 bytes.
 * Shared records (envelopes, keymaps, wavetables, books, loops) stay shared:
 * each file offset maps to one native copy. File layouts (N64 offsets):
 *   ALBankFile   s16 revision, s16 bankCount, u32 bankArray[] @4
 *   ALBank       s16 instCount, u8 flags, u8 pad, s32 sampleRate @4,
 *                u32 percussion @8, u32 instArray[] @12
 *   ALInstrument 12 u8, s16 bendRange @12, s16 soundCount @14, u32 soundArray[] @16
 *   ALSound      u32 envelope, keyMap, wavetable, u8 samplePan @12, sampleVolume @13
 *   ALEnvelope   s32 attack, decay, release, u8 attackVolume @12, decayVolume @13
 *   ALWaveTable  u32 base, s32 len @4, u8 type @8, u8 flags @9, u32 loop @12, u32 book @16
 *   ALADPCMBook  s32 order, s32 npredictors, s16 book[order * npredictors * 8] @8
 *   ALADPCMloop  u32 start, end, count, s16 state[16] @12
 *   ALRawLoop    u32 start, end, count */

static u32 bnkfBe32(const u8 *p)
{
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | (u32)p[3];
}

static u16 bnkfBe16(const u8 *p)
{
    return (u16)(((u16)p[0] << 8) | p[1]);
}

#define BNKF_MAP_MAX 4096

struct bnkfmap { u32 off; void *native; };

struct bnkfctx {
    const u8 *src;    /* the BE .ctl image */
    u8 *table;        /* the .tbl wave data */
    struct bnkfmap map[BNKF_MAP_MAX];
    s32 nmap;
};

static void *bnkfFind(struct bnkfctx *c, u32 off)
{
    s32 i;
    for (i = 0; i < c->nmap; i++) {
        if (c->map[i].off == off) {
            return c->map[i].native;
        }
    }
    return NULL;
}

static void *bnkfNew(struct bnkfctx *c, u32 off, size_t size)
{
    void *p = calloc(1, size);
    if (c->nmap < BNKF_MAP_MAX) {
        c->map[c->nmap].off = off;
        c->map[c->nmap].native = p;
        c->nmap++;
    }
    return p;
}

/* RAW16 samples are big-endian s16 in the .tbl but the mixer plays native s16:
 * swap each wave once, even if the bank is loaded again */
static u8 *s_bnkfSwappedWaves[BNKF_MAP_MAX];
static s32 s_bnkfSwappedCount;

static void bnkfSwapRaw16(u8 *base, s32 len)
{
    s32 i;
    for (i = 0; i < s_bnkfSwappedCount; i++) {
        if (s_bnkfSwappedWaves[i] == base) {
            return;
        }
    }
    if (s_bnkfSwappedCount < BNKF_MAP_MAX) {
        s_bnkfSwappedWaves[s_bnkfSwappedCount++] = base;
    }
    for (i = 0; i + 1 < len; i += 2) {
        u8 t = base[i];
        base[i] = base[i + 1];
        base[i + 1] = t;
    }
}

static ALEnvelope *bnkfEnvelope(struct bnkfctx *c, u32 off)
{
    const u8 *s = c->src + off;
    ALEnvelope *e = bnkfFind(c, off);
    if (e != NULL) {
        return e;
    }
    e = bnkfNew(c, off, sizeof(*e));
    e->attackTime = (s32)bnkfBe32(s + 0);
    e->decayTime = (s32)bnkfBe32(s + 4);
    e->releaseTime = (s32)bnkfBe32(s + 8);
    e->attackVolume = s[12];
    e->decayVolume = s[13];
    return e;
}

static ALKeyMap *bnkfKeyMap(struct bnkfctx *c, u32 off)
{
    ALKeyMap *k = bnkfFind(c, off);
    if (k != NULL) {
        return k;
    }
    k = bnkfNew(c, off, sizeof(*k));
    memcpy(k, c->src + off, 6); /* all bytes */
    return k;
}

static ALADPCMBook *bnkfBook(struct bnkfctx *c, u32 off)
{
    const u8 *s = c->src + off;
    ALADPCMBook *b = bnkfFind(c, off);
    s32 order, npred, n, i;
    if (b != NULL) {
        return b;
    }
    order = (s32)bnkfBe32(s + 0);
    npred = (s32)bnkfBe32(s + 4);
    n = order * npred * 8;
    b = bnkfNew(c, off, sizeof(*b) + (size_t)(n > 0 ? n : 1) * sizeof(s16));
    b->order = order;
    b->npredictors = npred;
    for (i = 0; i < n; i++) {
        b->book[i] = (s16)bnkfBe16(s + 8 + i * 2);
    }
    return b;
}

static ALADPCMloop *bnkfAdpcmLoop(struct bnkfctx *c, u32 off)
{
    const u8 *s = c->src + off;
    ALADPCMloop *l = bnkfFind(c, off);
    s32 i;
    if (l != NULL) {
        return l;
    }
    l = bnkfNew(c, off, sizeof(*l));
    l->start = bnkfBe32(s + 0);
    l->end = bnkfBe32(s + 4);
    l->count = bnkfBe32(s + 8);
    for (i = 0; i < 16; i++) {
        ((s16 *)l->state)[i] = (s16)bnkfBe16(s + 12 + i * 2);
    }
    return l;
}

static ALRawLoop *bnkfRawLoop(struct bnkfctx *c, u32 off)
{
    const u8 *s = c->src + off;
    ALRawLoop *l = bnkfFind(c, off);
    if (l != NULL) {
        return l;
    }
    l = bnkfNew(c, off, sizeof(*l));
    l->start = bnkfBe32(s + 0);
    l->end = bnkfBe32(s + 4);
    l->count = bnkfBe32(s + 8);
    return l;
}

static ALWaveTable *bnkfWaveTable(struct bnkfctx *c, u32 off)
{
    const u8 *s = c->src + off;
    ALWaveTable *w = bnkfFind(c, off);
    u32 loop, book;
    if (w != NULL) {
        return w;
    }
    w = bnkfNew(c, off, sizeof(*w));
    w->base = c->table + bnkfBe32(s + 0);
    w->len = (s32)bnkfBe32(s + 4);
    w->type = s[8];
    w->flags = 1; /* patched */
    loop = bnkfBe32(s + 12);
    if (w->type == AL_ADPCM_WAVE) {
        book = bnkfBe32(s + 16);
        w->waveInfo.adpcmWave.loop = loop ? bnkfAdpcmLoop(c, loop) : NULL;
        w->waveInfo.adpcmWave.book = book ? bnkfBook(c, book) : NULL;
    } else if (w->type == AL_RAW16_WAVE) {
        w->waveInfo.rawWave.loop = loop ? bnkfRawLoop(c, loop) : NULL;
        bnkfSwapRaw16(w->base, w->len);
    }
    return w;
}

static ALSound *bnkfSound(struct bnkfctx *c, u32 off)
{
    const u8 *s = c->src + off;
    ALSound *snd = bnkfFind(c, off);
    if (snd != NULL) {
        return snd;
    }
    snd = bnkfNew(c, off, sizeof(*snd));
    snd->envelope = bnkfEnvelope(c, bnkfBe32(s + 0));
    snd->keyMap = bnkfKeyMap(c, bnkfBe32(s + 4));
    snd->wavetable = bnkfWaveTable(c, bnkfBe32(s + 8));
    snd->samplePan = s[12];
    snd->sampleVolume = s[13];
    snd->flags = 1; /* patched */
    return snd;
}

static ALInstrument *bnkfInstrument(struct bnkfctx *c, u32 off)
{
    const u8 *s = c->src + off;
    ALInstrument *inst = bnkfFind(c, off);
    s32 count, i;
    if (inst != NULL) {
        return inst;
    }
    count = (s16)bnkfBe16(s + 14);
    inst = bnkfNew(c, off, offsetof(ALInstrument, soundArray) + (size_t)(count > 0 ? count : 1) * sizeof(ALSound *));
    memcpy(inst, s, 12); /* volume .. vibDelay: bytes */
    inst->flags = 1; /* patched */
    inst->bendRange = (s16)bnkfBe16(s + 12);
    inst->soundCount = (s16)count;
    for (i = 0; i < count; i++) {
        u32 so = bnkfBe32(s + 16 + i * 4);
        inst->soundArray[i] = so ? bnkfSound(c, so) : NULL;
    }
    return inst;
}

static ALBank *bnkfBank(struct bnkfctx *c, u32 off)
{
    const u8 *s = c->src + off;
    ALBank *bank;
    s32 count, i;
    u32 perc;

    count = (s16)bnkfBe16(s + 0);
    bank = bnkfNew(c, off, offsetof(ALBank, instArray) + (size_t)(count > 0 ? count : 1) * sizeof(ALInstrument *));
    bank->instCount = (s16)count;
    bank->flags = 1; /* patched */
    bank->sampleRate = (s32)bnkfBe32(s + 4);
    perc = bnkfBe32(s + 8);
    bank->percussion = perc ? bnkfInstrument(c, perc) : NULL;
    for (i = 0; i < count; i++) {
        u32 io = bnkfBe32(s + 12 + i * 4);
        bank->instArray[i] = io ? bnkfInstrument(c, io) : NULL;
    }
    return bank;
}

void alBnkfNew(ALBankFile *f, u8 *table)
{
    static struct bnkfctx ctx; /* the offset map is too big for the stack */
    u8 *img = (u8 *)f;
    ALBank *banks[16];
    s32 count, i;

    if (bnkfBe16(img + 0) != AL_BANK_VERSION) {
        /* not a BE ctl image (or already converted) - leave it alone */
        return;
    }
    count = (s16)bnkfBe16(img + 2);
    if (count > (s32)(sizeof(banks) / sizeof(banks[0]))) {
        count = (s32)(sizeof(banks) / sizeof(banks[0]));
    }

    ctx.src = img;
    ctx.table = table;
    ctx.nmap = 0;
    for (i = 0; i < count; i++) {
        u32 bo = bnkfBe32(img + 4 + i * 4);
        banks[i] = bo ? bnkfBank(&ctx, bo) : NULL;
    }

    /* the caller reads bankArray[] out of its copy of the image, so overwrite
     * the image's header with the native one (the image is not read again) */
    f->revision = AL_BANK_VERSION;
    f->bankCount = (s16)count;
    for (i = 0; i < count; i++) {
        f->bankArray[i] = banks[i];
    }

    { /* bring-up (GE_DUMP_BOND): prove the parse - bank/inst/sound shape */
        extern void ge_dbg_vec(const char *tag, const float *v, int n);
        ALBank *bank = f->bankArray[0];
        float v[7];

        if (bank != NULL && bank->instArray[0] != NULL && bank->instArray[0]->soundArray[0] != NULL) {
            v[0] = (float)f->bankCount;
            v[1] = (float)bank->instCount;
            v[2] = (float)bank->sampleRate;
            v[3] = (float)bank->instArray[0]->soundCount;
            v[4] = (float)bank->instArray[0]->soundArray[0]->wavetable->type;
            v[5] = (float)bank->instArray[0]->soundArray[0]->wavetable->len;
            v[6] = (float)(uintptr_t)bank->instArray[0]->soundArray[0]->wavetable->base;
            ge_dbg_vec("bnkf banks/insts/rate/sounds/wt0type/len/base", v, 7);
        }
    }
}

/* ==== compressed-midi seq / seq player =================================== */

/**
 * Real alCSeqNew parses a compressed MIDI stream header. The stream our
 * crafted sample table produces is an empty deflate block (decompresses
 * to 0 bytes), so there is nothing to parse; zero the struct so any later
 * reader sees a consistent "empty sequence".
 */
/* (The compressed-MIDI sequencer stubs - alCSeqNew, the alCSP set, and
 * alSeqpSetBank - were retired with the MUSIC PHASE: cseq.c, csplayer.c,
 * the csp wrappers and seqpsetbank.c are compiled for real now, and the
 * real music.sbk feeds them via _musicsampletblSegmentRomStart.) */

/* (alCents2Ratio, alAudioFrame, the alSyn* voice API, and the fake
 * portPcmBank graph were retired with AUDIO PHASE 1c: the real library
 * provides the former, and the real banks replaced the latter.) */
