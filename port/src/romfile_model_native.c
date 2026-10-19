/**
 * romfile_model_native.c - builds a native model graph from a ROM model file
 * on 64-bit hosts.
 *
 * romfile_model.c byte-swaps a model file in place into the N64 layout: 4-byte
 * pointer slots holding 0x05000000-based offsets, and 8-byte display list
 * commands. On 32-bit hosts the game's promote step then rewrites those slots
 * into pointers in place. On 64-bit hosts a pointer does not fit a slot and a
 * Gfx is 16 bytes, so instead this pass builds the graph the game walks in
 * separate memory:
 *   - every node and every rodata record that holds a pointer is a native copy,
 *     its pointer fields resolved to native nodes, native records, widened
 *     display lists, or addresses inside the file image;
 *   - every display list is widened to host Gfx, with G_DL targets pointing at
 *     the widened copies; other segment-5 words (vertices, inline textures)
 *     stay as offsets, resolved through segment 5 = the file at draw time;
 *   - Switches[] and Textures[] are native arrays;
 *   - collision vertices keep their related node as a file offset
 *     (portModelCollisionNode).
 * Records without pointers, vertices, point usage and texture data stay in
 * the file image. The memory belongs to the image address: loading another
 * model there (the hand buffers do every weapon switch) frees the old graph.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#if UINTPTR_MAX > 0xFFFFFFFFu

#include <ultra64.h>
#include "bondtypes.h"
#include "bondconstants.h"

extern void portConLogf(const char *fmt, ...);

#define NONE 0xFFFFFFFFu

/* ---- offset -> native pointer map (open addressing) ---------------------- */

struct omap {
    u32 *keys;   /* offset + 1, 0 = empty */
    void **vals;
    u32 cap;
    u32 count;
};

static u32 omapHash(u32 k)
{
    k ^= k >> 16;
    k *= 0x45d9f3bu;
    k ^= k >> 16;
    return k;
}

static void omapPut(struct omap *m, u32 off, void *val);

static void omapGrow(struct omap *m)
{
    struct omap n;
    u32 i;

    n.cap = m->cap ? m->cap * 2 : 256;
    n.count = 0;
    n.keys = calloc(n.cap, sizeof(u32));
    n.vals = calloc(n.cap, sizeof(void *));
    for (i = 0; i < m->cap; i++) {
        if (m->keys[i] != 0) {
            omapPut(&n, m->keys[i] - 1, m->vals[i]);
        }
    }
    free(m->keys);
    free(m->vals);
    *m = n;
}

static void omapPut(struct omap *m, u32 off, void *val)
{
    u32 i;

    if ((m->count + 1) * 2 > m->cap) {
        omapGrow(m);
    }
    for (i = omapHash(off) & (m->cap - 1); m->keys[i] != 0; i = (i + 1) & (m->cap - 1)) {
        if (m->keys[i] == off + 1) {
            m->vals[i] = val;
            return;
        }
    }
    m->keys[i] = off + 1;
    m->vals[i] = val;
    m->count++;
}

static void *omapGet(const struct omap *m, u32 off)
{
    u32 i;

    if (m->cap == 0) {
        return NULL;
    }
    for (i = omapHash(off) & (m->cap - 1); m->keys[i] != 0; i = (i + 1) & (m->cap - 1)) {
        if (m->keys[i] == off + 1) {
            return m->vals[i];
        }
    }
    return NULL;
}

static void omapFree(struct omap *m)
{
    free(m->keys);
    free(m->vals);
    memset(m, 0, sizeof(*m));
}

/* ---- per-image registry -------------------------------------------------- */

struct mnative {
    const u8 *img;
    void **allocs;     /* everything calloc'd for this model */
    u32 nallocs, capallocs;
    struct omap nodes; /* file offset -> native ModelNode, kept for collision lookups */
};

#define MNATIVE_MAX 256
static struct mnative s_Models[MNATIVE_MAX];

static void mnativeRelease(struct mnative *m)
{
    u32 i;
    for (i = 0; i < m->nallocs; i++) {
        free(m->allocs[i]);
    }
    free(m->allocs);
    omapFree(&m->nodes);
    memset(m, 0, sizeof(*m));
}

static struct mnative *mnativeFor(const u8 *img)
{
    s32 i, freeslot = -1;

    for (i = 0; i < MNATIVE_MAX; i++) {
        if (s_Models[i].img == img) {
            mnativeRelease(&s_Models[i]);
            s_Models[i].img = img;
            return &s_Models[i];
        }
        if (freeslot < 0 && s_Models[i].img == NULL) {
            freeslot = i;
        }
    }
    if (freeslot < 0) {
        /* more live images than slots, which romfileModelNativeForget should
         * prevent: recycle slot 0 and say so, since that model may be live */
        portConLogf("ge: model64: %d native models live, recycling slot 0\n", MNATIVE_MAX);
        freeslot = 0;
        mnativeRelease(&s_Models[0]);
    }
    s_Models[freeslot].img = img;
    return &s_Models[freeslot];
}

/* A memory pool was reset (memp.c mempResetBank): the model images in it are
 * gone, so release their native graphs before the addresses get reused. */
void romfileModelNativeForget(const void *lo, const void *hi)
{
    s32 i;

    for (i = 0; i < MNATIVE_MAX; i++) {
        if (s_Models[i].img != NULL && (const void *) s_Models[i].img >= lo
                && (const void *) s_Models[i].img < hi) {
            mnativeRelease(&s_Models[i]);
        }
    }
}

/* ---- builder -------------------------------------------------------------- */

struct mbuild {
    u8 *img;
    u32 size;
    const char *name;
    struct mnative *m;
    struct omap recops; /* rodata offset -> opcode + 1 (from the node that owns it) */
    struct omap recs;   /* rodata offset -> native record */
    struct omap dls;    /* DL offset -> widened Gfx[] */
    struct omap fixed;  /* collision vertex arrays already repaired */
    u32 *queue;
    u32 nqueue, capqueue;
    s32 bad;
    ModelFileTextures *textures; /* native Textures[], built before the records */
    u32 texbase, numtextures;    /* the file's 12-byte table: offset and count */
};

static void *mbAlloc(struct mbuild *b, size_t n)
{
    struct mnative *m = b->m;
    void *p = calloc(1, n ? n : 1);

    if (m->nallocs == m->capallocs) {
        m->capallocs = m->capallocs ? m->capallocs * 2 : 256;
        m->allocs = realloc(m->allocs, m->capallocs * sizeof(void *));
    }
    m->allocs[m->nallocs++] = p;
    return p;
}

static u32 rd32(const struct mbuild *b, u32 off)
{
    u32 v;
    memcpy(&v, b->img + off, 4);
    return v;
}

static u16 rd16(const struct mbuild *b, u32 off)
{
    u16 v;
    memcpy(&v, b->img + off, 2);
    return v;
}

static f32 rdf(const struct mbuild *b, u32 off)
{
    f32 v;
    memcpy(&v, b->img + off, 4);
    return v;
}

/* a 4-byte slot -> file offset, or NONE for null / not a segment-5 reference */
static u32 segoff(u32 v)
{
    if (v == 0 || (v >> 24) != 0x05) {
        return NONE;
    }
    return v & 0x00FFFFFFu;
}

static int inFile(const struct mbuild *b, u32 off, u32 len)
{
    return off != NONE && off < b->size && len <= b->size - off;
}

static ModelNode *node(struct mbuild *b, u32 off)
{
    return off == NONE ? NULL : (ModelNode *) omapGet(&b->m->nodes, off);
}

static void *rawptr(struct mbuild *b, u32 slot)
{
    u32 off = segoff(slot);
    return inFile(b, off, 1) ? b->img + off : NULL;
}

static void *record(struct mbuild *b, u32 off, u32 opcode);
static Gfx *widenDL(struct mbuild *b, u32 off, int depth);

/* A gunfire/shadow image slot: on N64 it points at a 12-byte entry of the
 * file's Textures table, which texLoad later fills in. Point at the native
 * entry instead, so the layout is the 64-bit sImageTableEntry and the loaded
 * texture is shared as on N64; anything else gets a native copy. */
_Static_assert(sizeof(ModelFileTextures) == sizeof(sImageTableEntry)
        && offsetof(ModelFileTextures, Width) == offsetof(sImageTableEntry, width)
        && offsetof(ModelFileTextures, tflags) == offsetof(sImageTableEntry, flagsT),
        "ModelFileTextures and sImageTableEntry must share a layout");

static void *imageptr(struct mbuild *b, u32 slot)
{
    u32 off = segoff(slot);
    sImageTableEntry *e;

    if (off == NONE) {
        return NULL;
    }
    if (off >= b->texbase && off < b->texbase + b->numtextures * 12 && (off - b->texbase) % 12 == 0) {
        return &b->textures[(off - b->texbase) / 12];
    }
    if (!inFile(b, off, 12)) {
        b->bad++;
        return NULL;
    }
    e = mbAlloc(b, sizeof(*e));
    e->index = rd32(b, off);
    memcpy(&e->width, b->img + off + 4, 8);
    return e;
}

/* a pointer slot whose target may be a node, another node's rodata, or data */
static void *resolve(struct mbuild *b, u32 slot)
{
    u32 off = segoff(slot);
    void *p;

    if (off == NONE) {
        return NULL;
    }
    if ((p = node(b, off)) != NULL) {
        return p;
    }
    p = omapGet(&b->recops, off);
    if (p != NULL) {
        return record(b, off, (u32) (uintptr_t) p - 1);
    }
    return inFile(b, off, 1) ? b->img + off : NULL;
}

/* ---- pass 1: discover nodes ---------------------------------------------- */

static void enqueue(struct mbuild *b, u32 off)
{
    if (!inFile(b, off, 24) || omapGet(&b->m->nodes, off) != NULL) {
        return;
    }
    omapPut(&b->m->nodes, off, mbAlloc(b, sizeof(ModelNode)));
    if (b->nqueue == b->capqueue) {
        b->capqueue = b->capqueue ? b->capqueue * 2 : 256;
        b->queue = realloc(b->queue, b->capqueue * sizeof(u32));
    }
    b->queue[b->nqueue++] = off;
}

static void discover(struct mbuild *b, u32 root, s32 numSwitches)
{
    u32 qi;
    s32 i;

    enqueue(b, root);
    for (i = 0; i < numSwitches; i++) {
        enqueue(b, segoff(rd32(b, (u32) i * 4)));
    }
    for (qi = 0; qi < b->nqueue; qi++) {
        u32 off = b->queue[qi];
        u32 opcode = rd16(b, off) & 0xff;
        u32 data = segoff(rd32(b, off + 0x04));

        enqueue(b, segoff(rd32(b, off + 0x08))); /* Parent */
        enqueue(b, segoff(rd32(b, off + 0x0c))); /* Next */
        enqueue(b, segoff(rd32(b, off + 0x10))); /* Prev */
        enqueue(b, segoff(rd32(b, off + 0x14))); /* Child */
        if (!inFile(b, data, 4)) {
            continue;
        }
        omapPut(&b->recops, data, (void *) (uintptr_t) (opcode + 1));
        switch (opcode) {
        case MODELNODE_OPCODE_HEADER:
        case MODELNODE_OPCODE_OP20:
            enqueue(b, segoff(rd32(b, data + 0x04))); /* FirstGroup */
            break;
        case MODELNODE_OPCODE_GROUP:
        case MODELNODE_OPCODE_OP03:
        case MODELNODE_OPCODE_OP17:
            enqueue(b, segoff(rd32(b, data + 0x14))); /* ChildGroup / othernode */
            break;
        case MODELNODE_OPCODE_LOD:
            enqueue(b, segoff(rd32(b, data + 0x08))); /* Affects */
            break;
        case MODELNODE_OPCODE_SWITCH:
            enqueue(b, segoff(rd32(b, data + 0x00))); /* Controls */
            break;
        case MODELNODE_OPCODE_BSP:
            enqueue(b, segoff(rd32(b, data + 0x18)));
            enqueue(b, segoff(rd32(b, data + 0x1c)));
            break;
        case MODELNODE_OPCODE_OP07:
            enqueue(b, segoff(rd32(b, data + 0x00)));
            enqueue(b, segoff(rd32(b, data + 0x04)));
            break;
        default:
            break;
        }
    }
}

/* ---- display lists -------------------------------------------------------- */

#define GBI_DL 0x06
#define GBI_DL_EX2 0xDE
#define GBI_ENDDL 0xB8
#define GBI_ENDDL_EX2 0xDF

static Gfx *widenDL(struct mbuild *b, u32 off, int depth)
{
    Gfx *dl;
    u32 n = 0, i;

    if (!inFile(b, off, 8) || depth > 32) {
        return NULL;
    }
    if ((dl = omapGet(&b->dls, off)) != NULL) {
        return dl;
    }
    for (;;) {
        u32 op;
        if (!inFile(b, off + n * 8, 8) || n > 0x8000) {
            b->bad++;
            return NULL;
        }
        op = rd32(b, off + n * 8) >> 24;
        n++;
        if (op == GBI_ENDDL || op == GBI_ENDDL_EX2) {
            break;
        }
    }
    dl = mbAlloc(b, n * sizeof(Gfx));
    omapPut(&b->dls, off, dl); /* before recursing: lists may share or loop */
    for (i = 0; i < n; i++) {
        u32 w0 = rd32(b, off + i * 8);
        u32 w1 = rd32(b, off + i * 8 + 4);
        u32 op = w0 >> 24;

        dl[i].words.w0 = w0;
        dl[i].words.w1 = w1;
        if ((op == GBI_DL || op == GBI_DL_EX2) && segoff(w1) != NONE) {
            Gfx *sub = widenDL(b, segoff(w1), depth + 1);
            if (sub != NULL) {
                dl[i].words.w1 = (uintptr_t) sub;
            }
        }
    }
    return dl;
}

static Gfx *dlptr(struct mbuild *b, u32 slot)
{
    return widenDL(b, segoff(slot), 0);
}

/* collision vertices: romfile_model.c swapped every vertex as 6 halfwords plus
 * 4 raw bytes, but in a collision vertex +8 is the related node's 4-byte
 * offset (so its halves came out swapped) and +0xC is an s16 related index
 * (left big-endian) */
static void fixCollisionVertices(struct mbuild *b, u32 off, s32 count)
{
    s32 i;

    if (count <= 0 || !inFile(b, off, (u32) count * 16) || omapGet(&b->fixed, off) != NULL) {
        return;
    }
    omapPut(&b->fixed, off, (void *) 1);
    for (i = 0; i < count; i++) {
        u8 *v = b->img + off + (u32) i * 16;
        u32 w = ((u32) v[9] << 24) | ((u32) v[8] << 16) | ((u32) v[11] << 8) | v[10];
        u8 t;

        memcpy(v + 8, &w, 4);
        t = v[12]; v[12] = v[13]; v[13] = t;
    }
}

/* ---- pass 2: records ------------------------------------------------------ */

/* a 4-byte field that romfile_model.c swapped as two halfwords (OP17's pos.y
 * and pos.z, which its field map takes for a GROUP record's u16s) */
static f32 rdfHalfSwapped(const struct mbuild *b, u32 off)
{
    const u8 *p = b->img + off;
    u32 w = ((u32) p[1] << 24) | ((u32) p[0] << 16) | ((u32) p[3] << 8) | p[2];
    f32 f;
    memcpy(&f, &w, 4);
    return f;
}

/* a 4-byte field romfile_model.c left big-endian (OP17's scale2) */
static f32 rdfBig(const struct mbuild *b, u32 off)
{
    const u8 *p = b->img + off;
    u32 w = ((u32) p[0] << 24) | ((u32) p[1] << 16) | ((u32) p[2] << 8) | p[3];
    f32 f;
    memcpy(&f, &w, 4);
    return f;
}

static ModelRoData_Child *children(struct mbuild *b, u32 slot, s32 count)
{
    u32 off = segoff(slot);
    ModelRoData_Child *c;
    s32 i;

    if (count <= 0 || count > 0x1000 || !inFile(b, off, (u32) count * 8)) {
        return NULL;
    }
    c = mbAlloc(b, (size_t) count * sizeof(*c));
    for (i = 0; i < count; i++) {
        u32 o = off + (u32) i * 8;
        c[i].NumEntries = b->img[o];
        c[i].unk01 = b->img[o + 1];
        c[i].unk02 = rd16(b, o + 2);
        c[i].unk04 = rawptr(b, rd32(b, o + 4));
    }
    return c;
}

static void *record(struct mbuild *b, u32 off, u32 opcode)
{
    u8 *in;
    void *r;

    if (!inFile(b, off, 4)) {
        return NULL;
    }
    if ((r = omapGet(&b->recs, off)) != NULL) {
        return r;
    }
    in = b->img + off;

    switch (opcode) {
    case MODELNODE_OPCODE_HEADER:
    case MODELNODE_OPCODE_OP20: {
        ModelRoData_HeaderRecord *h = mbAlloc(b, sizeof(*h));
        omapPut(&b->recs, off, h);
        h->AnimPart = rd16(b, off + 0x0);
        h->MatrixIndex = (s16) rd16(b, off + 0x2);
        h->FirstGroupNode = resolve(b, rd32(b, off + 0x4));
        memcpy(&h->GroupsAsF32, in + 0x8, 4); /* Group1/Group2, already native u16s */
        h->RwDataIndex = rd16(b, off + 0xc);
        h->reserved = rd16(b, off + 0xe);
        return h;
    }
    case MODELNODE_OPCODE_GROUP:
    case MODELNODE_OPCODE_OP03: {
        ModelRoData_GroupRecord *g = mbAlloc(b, sizeof(*g));
        omapPut(&b->recs, off, g);
        g->Origin.x = rdf(b, off + 0x0);
        g->Origin.y = rdf(b, off + 0x4);
        g->Origin.z = rdf(b, off + 0x8);
        g->JointID = rd16(b, off + 0xc);
        g->MatrixIDs[0] = (s16) rd16(b, off + 0xe);
        g->MatrixIDs[1] = (s16) rd16(b, off + 0x10);
        g->MatrixIDs[2] = (s16) rd16(b, off + 0x12);
        g->ChildGroupNode = resolve(b, rd32(b, off + 0x14));
        g->BoundingVolumeRadius = rdf(b, off + 0x18);
        return g;
    }
    case MODELNODE_OPCODE_OP17: {
        ModelRoData_Op17Record *o = mbAlloc(b, sizeof(*o));
        omapPut(&b->recs, off, o);
        o->hitpart = (s32) rd32(b, off + 0x0);
        o->radiusSq = rdf(b, off + 0x4);
        o->pos.x = rdf(b, off + 0x8);
        o->pos.y = rdfHalfSwapped(b, off + 0xc);
        o->pos.z = rdfHalfSwapped(b, off + 0x10);
        o->othernode = resolve(b, rd32(b, off + 0x14));
        o->scale1 = rdf(b, off + 0x18);
        o->scale2 = inFile(b, off + 0x1c, 4) ? rdfBig(b, off + 0x1c) : 0.0f;
        return o;
    }
    case MODELNODE_OPCODE_DL: {
        ModelRoData_DisplayListRecord *d = mbAlloc(b, sizeof(*d));
        omapPut(&b->recs, off, d);
        d->Primary = dlptr(b, rd32(b, off + 0x0));
        d->Secondary = dlptr(b, rd32(b, off + 0x4));
        d->BaseAddr = b->img;
        d->Vertices = rawptr(b, rd32(b, off + 0xc));
        d->numVertices = rd16(b, off + 0x10);
        d->ModelType = (s8) in[0x12];
        return d;
    }
    case MODELNODE_OPCODE_DLCOLLISION: {
        ModelRoData_DisplayList_CollisionRecord *d = mbAlloc(b, sizeof(*d));
        omapPut(&b->recs, off, d);
        d->Primary = dlptr(b, rd32(b, off + 0x0));
        d->Secondary = dlptr(b, rd32(b, off + 0x4));
        d->Vertices = rawptr(b, rd32(b, off + 0x8));
        d->numVertices = (s16) rd16(b, off + 0xc);
        d->numCollisionVertices = (s16) rd16(b, off + 0xe);
        d->CollisionVertices = rawptr(b, rd32(b, off + 0x10));
        d->PointUsage = rawptr(b, rd32(b, off + 0x14));
        d->ModelType = (s16) rd16(b, off + 0x18);
        d->RwDataIndex = rd16(b, off + 0x1a);
        d->BaseAddr = b->img;
        fixCollisionVertices(b, segoff(rd32(b, off + 0x10)), d->numCollisionVertices);
        return d;
    }
    case MODELNODE_OPCODE_DLPRIMARY: {
        ModelRoData_DisplayListPrimaryRecord *d = mbAlloc(b, sizeof(*d));
        omapPut(&b->recs, off, d);
        d->numVertices = (s32) rd32(b, off + 0x0);
        d->Vertices = rawptr(b, rd32(b, off + 0x4));
        d->Primary = dlptr(b, rd32(b, off + 0x8));
        d->BaseAddr = b->img;
        return d;
    }
    case MODELNODE_OPCODE_LOD: {
        ModelRoData_LODRecord *l = mbAlloc(b, sizeof(*l));
        omapPut(&b->recs, off, l);
        l->MinDistance = rdf(b, off + 0x0);
        l->MaxDistance = rdf(b, off + 0x4);
        l->Affects = node(b, segoff(rd32(b, off + 0x8)));
        l->RwDataIndex = rd16(b, off + 0xc);
        return l;
    }
    case MODELNODE_OPCODE_BSP: {
        ModelRoData_BSPRecord *p = mbAlloc(b, sizeof(*p));
        omapPut(&b->recs, off, p);
        p->Point.x = rdf(b, off + 0x0);
        p->Point.y = rdf(b, off + 0x4);
        p->Point.z = rdf(b, off + 0x8);
        p->Vector.x = rdf(b, off + 0xc);
        p->Vector.y = rdf(b, off + 0x10);
        p->Vector.z = rdf(b, off + 0x14);
        p->leftChild = node(b, segoff(rd32(b, off + 0x18)));
        p->rightChild = node(b, segoff(rd32(b, off + 0x1c)));
        p->reserved = (s16) rd16(b, off + 0x20);
        p->RwDataIndex = rd16(b, off + 0x22);
        return p;
    }
    case MODELNODE_OPCODE_GUNFIRE: {
        ModelRoData_GunfireRecord *g = mbAlloc(b, sizeof(*g));
        omapPut(&b->recs, off, g);
        g->Offset.x = rdf(b, off + 0x0);
        g->Offset.y = rdf(b, off + 0x4);
        g->Offset.z = rdf(b, off + 0x8);
        g->Size.x = rdf(b, off + 0xc);
        g->Size.y = rdf(b, off + 0x10);
        g->Size.z = rdf(b, off + 0x14);
        g->Image = imageptr(b, rd32(b, off + 0x18));
        g->Scale = rdf(b, off + 0x1c);
        g->RwDataIndex = rd16(b, off + 0x20);
        g->BaseAddr = b->img;
        return g;
    }
    case MODELNODE_OPCODE_SHADOW: {
        ModelRoData_ShadowRecord *s = mbAlloc(b, sizeof(*s));
        omapPut(&b->recs, off, s);
        s->pos.x = rdf(b, off + 0x0);
        s->pos.y = rdf(b, off + 0x4);
        s->size.x = rdf(b, off + 0x8);
        s->size.y = rdf(b, off + 0xc);
        s->image = imageptr(b, rd32(b, off + 0x10));
        s->HeaderNode = resolve(b, rd32(b, off + 0x14));
        s->Scale = rdf(b, off + 0x18);
        s->BaseAddr = b->img;
        return s;
    }
    case MODELNODE_OPCODE_SWITCH: {
        ModelRoData_SwitchRecord *s = mbAlloc(b, sizeof(*s));
        omapPut(&b->recs, off, s);
        s->Controls = node(b, segoff(rd32(b, off + 0x0)));
        s->RwDataIndex = rd16(b, off + 0x4);
        return s;
    }
    case MODELNODE_OPCODE_OP06: {
        ModelRoData_Op06Record *o = mbAlloc(b, sizeof(*o));
        omapPut(&b->recs, off, o);
        o->unk00 = rd32(b, off + 0x0);
        o->unk04 = rd32(b, off + 0x4);
        o->unk08 = rd32(b, off + 0x8);
        o->unk0C = rd32(b, off + 0xc);
        o->unk10 = rd32(b, off + 0x10);
        o->BaseAddr = b->img;
        return o;
    }
    case MODELNODE_OPCODE_OP11: {
        ModelRoData_Op11Record *o = mbAlloc(b, sizeof(*o));
        omapPut(&b->recs, off, o);
        memcpy(o->unk0c, in, sizeof(o->unk0c)); /* word 15 is an unused pointer slot */
        o->BoundingVolumeRadius = rdf(b, off + 0x40);
        o->RwDataIndex = rd16(b, off + 0x44);
        o->unk46 = rd16(b, off + 0x46);
        o->BaseAddr = b->img;
        return o;
    }
    case MODELNODE_OPCODE_OP05: {
        ModelRoData_Op05Record *o = mbAlloc(b, sizeof(*o));
        omapPut(&b->recs, off, o);
        o->NumChildren = (s32) rd32(b, off + 0x0);
        o->Children = children(b, rd32(b, off + 0x4), o->NumChildren);
        o->Vertices = rawptr(b, rd32(b, off + 0x8));
        o->Images = rawptr(b, rd32(b, off + 0xc));
        memcpy(o->Data, in + 0x10, sizeof(o->Data));
        o->unk1A0 = rd32(b, off + 0x1a0);
        o->BaseAddr = b->img;
        return o;
    }
    case MODELNODE_OPCODE_OP07: {
        ModelRoData_Op07Record *o = mbAlloc(b, sizeof(*o));
        omapPut(&b->recs, off, o);
        o->unk00 = node(b, segoff(rd32(b, off + 0x0)));
        o->unk04 = node(b, segoff(rd32(b, off + 0x4)));
        o->NumChildren = (s32) rd32(b, off + 0x8);
        o->Children = children(b, rd32(b, off + 0xc), o->NumChildren);
        o->Vertices = rawptr(b, rd32(b, off + 0x10));
        o->Images = rawptr(b, rd32(b, off + 0x14));
        memcpy(o->Data, in + 0x18, sizeof(o->Data));
        o->unk1A8 = rd16(b, off + 0x1a8);
        o->RwDataIndex = rd16(b, off + 0x1aa);
        o->BaseAddr = b->img;
        return o;
    }
    default:
        /* no pointer fields (BBOX, GROUPSIMPLE, HEAD, INTERLINK, OP14, OP16,
         * ...): the converted record in the file is already the native layout */
        omapPut(&b->recs, off, in);
        return in;
    }
}

/* ---- entry points ---------------------------------------------------------- */

/**
 * Build the native graph for a model file romfile_model.c has converted, and
 * point the header at it. Returns 0 if the file could not be walked.
 */
s32 romfileModelBuildNative(ModelFileHeader *hdr, u8 *img, u32 size, const char *name)
{
    struct mbuild b;
    u32 root, qi;
    s32 i;
    ModelNode **switches;
    ModelFileTextures *textures;

    if (hdr->numSwitches < 0 || hdr->numtextures < 0) {
        return 0;
    }
    root = (u32) hdr->numSwitches * 4 + (u32) hdr->numtextures * 12;
    if (root + 24 > size) {
        return 0;
    }

    memset(&b, 0, sizeof(b));
    b.img = img;
    b.size = size;
    b.name = name;
    b.m = mnativeFor(img);

    /* native Textures[] first: gunfire/shadow records point into it */
    textures = mbAlloc(&b, (size_t) (hdr->numtextures + 1) * sizeof(ModelFileTextures));
    for (i = 0; i < hdr->numtextures; i++) {
        u32 o = (u32) hdr->numSwitches * 4 + (u32) i * 12;
        textures[i].TextureID = rd32(&b, o); /* a number, or an inline 0x05 offset */
        memcpy(&textures[i].Width, img + o + 4, 7);
    }
    b.textures = textures;
    b.texbase = (u32) hdr->numSwitches * 4;
    b.numtextures = (u32) hdr->numtextures;

    discover(&b, root, hdr->numSwitches);

    /* records first (resolve() may need any of them), then the nodes */
    for (qi = 0; qi < b.nqueue; qi++) {
        u32 off = b.queue[qi];
        record(&b, segoff(rd32(&b, off + 0x04)), rd16(&b, off) & 0xff);
    }
    for (qi = 0; qi < b.nqueue; qi++) {
        u32 off = b.queue[qi];
        ModelNode *n = node(&b, off);
        u32 opcode = rd16(&b, off) & 0xff;

        n->Opcode = rd16(&b, off);
        n->Data = (union ModelRoData *) record(&b, segoff(rd32(&b, off + 0x04)), opcode);
        n->Parent = node(&b, segoff(rd32(&b, off + 0x08)));
        n->Next = node(&b, segoff(rd32(&b, off + 0x0c)));
        n->Prev = node(&b, segoff(rd32(&b, off + 0x10)));
        n->Child = node(&b, segoff(rd32(&b, off + 0x14)));
        if (opcode == MODELNODE_OPCODE_LOD && n->Data != NULL) {
            n->Child = n->Data->LOD.Affects; /* as modelPromoteNodeOffsetsToPointers does */
        }
    }

    switches = mbAlloc(&b, (size_t) (hdr->numSwitches + 1) * sizeof(ModelNode *));
    for (i = 0; i < hdr->numSwitches; i++) {
        switches[i] = node(&b, segoff(rd32(&b, (u32) i * 4)));
    }

    hdr->RootNode = node(&b, root);
    hdr->Switches = switches;
    hdr->Textures = textures;

    if (b.bad) {
        portConLogf("ge: romdata: model %s: native build hit %d bounds issues\n", name, b.bad);
    }
    free(b.queue);
    omapFree(&b.recops);
    omapFree(&b.recs);
    omapFree(&b.dls);
    omapFree(&b.fixed);
    return hdr->RootNode != NULL;
}

ModelNode *portModelCollisionNode(void *baseaddr, u32 offset)
{
    s32 i;
    u32 off = segoff(offset);

    if (off == NONE) {
        return NULL;
    }
    for (i = 0; i < MNATIVE_MAX; i++) {
        if (s_Models[i].img == baseaddr) {
            return (ModelNode *) omapGet(&s_Models[i].nodes, off);
        }
    }
    return NULL;
}

#endif /* 64-bit */
