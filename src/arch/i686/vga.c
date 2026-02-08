#include "vga.h"
#include "io.h"
#include "legacytty.h"
#include "../../lib.h"

static int vga_mode13h_active = 0;

// ============================================================
// VGA State Save/Restore
// ============================================================

// Saved VGA state for restoring text mode
static uint8_t saved_misc;
static uint8_t saved_seq[5];
static uint8_t saved_crtc[25];
static uint8_t saved_gc[9];
static uint8_t saved_ac[21];
static uint8_t saved_dac[256][3];

// Save all 4 planes of VGA memory (64KB each, but text mode only uses ~32KB)
// We save 64KB which covers the font plane and text buffer
#define VGA_PLANE_SIZE 0x10000
static uint8_t saved_plane0[VGA_PLANE_SIZE];
static uint8_t saved_plane1[VGA_PLANE_SIZE];
static uint8_t saved_plane2[VGA_PLANE_SIZE];
static uint8_t saved_plane3[VGA_PLANE_SIZE];
static int state_saved = 0;

static void vga_save_state(void) {
    volatile uint8_t *vmem = (volatile uint8_t *)0xA0000;

    // Save Miscellaneous register
    saved_misc = inb(VGA_MISC_READ);

    // Save Sequencer registers
    for (int i = 0; i < 5; i++) {
        outb(VGA_SEQ_INDEX, (uint8_t)i);
        saved_seq[i] = inb(VGA_SEQ_DATA);
    }

    // Save CRTC registers
    for (int i = 0; i < 25; i++) {
        outb(VGA_CRTC_INDEX, (uint8_t)i);
        saved_crtc[i] = inb(VGA_CRTC_DATA);
    }

    // Save Graphics Controller registers
    for (int i = 0; i < 9; i++) {
        outb(VGA_GC_INDEX, (uint8_t)i);
        saved_gc[i] = inb(VGA_GC_DATA);
    }

    // Save Attribute Controller registers
    for (int i = 0; i < 21; i++) {
        inb(VGA_INSTAT_READ);  // Reset flip-flop
        outb(VGA_AC_INDEX, (uint8_t)i);
        saved_ac[i] = inb(VGA_AC_READ);
    }
    // Re-enable video
    inb(VGA_INSTAT_READ);
    outb(VGA_AC_INDEX, 0x20);

    // Save DAC palette
    outb(0x3C7, 0);  // DAC read index
    for (int i = 0; i < 256; i++) {
        saved_dac[i][0] = inb(VGA_DAC_DATA);
        saved_dac[i][1] = inb(VGA_DAC_DATA);
        saved_dac[i][2] = inb(VGA_DAC_DATA);
    }

    // Save video memory - all 4 planes
    // Set sequential mode for reading
    outb(VGA_SEQ_INDEX, 0x04);  // Memory Mode
    outb(VGA_SEQ_DATA, 0x06);   // Sequential, disable chain-4, disable odd/even

    outb(VGA_GC_INDEX, 0x05);   // Mode register
    outb(VGA_GC_DATA, 0x00);    // Read mode 0, write mode 0
    outb(VGA_GC_INDEX, 0x06);   // Misc register
    outb(VGA_GC_DATA, 0x05);    // Map A0000, disable odd/even

    for (int plane = 0; plane < 4; plane++) {
        outb(VGA_GC_INDEX, 0x04);   // Read Map Select
        outb(VGA_GC_DATA, (uint8_t)plane);

        uint8_t *dst;
        switch (plane) {
            case 0: dst = saved_plane0; break;
            case 1: dst = saved_plane1; break;
            case 2: dst = saved_plane2; break;
            default: dst = saved_plane3; break;
        }
        for (int i = 0; i < VGA_PLANE_SIZE; i++) {
            dst[i] = vmem[i];
        }
    }

    // Restore original sequencer and GC settings
    outb(VGA_SEQ_INDEX, 0x04);
    outb(VGA_SEQ_DATA, saved_seq[4]);
    outb(VGA_GC_INDEX, 0x05);
    outb(VGA_GC_DATA, saved_gc[5]);
    outb(VGA_GC_INDEX, 0x06);
    outb(VGA_GC_DATA, saved_gc[6]);
    outb(VGA_GC_INDEX, 0x04);
    outb(VGA_GC_DATA, saved_gc[4]);

    state_saved = 1;
}

static void vga_restore_state(void) {
    if (!state_saved) return;

    volatile uint8_t *vmem = (volatile uint8_t *)0xA0000;

    // Write Miscellaneous register
    outb(VGA_MISC_WRITE, saved_misc);

    // Write Sequencer registers (reset first)
    outb(VGA_SEQ_INDEX, 0x00);
    outb(VGA_SEQ_DATA, 0x01);  // Synchronous reset
    for (int i = 1; i < 5; i++) {
        outb(VGA_SEQ_INDEX, (uint8_t)i);
        outb(VGA_SEQ_DATA, saved_seq[i]);
    }
    outb(VGA_SEQ_INDEX, 0x00);
    outb(VGA_SEQ_DATA, 0x03);  // End reset

    // Unlock CRTC
    outb(VGA_CRTC_INDEX, 0x11);
    outb(VGA_CRTC_DATA, saved_crtc[0x11] & ~0x80);

    // Write CRTC registers
    for (int i = 0; i < 25; i++) {
        outb(VGA_CRTC_INDEX, (uint8_t)i);
        outb(VGA_CRTC_DATA, saved_crtc[i]);
    }

    // Write Graphics Controller registers
    for (int i = 0; i < 9; i++) {
        outb(VGA_GC_INDEX, (uint8_t)i);
        outb(VGA_GC_DATA, saved_gc[i]);
    }

    // Write Attribute Controller registers
    for (int i = 0; i < 21; i++) {
        inb(VGA_INSTAT_READ);
        outb(VGA_AC_INDEX, (uint8_t)i);
        outb(VGA_AC_WRITE, saved_ac[i]);
    }
    inb(VGA_INSTAT_READ);
    outb(VGA_AC_INDEX, 0x20);

    // Restore DAC palette
    outb(VGA_DAC_WRITE_INDEX, 0);
    for (int i = 0; i < 256; i++) {
        outb(VGA_DAC_DATA, saved_dac[i][0]);
        outb(VGA_DAC_DATA, saved_dac[i][1]);
        outb(VGA_DAC_DATA, saved_dac[i][2]);
    }

    // Restore video memory - all 4 planes
    outb(VGA_SEQ_INDEX, 0x04);  // Memory Mode
    outb(VGA_SEQ_DATA, 0x06);   // Sequential, disable chain-4

    outb(VGA_GC_INDEX, 0x05);   // Mode register
    outb(VGA_GC_DATA, 0x00);    // Write mode 0
    outb(VGA_GC_INDEX, 0x06);   // Misc register
    outb(VGA_GC_DATA, 0x05);    // Map A0000, disable odd/even

    for (int plane = 0; plane < 4; plane++) {
        outb(VGA_SEQ_INDEX, 0x02);  // Map Mask
        outb(VGA_SEQ_DATA, (uint8_t)(1 << plane));  // Select one plane

        const uint8_t *src;
        switch (plane) {
            case 0: src = saved_plane0; break;
            case 1: src = saved_plane1; break;
            case 2: src = saved_plane2; break;
            default: src = saved_plane3; break;
        }
        for (int i = 0; i < VGA_PLANE_SIZE; i++) {
            vmem[i] = src[i];
        }
    }

    // Restore sequencer and GC to text mode settings
    outb(VGA_SEQ_INDEX, 0x02);
    outb(VGA_SEQ_DATA, saved_seq[2]);
    outb(VGA_SEQ_INDEX, 0x04);
    outb(VGA_SEQ_DATA, saved_seq[4]);
    outb(VGA_GC_INDEX, 0x04);
    outb(VGA_GC_DATA, saved_gc[4]);
    outb(VGA_GC_INDEX, 0x05);
    outb(VGA_GC_DATA, saved_gc[5]);
    outb(VGA_GC_INDEX, 0x06);
    outb(VGA_GC_DATA, saved_gc[6]);
}

// ============================================================
// Mode 13h Register Tables
// ============================================================

static const uint8_t mode13h_misc = 0x63;

static const uint8_t mode13h_seq[] = {
    0x03, 0x01, 0x0F, 0x00, 0x0E
};

static const uint8_t mode13h_crtc[] = {
    0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0xBF, 0x1F,
    0x00, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x9C, 0x0E, 0x8F, 0x28, 0x40, 0x96, 0xB9, 0xA3,
    0xFF
};

static const uint8_t mode13h_gc[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x05, 0x0F,
    0xFF
};

static const uint8_t mode13h_ac[] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x41, 0x00, 0x0F, 0x00, 0x00
};

// ============================================================
// 8x8 Bitmap Font (ASCII 32-126)
// ============================================================

static const uint8_t font8x8[95][8] = {
    // 32: space
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 33: !
    {0x18, 0x3C, 0x3C, 0x18, 0x18, 0x00, 0x18, 0x00},
    // 34: "
    {0x36, 0x36, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 35: #
    {0x36, 0x36, 0x7F, 0x36, 0x7F, 0x36, 0x36, 0x00},
    // 36: $
    {0x0C, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x0C, 0x00},
    // 37: %
    {0x00, 0x63, 0x33, 0x18, 0x0C, 0x66, 0x63, 0x00},
    // 38: &
    {0x1C, 0x36, 0x1C, 0x6E, 0x3B, 0x33, 0x6E, 0x00},
    // 39: '
    {0x06, 0x06, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 40: (
    {0x18, 0x0C, 0x06, 0x06, 0x06, 0x0C, 0x18, 0x00},
    // 41: )
    {0x06, 0x0C, 0x18, 0x18, 0x18, 0x0C, 0x06, 0x00},
    // 42: *
    {0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00},
    // 43: +
    {0x00, 0x0C, 0x0C, 0x3F, 0x0C, 0x0C, 0x00, 0x00},
    // 44: ,
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x06},
    // 45: -
    {0x00, 0x00, 0x00, 0x3F, 0x00, 0x00, 0x00, 0x00},
    // 46: .
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x00},
    // 47: /
    {0x60, 0x30, 0x18, 0x0C, 0x06, 0x03, 0x01, 0x00},
    // 48: 0
    {0x3E, 0x63, 0x73, 0x7B, 0x6F, 0x67, 0x3E, 0x00},
    // 49: 1
    {0x0C, 0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x3F, 0x00},
    // 50: 2
    {0x1E, 0x33, 0x30, 0x1C, 0x06, 0x33, 0x3F, 0x00},
    // 51: 3
    {0x1E, 0x33, 0x30, 0x1C, 0x30, 0x33, 0x1E, 0x00},
    // 52: 4
    {0x38, 0x3C, 0x36, 0x33, 0x7F, 0x30, 0x78, 0x00},
    // 53: 5
    {0x3F, 0x03, 0x1F, 0x30, 0x30, 0x33, 0x1E, 0x00},
    // 54: 6
    {0x1C, 0x06, 0x03, 0x1F, 0x33, 0x33, 0x1E, 0x00},
    // 55: 7
    {0x3F, 0x33, 0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x00},
    // 56: 8
    {0x1E, 0x33, 0x33, 0x1E, 0x33, 0x33, 0x1E, 0x00},
    // 57: 9
    {0x1E, 0x33, 0x33, 0x3E, 0x30, 0x18, 0x0E, 0x00},
    // 58: :
    {0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x00},
    // 59: ;
    {0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x06},
    // 60: <
    {0x18, 0x0C, 0x06, 0x03, 0x06, 0x0C, 0x18, 0x00},
    // 61: =
    {0x00, 0x00, 0x3F, 0x00, 0x00, 0x3F, 0x00, 0x00},
    // 62: >
    {0x06, 0x0C, 0x18, 0x30, 0x18, 0x0C, 0x06, 0x00},
    // 63: ?
    {0x1E, 0x33, 0x30, 0x18, 0x0C, 0x00, 0x0C, 0x00},
    // 64: @
    {0x3E, 0x63, 0x7B, 0x7B, 0x7B, 0x03, 0x1E, 0x00},
    // 65: A
    {0x0C, 0x1E, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x00},
    // 66: B
    {0x3F, 0x66, 0x66, 0x3E, 0x66, 0x66, 0x3F, 0x00},
    // 67: C
    {0x3C, 0x66, 0x03, 0x03, 0x03, 0x66, 0x3C, 0x00},
    // 68: D
    {0x1F, 0x36, 0x66, 0x66, 0x66, 0x36, 0x1F, 0x00},
    // 69: E
    {0x7F, 0x46, 0x16, 0x1E, 0x16, 0x46, 0x7F, 0x00},
    // 70: F
    {0x7F, 0x46, 0x16, 0x1E, 0x16, 0x06, 0x0F, 0x00},
    // 71: G
    {0x3C, 0x66, 0x03, 0x03, 0x73, 0x66, 0x7C, 0x00},
    // 72: H
    {0x33, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x33, 0x00},
    // 73: I
    {0x1E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},
    // 74: J
    {0x78, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E, 0x00},
    // 75: K
    {0x67, 0x66, 0x36, 0x1E, 0x36, 0x66, 0x67, 0x00},
    // 76: L
    {0x0F, 0x06, 0x06, 0x06, 0x46, 0x66, 0x7F, 0x00},
    // 77: M
    {0x63, 0x77, 0x7F, 0x7F, 0x6B, 0x63, 0x63, 0x00},
    // 78: N
    {0x63, 0x67, 0x6F, 0x7B, 0x73, 0x63, 0x63, 0x00},
    // 79: O
    {0x1C, 0x36, 0x63, 0x63, 0x63, 0x36, 0x1C, 0x00},
    // 80: P
    {0x3F, 0x66, 0x66, 0x3E, 0x06, 0x06, 0x0F, 0x00},
    // 81: Q
    {0x1E, 0x33, 0x33, 0x33, 0x3B, 0x1E, 0x38, 0x00},
    // 82: R
    {0x3F, 0x66, 0x66, 0x3E, 0x36, 0x66, 0x67, 0x00},
    // 83: S
    {0x1E, 0x33, 0x07, 0x0E, 0x38, 0x33, 0x1E, 0x00},
    // 84: T
    {0x3F, 0x2D, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},
    // 85: U
    {0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x3F, 0x00},
    // 86: V
    {0x33, 0x33, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00},
    // 87: W
    {0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0x00},
    // 88: X
    {0x63, 0x63, 0x36, 0x1C, 0x1C, 0x36, 0x63, 0x00},
    // 89: Y
    {0x33, 0x33, 0x33, 0x1E, 0x0C, 0x0C, 0x1E, 0x00},
    // 90: Z
    {0x7F, 0x63, 0x31, 0x18, 0x4C, 0x66, 0x7F, 0x00},
    // 91: [
    {0x1E, 0x06, 0x06, 0x06, 0x06, 0x06, 0x1E, 0x00},
    // 92: backslash
    {0x03, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x40, 0x00},
    // 93: ]
    {0x1E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x1E, 0x00},
    // 94: ^
    {0x08, 0x1C, 0x36, 0x63, 0x00, 0x00, 0x00, 0x00},
    // 95: _
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF},
    // 96: `
    {0x0C, 0x0C, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 97: a
    {0x00, 0x00, 0x1E, 0x30, 0x3E, 0x33, 0x6E, 0x00},
    // 98: b
    {0x07, 0x06, 0x06, 0x3E, 0x66, 0x66, 0x3B, 0x00},
    // 99: c
    {0x00, 0x00, 0x1E, 0x33, 0x03, 0x33, 0x1E, 0x00},
    // 100: d
    {0x38, 0x30, 0x30, 0x3E, 0x33, 0x33, 0x6E, 0x00},
    // 101: e
    {0x00, 0x00, 0x1E, 0x33, 0x3F, 0x03, 0x1E, 0x00},
    // 102: f
    {0x1C, 0x36, 0x06, 0x0F, 0x06, 0x06, 0x0F, 0x00},
    // 103: g
    {0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x1F},
    // 104: h
    {0x07, 0x06, 0x36, 0x6E, 0x66, 0x66, 0x67, 0x00},
    // 105: i
    {0x0C, 0x00, 0x0E, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},
    // 106: j
    {0x30, 0x00, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E},
    // 107: k
    {0x07, 0x06, 0x66, 0x36, 0x1E, 0x36, 0x67, 0x00},
    // 108: l
    {0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},
    // 109: m
    {0x00, 0x00, 0x33, 0x7F, 0x7F, 0x6B, 0x63, 0x00},
    // 110: n
    {0x00, 0x00, 0x1F, 0x33, 0x33, 0x33, 0x33, 0x00},
    // 111: o
    {0x00, 0x00, 0x1E, 0x33, 0x33, 0x33, 0x1E, 0x00},
    // 112: p
    {0x00, 0x00, 0x3B, 0x66, 0x66, 0x3E, 0x06, 0x0F},
    // 113: q
    {0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x78},
    // 114: r
    {0x00, 0x00, 0x3B, 0x6E, 0x66, 0x06, 0x0F, 0x00},
    // 115: s
    {0x00, 0x00, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x00},
    // 116: t
    {0x08, 0x0C, 0x3E, 0x0C, 0x0C, 0x2C, 0x18, 0x00},
    // 117: u
    {0x00, 0x00, 0x33, 0x33, 0x33, 0x33, 0x6E, 0x00},
    // 118: v
    {0x00, 0x00, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00},
    // 119: w
    {0x00, 0x00, 0x63, 0x6B, 0x7F, 0x7F, 0x36, 0x00},
    // 120: x
    {0x00, 0x00, 0x63, 0x36, 0x1C, 0x36, 0x63, 0x00},
    // 121: y
    {0x00, 0x00, 0x33, 0x33, 0x33, 0x3E, 0x30, 0x1F},
    // 122: z
    {0x00, 0x00, 0x3F, 0x19, 0x0C, 0x26, 0x3F, 0x00},
    // 123: {
    {0x38, 0x0C, 0x0C, 0x07, 0x0C, 0x0C, 0x38, 0x00},
    // 124: |
    {0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x18, 0x00},
    // 125: }
    {0x07, 0x0C, 0x0C, 0x38, 0x0C, 0x0C, 0x07, 0x00},
    // 126: ~
    {0x6E, 0x3B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
};

// ============================================================
// VGA Register Programming (for Mode 13h entry)
// ============================================================

static void vga_write_regs(uint8_t misc, const uint8_t *seq,
                           const uint8_t *crtc, const uint8_t *gc,
                           const uint8_t *ac) {
    // Write Miscellaneous register
    outb(VGA_MISC_WRITE, misc);

    // Write Sequencer registers (with reset)
    outb(VGA_SEQ_INDEX, 0x00);
    outb(VGA_SEQ_DATA, 0x01);  // Synchronous reset
    for (int i = 1; i < 5; i++) {
        outb(VGA_SEQ_INDEX, (uint8_t)i);
        outb(VGA_SEQ_DATA, seq[i]);
    }
    outb(VGA_SEQ_INDEX, 0x00);
    outb(VGA_SEQ_DATA, 0x03);  // End reset

    // Unlock CRTC registers
    outb(VGA_CRTC_INDEX, 0x11);
    outb(VGA_CRTC_DATA, inb(VGA_CRTC_DATA) & ~0x80);

    // Write CRTC registers
    for (int i = 0; i < 25; i++) {
        outb(VGA_CRTC_INDEX, (uint8_t)i);
        outb(VGA_CRTC_DATA, crtc[i]);
    }

    // Write Graphics Controller registers
    for (int i = 0; i < 9; i++) {
        outb(VGA_GC_INDEX, (uint8_t)i);
        outb(VGA_GC_DATA, gc[i]);
    }

    // Write Attribute Controller registers
    for (int i = 0; i < 21; i++) {
        inb(VGA_INSTAT_READ);
        outb(VGA_AC_INDEX, (uint8_t)i);
        outb(VGA_AC_WRITE, ac[i]);
    }

    // Enable video output
    inb(VGA_INSTAT_READ);
    outb(VGA_AC_INDEX, 0x20);
}

// ============================================================
// Mode Switching
// ============================================================

void vga_enter_mode13h(void) {
    // Save current text mode state
    vga_save_state();

    // Program Mode 13h registers
    vga_write_regs(mode13h_misc, mode13h_seq, mode13h_crtc,
                   mode13h_gc, mode13h_ac);
    vga_init_palette();
    vga_clear(0);
    vga_mode13h_active = 1;
}

void vga_enter_text_mode(void) {
    // Restore saved text mode state (registers + video memory + font)
    vga_restore_state();
    vga_mode13h_active = 0;
}

int vga_is_mode13h(void) {
    return vga_mode13h_active;
}

// ============================================================
// Drawing Primitives
// ============================================================

void vga_put_pixel(int x, int y, uint8_t color) {
    if (x < 0 || x >= VGA_WIDTH || y < 0 || y >= VGA_HEIGHT)
        return;
    VGA_FB[y * VGA_WIDTH + x] = color;
}

void vga_fill_rect(int x, int y, int w, int h, uint8_t color) {
    for (int row = y; row < y + h; row++) {
        if (row < 0 || row >= VGA_HEIGHT) continue;
        for (int col = x; col < x + w; col++) {
            if (col < 0 || col >= VGA_WIDTH) continue;
            VGA_FB[row * VGA_WIDTH + col] = color;
        }
    }
}

void vga_clear(uint8_t color) {
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        VGA_FB[i] = color;
    }
}

void vga_draw_line(int x0, int y0, int x1, int y1, uint8_t color) {
    int dx = x1 - x0;
    int dy = y1 - y0;
    int sx = (dx > 0) ? 1 : -1;
    int sy = (dy > 0) ? 1 : -1;

    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;

    int err = dx - dy;

    while (1) {
        vga_put_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;

        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

// ============================================================
// Text Rendering
// ============================================================

void vga_draw_char(int x, int y, char c, uint8_t color) {
    if (c < 32 || c > 126) return;
    const uint8_t *glyph = font8x8[c - 32];

    for (int row = 0; row < 8; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 8; col++) {
            if (bits & (1 << col)) {
                vga_put_pixel(x + col, y + row, color);
            }
        }
    }
}

void vga_draw_string(int x, int y, const char *str, uint8_t color) {
    int cx = x;
    while (*str) {
        if (*str == '\n') {
            y += 10;
            cx = x;
        } else {
            vga_draw_char(cx, y, *str, color);
            cx += 8;
        }
        str++;
    }
}

// ============================================================
// Palette
// ============================================================

void vga_set_palette_entry(uint8_t index, uint8_t r, uint8_t g, uint8_t b) {
    outb(VGA_DAC_WRITE_INDEX, index);
    outb(VGA_DAC_DATA, r & 0x3F);
    outb(VGA_DAC_DATA, g & 0x3F);
    outb(VGA_DAC_DATA, b & 0x3F);
}

void vga_init_palette(void) {
    static const uint8_t cga_palette[16][3] = {
        { 0,  0,  0},  // 0: Black
        { 0,  0, 42},  // 1: Blue
        { 0, 42,  0},  // 2: Green
        { 0, 42, 42},  // 3: Cyan
        {42,  0,  0},  // 4: Red
        {42,  0, 42},  // 5: Magenta
        {42, 21,  0},  // 6: Brown
        {42, 42, 42},  // 7: Light Gray
        {21, 21, 21},  // 8: Dark Gray
        {21, 21, 63},  // 9: Light Blue
        {21, 63, 21},  // 10: Light Green
        {21, 63, 63},  // 11: Light Cyan
        {63, 21, 21},  // 12: Light Red
        {63, 21, 63},  // 13: Light Magenta
        {63, 63, 21},  // 14: Yellow
        {63, 63, 63},  // 15: White
    };

    for (int i = 0; i < 16; i++) {
        vga_set_palette_entry((uint8_t)i,
            cga_palette[i][0], cga_palette[i][1], cga_palette[i][2]);
    }

    // 6x6x6 color cube (indices 16-231)
    uint8_t idx = 16;
    for (int r = 0; r < 6; r++) {
        for (int g = 0; g < 6; g++) {
            for (int b = 0; b < 6; b++) {
                vga_set_palette_entry(idx++,
                    (uint8_t)(r * 63 / 5),
                    (uint8_t)(g * 63 / 5),
                    (uint8_t)(b * 63 / 5));
            }
        }
    }

    // Grayscale ramp (indices 232-255)
    for (int i = 0; i < 24; i++) {
        uint8_t v = (uint8_t)(i * 63 / 23);
        vga_set_palette_entry((uint8_t)(232 + i), v, v, v);
    }
}
