/* ENH: key bindings (the launcher's Game settings > Key Bindings), after Aces of the Pacific Enhanced.
 *
 * Keys are XT set-1 make codes as host.c sends them (ENH_KEY_GREY = sent after an E0 prefix) with the
 * modifiers held with them (ENH_KEY_SHIFT / CTRL / ALT). While a race runs, a key bound to an action is sent
 * to the game as the action's own key (with the modifiers that key needs); a key that is the default of an
 * action bound elsewhere is dropped; every other key goes through as it is. The menus, the name entry and
 * the message boxes' answers (Y / N, digits, Enter) always get the keys as they are. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "enhanced/enhanced.h"

#define SHIFT ENH_KEY_SHIFT
#define CTRL  ENH_KEY_CTRL
#define ALT   ENH_KEY_ALT
#define GREY  ENH_KEY_GREY
#define MODS  (SHIFT | CTRL | ALT)

/* The launcher's list (launcher/keys.cpp): the same names and defaults. */
typedef struct {
    const char *name;
    u16 def;          /* the key it has by default, and what the game is sent */
    u16 key;          /* the key it has now (0 = none) */
} Action;

static Action actions[] = {
    { "accelerate",    GREY | 0x48, 0 },
    { "brake",         GREY | 0x50, 0 },
    { "steer_left",    GREY | 0x4B, 0 },
    { "steer_right",   GREY | 0x4D, 0 },
    { "handbrake",     0x39, 0 },
    { "gear_up",       0x1E, 0 },
    { "gear_down",     0x2C, 0 },
    { "mirror",        0x13, 0 },
    { "headlights",    0x23, 0 },
    { "wipers",        0x11, 0 },
    { "centring",      0x2E, 0 },
    { "radio",         0x32, 0 },
    { "knob",          0x20, 0 },
    { "chase_view",    0x3F, 0 },
    { "return_road",   0x40, 0 },
    { "mouse",         0x41, 0 },
    { "replay",        0x44, 0 },
    { "replay_pause",  0x43, 0 },
    { "window_size",   0x3B, 0 },
    { "detail",        0x3C, 0 },
    { "sensitivity",   0x3D, 0 },
    { "pause",         CTRL | 0x19, 0 },
    { "sound",         CTRL | 0x1F, 0 },
    { "music",         CTRL | 0x10, 0 },
    { "engine_sound",  CTRL | 0x12, 0 },
    { "joystick",      CTRL | 0x24, 0 },
    { "keyboard",      CTRL | 0x25, 0 },
};
#define NACTIONS ((int)(sizeof actions / sizeof actions[0]))

static bool racing, suspended;
static u16 mods;               /* the modifiers held now (every modifier key goes through) */
static u8 held_shift, held_ctrl, held_alt;   /* bit 0 = left, bit 1 = right */
/* per physical key (x & 0x1FF) while it is down: the game key sent for it, PASS = the key itself,
 * DROP = nothing; NONE = up, or down since before the race */
#define NONE 0xFFFF
#define PASS 0xFFFE
#define DROP 0
static u16 sent[0x200];

static void init_once(void)
{
    static bool done;
    if (done) return;
    done = true;
    for (int i = 0; i < NACTIONS; i++) actions[i].key = actions[i].def;
    for (int i = 0; i < 0x200; i++) sent[i] = NONE;
}

bool enh_keys_parse(const char *spec)
{
    init_once();
    char buf[1024];
    snprintf(buf, sizeof buf, "%s", spec);
    for (char *item = strtok(buf, ","); item; item = strtok(NULL, ",")) {
        char *eq = strchr(item, '=');
        if (!eq) return false;
        *eq = 0;
        char *end;
        unsigned long code = strtoul(eq + 1, &end, 0);
        if (*end || code > 0xFFFF || (code & ~(unsigned long)(MODS | GREY | 0x7F))) return false;
        int i = 0;
        while (i < NACTIONS && strcmp(actions[i].name, item)) i++;
        if (i == NACTIONS) return false;
        actions[i].key = (u16)code;
    }
    return true;
}

void enh_keys_race(bool on) { racing = on; }
void enh_keys_suspend(bool on) { suspended = on; }

static int push(u16 *out, int n, u16 key, bool down)
{
    out[n] = (u16)((key & (GREY | 0x7F)) | (down ? 0 : 0x80));
    return n + 1;
}

/* Keeps track of the modifiers; returns true for a modifier key. */
static bool modifier(u16 x, bool down)
{
    u8 *held = NULL, side = (x & GREY) ? 2 : 1;
    switch (x) {
    case 0x2A: held = &held_shift; side = 1; break;
    case 0x36: held = &held_shift; side = 2; break;
    case 0x1D: case GREY | 0x1D: held = &held_ctrl; break;
    case 0x38: case GREY | 0x38: held = &held_alt; break;
    default: return false;
    }
    if (down) *held |= side; else *held &= (u8)~side;
    mods = (u16)((held_shift ? SHIFT : 0) | (held_ctrl ? CTRL : 0) | (held_alt ? ALT : 0));
    return true;
}

/* The key `game` pressed with exactly its own modifiers: the ones held that it doesn't want are let go
 * around it, the ones it wants that aren't held are pressed around it (the game reads the modifiers when
 * the key goes down). */
static int press(u16 *out, u16 game)
{
    const u16 want = game & MODS;
    int n = 0;
    static const struct { u16 bit; u16 left, right; const u8 *held; } M[] = {
        { SHIFT, 0x2A, 0x36,        &held_shift },
        { CTRL,  0x1D, GREY | 0x1D, &held_ctrl },
        { ALT,   0x38, GREY | 0x38, &held_alt },
    };
    for (int i = 0; i < 3; i++) {
        if ((want & M[i].bit) && !*M[i].held) n = push(out, n, M[i].left, true);
        if (!(want & M[i].bit) && (*M[i].held & 1)) n = push(out, n, M[i].left, false);
        if (!(want & M[i].bit) && (*M[i].held & 2)) n = push(out, n, M[i].right, false);
    }
    n = push(out, n, game, true);
    for (int i = 0; i < 3; i++) {
        if ((want & M[i].bit) && !*M[i].held) n = push(out, n, M[i].left, false);
        if (!(want & M[i].bit) && (*M[i].held & 1)) n = push(out, n, M[i].left, true);
        if (!(want & M[i].bit) && (*M[i].held & 2)) n = push(out, n, M[i].right, true);
    }
    return n;
}

int enh_key_map(u16 x, bool down, u16 *out)
{
    init_once();
    if (modifier(x, down)) return -1;
    u16 *s = &sent[x & 0x1FF];
    if (!down) {                                   /* the key goes up: as it went down */
        u16 was = *s;
        *s = NONE;
        if (was == NONE || was == PASS) return -1;
        return was == DROP ? 0 : push(out, 0, was, false);
    }
    if (*s == PASS) return -1;                     /* typematic repeat */
    if (*s != NONE && *s != DROP) return press(out, *s);
    if (!racing || suspended) return -1;
    const u16 key = (u16)(x | mods);
    for (int i = 0; i < NACTIONS; i++)
        if (actions[i].key == key) {
            *s = actions[i].def;
            return press(out, actions[i].def);
        }
    for (int i = 0; i < NACTIONS; i++)
        if (actions[i].def == key && actions[i].key != key) {
            *s = DROP;                             /* its action moved to another key */
            return 0;
        }
    *s = PASS;
    return -1;
}

void enh_keys_focus_lost(void)
{
    init_once();
    held_shift = held_ctrl = held_alt = 0;
    mods = 0;
    for (int i = 0; i < 0x200; i++) sent[i] = NONE;
}
