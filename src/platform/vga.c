#include "platform/vga.h"

#include <string.h>

#include "host.h"
#include "mem.h"
#include "enhanced/enhanced.h"

static u8 dac[256][3];
static u16 start;
static bool dirty = true;

/* Last composed input, to report "changed" only when the screen or the DAC really changed. */
static u8 last_vram[65536];
static u8 last_dac[256][3];
static u16 last_start = 0xFFFF;

static int scale = 1;                  /* ENH: output = 320x200 times this */

static bool compose(u32 *xrgb)
{
    const u8 *vram = mp(VRAM_SEG, 0);
    static bool enh_shown;
    bool same = !dirty && start == last_start && memcmp(vram, last_vram, sizeof last_vram) == 0 &&
                memcmp(dac, last_dac, sizeof dac) == 0;
    if (same && !enh_shown) return false;
    dirty = false;
    memcpy(last_vram, vram, sizeof last_vram);
    memcpy(last_dac, dac, sizeof dac);
    last_start = start;

    u32 pal[256];
    for (int i = 0; i < 256; i++) {
        /* 6-bit DAC value to 8 bits: v << 2 | v >> 4, as VGA-to-RGB conversions usually do. */
        u32 r = (u32)(dac[i][0] << 2 | dac[i][0] >> 4);
        u32 g = (u32)(dac[i][1] << 2 | dac[i][1] >> 4);
        u32 b = (u32)(dac[i][2] << 2 | dac[i][2] >> 4);
        pal[i] = r << 16 | g << 8 | b;
    }
    if (scale == 1) {
        for (int i = 0; i < 320 * 200; i++)
            xrgb[i] = pal[vram[(u16)(start + i)]];
    } else {                           /* ENH: the VGA picture scaled up, then the enhanced view over it */
        int w = 320 * scale;
        for (int y = 0; y < 200; y++) {
            u32 *row = xrgb + (size_t)y * scale * w;
            for (int x = 0; x < 320; x++) {
                u32 c = pal[vram[(u16)(start + y * 320 + x)]];
                for (int i = 0; i < scale; i++) row[x * scale + i] = c;
            }
            for (int j = 1; j < scale; j++) memcpy(row + (size_t)j * w, row, sizeof(u32) * (size_t)w);
        }
    }
    enh_shown = enh_compose(xrgb, pal);
    return true;
}

void vga_init(void)
{
    memset(dac, 0, sizeof dac);
    start = 0;
    dirty = true;
    scale = enh_res_scale();
    host_set_frame_source(compose, 320 * scale, 200 * scale);
}

void vga_dac_write(u8 index, u8 r, u8 g, u8 b)
{
    dac[index][0] = r & 0x3F;
    dac[index][1] = g & 0x3F;
    dac[index][2] = b & 0x3F;
}

void vga_dac_read(u8 index, u8 *r, u8 *g, u8 *b)
{
    if (r) *r = dac[index][0];
    if (g) *g = dac[index][1];
    if (b) *b = dac[index][2];
}

void vga_set_start(u16 byte_offset) { start = byte_offset; }
u16  vga_start(void) { return start; }
