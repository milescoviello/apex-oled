// doomgeneric backend for the SteelSeries Apex Pro Gen 3 OLED: 128x40, 1bpp.
//
// Video: Doom renders at 320x200. The status bar is the bottom 32 rows, so the
// play area is 320x168; taking the middle 100 rows of that gives exactly
// 320x100, which is 3.2:1 - the panel's aspect - so the downscale to 128x40 is
// a uniform 2.5x with no distortion.
//
// The panel is one bit deep, so the greyscale is ordered-dithered with a 4x4
// Bayer matrix. Ordered dithering rather than error diffusion because it is
// temporally stable: Floyd-Steinberg reshuffles every pixel each frame and the
// whole screen boils, which is unwatchable in motion.
//
// Input: the keyboard's own evdev node, grabbed with EVIOCGRAB so WASD does not
// type into whatever window has focus. The grab is per-fd, so it dies with the
// process even on a crash.
#include <fcntl.h>
#include <linux/hidraw.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>

#include "doomgeneric.h"
#include "doomkeys.h"

// linux/input-event-codes.h and doomkeys.h both define KEY_* with different
// meanings (KEY_ENTER is 28 there, 13 here). Referring to either by name in
// this file silently picks whichever won the redefinition, so evdev codes are
// spelled out numerically instead.
enum {
    EVK_ESC = 1, EVK_1 = 2, EVK_7 = 8, EVK_TAB = 15, EVK_W = 17, EVK_E = 18,
    EVK_Y = 21, EVK_ENTER = 28, EVK_BACKSPACE = 14, EVK_LEFTCTRL = 29, EVK_A = 30, EVK_S = 31,
    EVK_D = 32, EVK_LEFTSHIFT = 42, EVK_N = 49, EVK_SPACE = 57,
    EVK_UP = 103, EVK_LEFT = 105, EVK_RIGHT = 106, EVK_DOWN = 108,
};

#define PANEL_W 128
#define PANEL_H 40
#define FB_LEN  (PANEL_W * PANEL_H / 8)   // 640
#define REPORT_LEN (FB_LEN + 2)           // 642: cmd + framebuffer + trailer

#define SRC_W DOOMGENERIC_RESX
#define SRC_H DOOMGENERIC_RESY
#define CROP_H (SRC_H / 2)                // middle band -> 3.2:1
#define CROP_Y ((SRC_H - 32 * SRC_H / 200 - CROP_H) / 2)

static void read_opts(void);

static int panel_fd = -1;
static int kbd_fd = -1;

// --- panel ----------------------------------------------------------------

static int open_panel(void) {
    DIR *d = opendir("/sys/class/hidraw");
    struct dirent *e;
    char path[512], buf[4096];
    int fd, found = -1;
    if (!d) return -1;
    while ((e = readdir(d)) && found < 0) {
        if (strncmp(e->d_name, "hidraw", 6)) continue;
        snprintf(path, sizeof path, "/sys/class/hidraw/%s/device/uevent", e->d_name);
        FILE *f = fopen(path, "r");
        if (!f) continue;
        size_t n = fread(buf, 1, sizeof buf - 1, f);
        buf[n] = 0;
        fclose(f);
        if (!strstr(buf, "0003:00001038:00001640")) continue;

        // the screen is the interface declaring a 642-byte feature report
        snprintf(path, sizeof path, "/sys/class/hidraw/%s/device/report_descriptor", e->d_name);
        f = fopen(path, "rb");
        if (!f) continue;
        n = fread(buf, 1, sizeof buf, f);
        fclose(f);
        static const unsigned char sig[] = {0x09, 0xF2, 0x96, 0x82, 0x02, 0xB1, 0x02};
        for (size_t i = 0; i + sizeof sig <= n; i++) {
            if (!memcmp(buf + i, sig, sizeof sig)) {
                snprintf(path, sizeof path, "/dev/%s", e->d_name);
                fd = open(path, O_WRONLY);
                if (fd >= 0) { found = fd; printf("panel: %s\n", path); }
                break;
            }
        }
    }
    closedir(d);
    return found;
}

static void panel_send(const unsigned char *fb) {
    unsigned char buf[REPORT_LEN + 1];
    buf[0] = 0x00;          // report number; this descriptor has no report IDs
    buf[1] = 0x61;          // framebuffer command
    memcpy(buf + 2, fb, FB_LEN);
    buf[REPORT_LEN] = 0x00;
    ioctl(panel_fd, HIDIOCSFEATURE(sizeof buf), buf);
}

// --- input ----------------------------------------------------------------

static int open_keyboard(void) {
    char path[256], name[256];
    for (int i = 0; i < 64; i++) {
        snprintf(path, sizeof path, "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        name[0] = 0;
        ioctl(fd, EVIOCGNAME(sizeof name), name);
        unsigned long keys[(0x2FF + 63) / 64];
        memset(keys, 0, sizeof keys);
        ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keys), keys);
        int has_a = (keys[EVK_A / 64] >> (EVK_A % 64)) & 1;
        if (strstr(name, "Apex Pro Gen 3") && has_a) {
            if (ioctl(fd, EVIOCGRAB, 1) == 0) {
                printf("keyboard: %s (%s, grabbed)\n", path, name);
                return fd;
            }
            fprintf(stderr, "could not grab %s\n", path);
        }
        close(fd);
    }
    return -1;
}

static unsigned char map_key(int code) {
    if (code >= EVK_1 && code <= EVK_7) return '1' + (code - EVK_1);
    switch (code) {
        case EVK_UP: case EVK_W:            return KEY_UPARROW;
        case EVK_DOWN: case EVK_S:          return KEY_DOWNARROW;
        case EVK_LEFT:                      return KEY_LEFTARROW;
        case EVK_RIGHT:                     return KEY_RIGHTARROW;
        case EVK_A:                         return KEY_STRAFE_L;
        case EVK_D:                         return KEY_STRAFE_R;
        case EVK_LEFTCTRL:                  return KEY_FIRE;
        // Enter must be KEY_ENTER: the menu selects with that, and mapping it
        // to KEY_USE left nothing bound to "confirm" - navigable menu, no way
        // to choose anything.
        case EVK_ENTER:                     return KEY_ENTER;
        case EVK_E: case EVK_SPACE:         return KEY_USE;
        case EVK_BACKSPACE:                 return KEY_BACKSPACE;
        case EVK_LEFTSHIFT:                 return KEY_RSHIFT;
        case EVK_ESC:                       return KEY_ESCAPE;
        case EVK_TAB:                       return KEY_TAB;
        case EVK_Y:                         return 'y';
        case EVK_N:                         return 'n';
        default: return 0;
    }
}

// --- doomgeneric hooks ----------------------------------------------------

void DG_Init(void) {
    panel_fd = open_panel();
    if (panel_fd < 0) { fprintf(stderr, "no Apex OLED found\n"); exit(1); }
    read_opts();
    kbd_fd = open_keyboard();
    if (kbd_fd < 0) fprintf(stderr, "no keyboard grabbed - input will not work\n");
}

// Tunables, read once from the environment so they can be dialled in without
// a rebuild:
//   APEX_DOOM_LIT   target fraction of the panel lit, 0.05-0.95 (default 0.38)
//   APEX_DOOM_POOL  max | avg   (default max)
//   APEX_DOOM_DITHER 1 to dither instead of hard threshold (default 0)
static float opt_lit = 0.38f;
static int opt_max_pool = 1, opt_dither = 0;

static void read_opts(void) {
    const char *e;
    if ((e = getenv("APEX_DOOM_LIT"))) {
        float v = atof(e);
        if (v > 0.05f && v < 0.95f) opt_lit = v;
    }
    if ((e = getenv("APEX_DOOM_POOL"))) opt_max_pool = strcmp(e, "avg") != 0;
    if ((e = getenv("APEX_DOOM_DITHER"))) opt_dither = atoi(e) != 0;
    printf("video: lit=%.2f pool=%s dither=%d\n", opt_lit,
           opt_max_pool ? "max" : "avg", opt_dither);
}

void DG_DrawFrame(void) {
    static const unsigned char bayer[4][4] = {
        {  0, 128,  32, 160 }, { 192,  64, 224,  96 },
        {  48, 176,  16, 144 }, { 240, 112, 208,  80 },
    };
    static unsigned char grey[PANEL_H][PANEL_W];
    unsigned char fb[FB_LEN];
    unsigned hist[256];

    memset(fb, 0, sizeof fb);
    memset(hist, 0, sizeof hist);

    for (int y = 0; y < PANEL_H; y++) {
        for (int x = 0; x < PANEL_W; x++) {
            int x0 = x * SRC_W / PANEL_W, x1 = (x + 1) * SRC_W / PANEL_W;
            int y0 = CROP_Y + y * CROP_H / PANEL_H;
            int y1 = CROP_Y + (y + 1) * CROP_H / PANEL_H;
            unsigned sum = 0, n = 0, best = 0;
            for (int yy = y0; yy < y1; yy++) {
                const uint32_t *row = DG_ScreenBuffer + (size_t)yy * SRC_W;
                for (int xx = x0; xx < x1; xx++) {
                    uint32_t p = row[xx];
                    unsigned l = (77 * ((p >> 16) & 0xFF) + 150 * ((p >> 8) & 0xFF)
                                  + 29 * (p & 0xFF)) >> 8;
                    sum += l; n++;
                    if (l > best) best = l;
                }
            }
            // Max-pooling keeps small bright features - an imp, a muzzle
            // flash - which averaging would blend into the wall behind them.
            unsigned v = !n ? 0 : (opt_max_pool ? best : sum / n);
            grey[y][x] = v;
            hist[v]++;
        }
    }

    // Auto-exposure: choose the cutoff so roughly opt_lit of the panel is lit,
    // whatever the level's brightness. A fixed cutoff blacks out a dark
    // corridor and whites out a courtyard.
    unsigned want = (unsigned)(PANEL_W * PANEL_H * opt_lit);
    unsigned acc = 0;
    int cut = 255;
    for (int v = 255; v >= 0; v--) {
        acc += hist[v];
        if (acc >= want) { cut = v; break; }
    }
    if (cut < 8) cut = 8;               // an all-black frame stays black

    for (int y = 0; y < PANEL_H; y++)
        for (int x = 0; x < PANEL_W; x++) {
            int lit;
            if (opt_dither) {
                int v = grey[y][x] * 128 / (cut ? cut : 1);
                lit = v > bayer[y & 3][x & 3];
            } else {
                lit = grey[y][x] >= cut;
            }
            if (lit) fb[(y / 8) * PANEL_W + x] |= 1 << (y & 7);
        }
    panel_send(fb);
}

void DG_SleepMs(uint32_t ms) { usleep(ms * 1000); }

uint32_t DG_GetTicksMs(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int DG_GetKey(int *pressed, unsigned char *key) {
    struct input_event ev;
    while (kbd_fd >= 0 && read(kbd_fd, &ev, sizeof ev) == sizeof ev) {
        if (ev.type != EV_KEY || ev.value == 2) continue;   // ignore autorepeat
        unsigned char k = map_key(ev.code);
        if (!k) continue;
        *pressed = ev.value ? 1 : 0;
        *key = k;
        return 1;
    }
    return 0;
}

void DG_SetWindowTitle(const char *title) { (void)title; }


int main(int argc, char **argv) {
    doomgeneric_Create(argc, argv);
    for (;;) doomgeneric_Tick();
    return 0;
}
