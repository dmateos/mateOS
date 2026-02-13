#include "mouse.h"
#include "arch/i686/io.h"

#define PS2_DATA    0x60
#define PS2_STATUS  0x64
#define PS2_COMMAND 0x64

static volatile int mouse_x = 0;
static volatile int mouse_y = 0;
static volatile uint8_t mouse_buttons = 0;
static int max_x = 1024;
static int max_y = 768;

// 3-byte packet assembly
static uint8_t packet[3];
static int packet_cycle = 0;

static void ps2_wait_write(void) {
    int timeout = 100000;
    while (timeout-- > 0) {
        if (!(inb(PS2_STATUS) & 0x02)) return;
    }
}

static void ps2_wait_read(void) {
    int timeout = 100000;
    while (timeout-- > 0) {
        if (inb(PS2_STATUS) & 0x01) return;
    }
}

static void ps2_write_cmd(uint8_t cmd) {
    ps2_wait_write();
    outb(PS2_COMMAND, cmd);
}

static void ps2_write_data(uint8_t data) {
    ps2_wait_write();
    outb(PS2_DATA, data);
}

static uint8_t ps2_read_data(void) {
    ps2_wait_read();
    return inb(PS2_DATA);
}

// Send a byte to the mouse (via PS/2 controller prefix 0xD4)
static void mouse_write(uint8_t byte) {
    ps2_write_cmd(0xD4);
    ps2_write_data(byte);
    ps2_read_data();  // ACK
}

void mouse_init(void) {
    // Enable auxiliary (mouse) device
    ps2_write_cmd(0xA8);

    // Read controller config byte
    ps2_write_cmd(0x20);
    uint8_t config = ps2_read_data();
    // Enable IRQ12 (bit 1) and enable mouse clock (clear bit 5)
    config |= 0x02;
    config &= ~0x20;
    // Write config back
    ps2_write_cmd(0x60);
    ps2_write_data(config);

    // Set mouse defaults
    mouse_write(0xF6);

    // Enable data reporting
    mouse_write(0xF4);

    packet_cycle = 0;
    printf("PS/2 mouse initialized\n");
}

void mouse_set_bounds(int w, int h) {
    max_x = w > 0 ? w : 1024;
    max_y = h > 0 ? h : 768;
    // Clamp current position
    if (mouse_x >= max_x) mouse_x = max_x - 1;
    if (mouse_y >= max_y) mouse_y = max_y - 1;
}

mouse_state_t mouse_get_state(void) {
    mouse_state_t s;
    s.x = mouse_x;
    s.y = mouse_y;
    s.buttons = mouse_buttons;
    return s;
}

void mouse_irq_handler(uint32_t num __attribute__((unused)),
                        uint32_t err __attribute__((unused))) {
    uint8_t data = inb(PS2_DATA);

    packet[packet_cycle] = data;
    packet_cycle++;

    if (packet_cycle == 1) {
        // Validate byte 0: bit 3 must be set (always-1 bit)
        if (!(data & 0x08)) {
            packet_cycle = 0;  // Resync
        }
        return;
    }

    if (packet_cycle < 3) return;

    // Complete 3-byte packet
    packet_cycle = 0;

    uint8_t flags = packet[0];
    mouse_buttons = flags & 0x07;

    // X delta
    int dx = (int)packet[1];
    if (flags & 0x10) dx |= 0xFFFFFF00;  // Sign extend
    // Y delta (PS/2 Y is inverted: positive = up)
    int dy = (int)packet[2];
    if (flags & 0x20) dy |= 0xFFFFFF00;

    // Discard overflows
    if (flags & 0xC0) return;

    mouse_x += dx;
    mouse_y -= dy;  // Negate: PS/2 up=positive, screen down=positive

    // Clamp
    if (mouse_x < 0) mouse_x = 0;
    if (mouse_x >= max_x) mouse_x = max_x - 1;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_y >= max_y) mouse_y = max_y - 1;
}
