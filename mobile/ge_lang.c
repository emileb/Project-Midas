// Translated text: Data/lang_<code>.txt (made with mobile/lang/extract_text.py) replaces the English
// text banks slot by slot through langGet. A slot the file leaves empty stays English.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <strings.h>

#include <android/log.h>

#include "ge_glyphs.h"

#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "GOLDENEYE", __VA_ARGS__))

#define LANG_BANKS 45   // language.c LnameX_lookuptable
#define LANG_SLOTS 1024 // slotID & 0x3FF

extern void *LnameX_lookuptable[LANG_BANKS][2];
extern const char *portRootPath(const char *name);

static char sCode[16];
// Letters above ASCII are single bytes drawn with ge_glyphs.h: CP1252 accents, or Russian
// А..я as 0xC0..0xFF (CP1251 order)
enum { PAGE_NONE = -1, PAGE_LATIN, PAGE_CYRILLIC };
static int sPage = PAGE_NONE;
static int sLoaded;
static char **sText[LANG_BANKS];

// The port's own menus are C strings, so they are found by their English (P:<English>=...)
#define PORT_TABLE 1024 // power of two, well over the ~330 strings
static struct
{
    char *english, *text;
    char *lower, *upper; // for a lowercased or capitalised copy of the English (the watch lowercases its labels)
} sPort[PORT_TABLE];
static int sPortCount;

void GE_LangSetCode(const char *code)
{
    snprintf(sCode, sizeof(sCode), "%s", code);
    if (strcmp(code, "ru") == 0)
        sPage = PAGE_CYRILLIC;
    else if (code[0] != '\0' && strcmp(code, "en") != 0)
        sPage = PAGE_LATIN;
    else
        sPage = PAGE_NONE;
}

// Case changes that know the language's letters above ASCII
int GE_LangUpper(int c)
{
    c &= 0xFF;
    if (sPage == PAGE_CYRILLIC && c >= 0xE0)
        return c - 0x20;
    if (sPage == PAGE_LATIN && ((c >= 0xE0 && c <= 0xFE && c != 0xF7) || c == 0x9C))
        return c == 0x9C ? 0x8C : c - 0x20; // ß stays (no capital)
    return (c >= 'a' && c <= 'z') ? c - 0x20 : c;
}

static int langLower(int c)
{
    c &= 0xFF;
    if (sPage == PAGE_CYRILLIC && c >= 0xC0 && c < 0xE0)
        return c + 0x20;
    if (sPage == PAGE_LATIN && ((c >= 0xC0 && c <= 0xDE && c != 0xD7) || c == 0x8C))
        return c == 0x8C ? 0x9C : c + 0x20;
    return (c >= 'A' && c <= 'Z') ? c + 0x20 : c;
}

// A letter with a CP1252 glyph: its byte, else 0 (folded to ASCII)
static int latinByte(unsigned cp)
{
    if (cp >= 0xC0 && cp <= 0xFF)
    {
        // Æ Ð × Ø Þ æ ð ÷ ø þ have no glyph (no language here uses them)
        static const unsigned char noGlyph[] = {0xC6, 0xD0, 0xD7, 0xD8, 0xDE, 0xE6, 0xF0, 0xF7, 0xF8, 0xFE};

        return memchr(noGlyph, (int)cp, sizeof(noGlyph)) ? 0 : (int)cp;
    }
    switch (cp)
    {
        case 0xA1: case 0xBF: return (int)cp; // ¡ ¿
        case 0x152: return 0x8C;              // Œ
        case 0x153: return 0x9C;              // œ
    }
    return 0;
}

// Characters with no glyph, as ASCII
static const char *foldLatin(unsigned cp)
{
    static const char *const latin[64] = {
        "A", "A", "A", "A", "A", "A", "AE", "C", "E", "E", "E", "E", "I", "I", "I", "I",  // U+00C0
        "D", "N", "O", "O", "O", "O", "O", "x", "O", "U", "U", "U", "U", "Y", "Th", "ss", // U+00D0
        "a", "a", "a", "a", "a", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i",  // U+00E0
        "d", "n", "o", "o", "o", "o", "o", "/", "o", "u", "u", "u", "u", "y", "th", "y",  // U+00F0
    };

    if (cp >= 0xC0 && cp <= 0xFF)
        return latin[cp - 0xC0];
    switch (cp)
    {
        case 0xA1: case 0xBF: return "";  // ¡ ¿
        case 0xAB: case 0xBB: case 0x201C: case 0x201D: return "\"";
        case 0xAA: return "a";
        case 0xBA: case 0xB0: return "o";
        case 0x2018: case 0x2019: return "'";
        case 0x2013: case 0x2014: return "-";
        case 0x2026: return "...";
        case 0xA0: case 0x202F: return " ";
        case 0x2116: return "N";  // №
        case 0x152: return "OE";
        case 0x153: return "oe";
    }
    return "?";
}

// Value as written (UTF-8, \n escapes) -> what the game draws
static char *decodeValue(const char *v)
{
    char *out = malloc(strlen(v) * 3 + 1);
    char *o = out;
    const unsigned char *p = (const unsigned char *)v;

    while (*p)
    {
        if (*p == '\\' && p[1])
        {
            *o++ = (p[1] == 'n') ? '\n' : (char)p[1];
            p += 2;
        }
        else if (*p < 0x80)
        {
            *o++ = (char)*p++;
        }
        else
        {
            unsigned cp = 0;
            int n = (*p >= 0xF0) ? 3 : (*p >= 0xE0) ? 2 : 1;
            const char *fold, *f;

            int caps;

            cp = *p++ & (0x3F >> n);
            for (int i = 0; i < n && (*p & 0xC0) == 0x80; i++)
                cp = (cp << 6) | (*p++ & 0x3F);
            if (sPage == PAGE_CYRILLIC && cp >= 0x401 && cp <= 0x451)
            {
                // Ё is written Е, as Russian print often does
                if (cp == 0x401 || cp == 0x451)
                    cp = (cp == 0x401) ? 0x415 : 0x435;
                if (cp >= 0x410 && cp <= 0x44F)
                {
                    *o++ = (char)(0xC0 + cp - 0x410);
                    continue;
                }
            }
            // In capitals ß is SS and Æ is AE, not ss and Ae
            caps = isupper(*p) || (o > out && isupper((unsigned char)o[-1]) && !islower(*p));
            if (sPage == PAGE_LATIN && latinByte(cp) && !(cp == 0xDF && caps))
            {
                *o++ = (char)latinByte(cp);
                continue;
            }
            fold = foldLatin(cp);
            for (f = fold; *f; f++)
                *o++ = (caps && (f > fold || cp == 0xDF)) ? (char)toupper((unsigned char)*f) : *f;
        }
    }
    *o = '\0';
    return out;
}

static int bankIndex(const char *name, size_t len)
{
    for (int b = 1; b < LANG_BANKS; b++)
    {
        const char *n = (const char *)LnameX_lookuptable[b][0];
        if (n != NULL && strlen(n) == len && memcmp(n, name, len) == 0)
            return b;
    }
    return -1;
}

static char **slotFor(const char *key, int create)
{
    const char *dot = strrchr(key, '.');
    int bank, slot;

    if (dot == NULL || (bank = bankIndex(key, (size_t)(dot - key))) < 0)
        return NULL;
    slot = atoi(dot + 1);
    if (slot < 0 || slot >= LANG_SLOTS)
        return NULL;
    if (sText[bank] == NULL)
    {
        if (!create)
            return NULL;
        sText[bank] = calloc(LANG_SLOTS, sizeof(char *));
    }
    return &sText[bank][slot];
}

static unsigned portHash(const char *s)
{
    unsigned h = 2166136261u;

    while (*s)
        h = (h ^ (unsigned char)tolower((unsigned char)*s++)) * 16777619u;
    return h & (PORT_TABLE - 1);
}

static void portAdd(char *english, char *text)
{
    unsigned h = portHash(english);

    while (sPort[h].english != NULL && strcmp(sPort[h].english, english) != 0)
        h = (h + 1) & (PORT_TABLE - 1);
    if (sPort[h].english == NULL)
    {
        if (sPortCount >= PORT_TABLE / 2)
        {
            free(english);
            free(text);
            return;
        }
        sPortCount++;
    }
    free(sPort[h].english);
    free(sPort[h].text);
    free(sPort[h].lower);
    free(sPort[h].upper);
    sPort[h].english = english;
    sPort[h].text = text;
    sPort[h].lower = strdup(text);
    sPort[h].upper = strdup(text);
    for (char *c = sPort[h].lower; *c; c++)
        *c = (char)langLower(*c);
    for (char *c = sPort[h].upper; *c; c++)
        *c = (char)GE_LangUpper(*c);
}

static int hasCase(const char *s, int (*is)(int))
{
    for (; *s; s++)
    {
        if (is((unsigned char)*s))
            return 1;
    }
    return 0;
}

// The "=" between key and value: the first one not escaped as \= (port keys are English text)
static char *splitEntry(char *line)
{
    for (char *c = line; *c; c++)
    {
        if (*c == '\\' && c[1])
            c++;
        else if (*c == '=')
            return c;
    }
    return NULL;
}

static void langLoad(void)
{
    char name[64];
    char line[4096];
    char lowers[512][2][32]; // "@lower" entries, resolved once every slot is in
    int nlower = 0, count = 0;
    FILE *f;

    snprintf(name, sizeof(name), "Data/lang_%s.txt", sCode);
    f = fopen(portRootPath(name), "r");
    if (f == NULL)
    {
        LOGI("lang: %s not found, English text", name);
        return;
    }

    while (fgets(line, sizeof(line), f) != NULL)
    {
        char *eq, **slot;

        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '#' || (eq = splitEntry(line)) == NULL || eq[1] == '\0')
            continue;
        *eq = '\0';
        if (strncmp(line, "P:", 2) == 0)
        {
            portAdd(decodeValue(line + 2), decodeValue(eq + 1));
            count++;
            continue;
        }
        if (strncmp(eq + 1, "@lower ", 7) == 0)
        {
            if (nlower < 512)
            {
                snprintf(lowers[nlower][0], 32, "%s", line);
                snprintf(lowers[nlower][1], 32, "%s", eq + 8);
                nlower++;
            }
            continue;
        }
        if ((slot = slotFor(line, 1)) != NULL)
        {
            free(*slot);
            // "@empty": nothing, where an empty value would mean English (a plural "s" Russian has no use for)
            *slot = strcmp(eq + 1, "@empty") == 0 ? strdup("") : decodeValue(eq + 1);
            count++;
        }
    }
    fclose(f);

    for (int i = 0; i < nlower; i++)
    {
        char **src = slotFor(lowers[i][1], 0);
        char **dst;

        if (src == NULL || *src == NULL || (dst = slotFor(lowers[i][0], 1)) == NULL)
            continue;
        *dst = strdup(*src);
        for (char *c = *dst; *c; c++)
            *c = (char)langLower(*c);
        count++;
    }
    LOGI("lang: %s, %d lines", name, count);
}

static void langEnsureLoaded(void)
{
    if (!sLoaded)
    {
        sLoaded = 1;
        if (sCode[0] != '\0' && strcmp(sCode, "en") != 0)
            langLoad();
    }
}

// language.c langGet: the translated slot, or NULL for the English one
const char *GE_LangGet(int slotID)
{
    int bank = slotID >> 10;

    langEnsureLoaded();
    if (bank < 0 || bank >= LANG_BANKS || sText[bank] == NULL)
        return NULL;
    return sText[bank][slotID & (LANG_SLOTS - 1)];
}

// textrelated.c, as text is drawn, measured or wrapped: the port's menu strings translated
const char *GE_LangPortText(const char *text)
{
    unsigned h;
    int other = -1;

    langEnsureLoaded();
    if (sPortCount == 0 || text == NULL)
        return text;
    // An exact match first, else the same words in another case, given back in the case asked for
    for (h = portHash(text); sPort[h].english != NULL; h = (h + 1) & (PORT_TABLE - 1))
    {
        if (strcmp(sPort[h].english, text) == 0)
            return sPort[h].text;
        if (other < 0 && strcasecmp(sPort[h].english, text) == 0)
            other = (int)h;
    }
    if (other < 0)
        return text;
    if (!hasCase(text, isupper))
        return sPort[other].lower;
    if (!hasCase(text, islower))
        return sPort[other].upper;
    return sPort[other].text;
}

// Same layout as the game's struct fontchar (bondtypes.h)
struct GeFontChar
{
    int index, baseline, height, width, kerningindex;
    unsigned char *pixeldata;
};

// textrelated.c, for a byte above 0x7F: the glyph in the language's letters, or NULL (the game's
// own handling). *twin is a Latin letter with the same kerning, to kern the next letter against.
const void *GE_LangGlyph(const void *fontChars, int c, char *twin)
{
    static struct GeFontChar sDrawn[2][2][128];
    const struct GeFontChar *chars = fontChars;
    const struct GeGlyph *g;
    struct GeFontChar *d;
    int f;

    c &= 0xFF;
    if (sPage == PAGE_NONE || c < 0x80)
        return NULL;
    f = chars['A' - 0x21].height >= 10; // Zurich Bold's capitals are 12 rows, Bank Gothic's 8
    g = &sLangGlyphs[sPage][f][c - 0x80];
    if (g->alias || g->width == 0)
    {
        // a look-alike, or a byte the page has no glyph for
        *twin = g->alias ? g->twin : '?';
        return &chars[(g->alias ? g->alias : '?') - 0x21];
    }
    *twin = g->twin;
    d = &sDrawn[sPage][f][c - 0x80];
    if (d->pixeldata == NULL)
    {
        d->index = c - 0x80; // below 0x80: drawn as an I8 glyph, like the Latin ones
        d->baseline = g->baseline;
        d->height = g->height;
        d->width = g->width;
        d->kerningindex = g->kern;
        d->pixeldata = (unsigned char *)sLangPixels + g->offset;
    }
    return d;
}

// textrelated.c textWrap: high bytes are one letter each, not half of a two-byte Japanese one
int GE_LangSingleByte(void)
{
    return sPage != PAGE_NONE;
}
