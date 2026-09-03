// matrix-rain-tty: the Matrix digital rain for a terminal.
//
// This is the code as it appears on the operators' screens and in the first film's
// opening titles: flat, crowded, no glow or gradient, a bright "cursor" glyph leading
// each drop, half-width katakana and digits. The raindrops use the same wobbling
// sawtooth as the GPU renderers (after Rezmason's matrix), the glyphs cycle the same
// way, but everything is drawn with plain text and 24-bit ANSI colours.
//
//   matrix-rain-tty [--screensaver] [--style operator|classic] [--head white|green] [--flat] [--trail N]
//                   [--speed-min F] [--speed-max F] [--period-k F] [--fps N] [--gap N]
//                   [--fall-speed F] [--cycle-speed F] [--raindrop-length F]
//                   [--animation-speed F]
//
// --screensaver exits on any key; otherwise q, Escape or Ctrl-C quits.
// Only cells that changed since the previous frame are redrawn.

#define _POSIX_C_SOURCE 200809L
#define PI 3.14159265358979323846
#define SQRT_2 1.41421356237309504880
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

// Rezmason's glyph order (Reloaded/Revolutions titles), in half-width katakana and ASCII
// so every glyph is exactly one terminal cell wide.
static const char *GLYPHS[] = {
    "ﾓ", "ｴ", "ﾔ", "ｷ", "ｵ", "ｶ", "7", "ｹ", "ｻ", "ｽ", "z", "1", "5", "2", "ﾖ", "ﾀ", "ﾜ", "4", "ﾈ", "ﾇ",
    "ﾅ", "9", "8", "ﾋ", "0", "ﾎ", "ｱ", "3", "ｳ", "ｾ", "|", ":", "\"", "=", "ﾐ", "ﾗ", "ﾘ", "-", "ﾂ", "ﾃ",
    "ﾆ", "ﾊ", "ｿ", ".", "-", "<", ">", "0", "|", "+", "*", "ｺ", "ｼ", "ﾏ", "ﾑ", "ﾒ",
};
enum { NGLYPHS = sizeof GLYPHS / sizeof GLYPHS[0] };

// Colour levels. 0 = off (space). The last level is the cursor.
enum { MAX_LEVELS = 12 };
typedef struct {
    int levels;                    // number of lit levels, cursor included
    char sgr[MAX_LEVELS][32];      // escape sequence per level
    float baseBrightness, baseContrast, brightnessOverride, brightnessThreshold;
} Palette;

typedef struct {
    const char *style, *color;
    float animationSpeed, fallSpeed, cycleSpeed, raindropLength, fps, trail;
    float speedMin, speedMax, periodK;   // column speed band (x fallSpeed) and period-vs-speed exponent
    int gap;
    bool screensaver, flat;
} Config;

typedef struct { uint8_t glyph, level; float age; uint8_t drawnGlyph, drawnLevel; } Cell;

static struct termios saved_termios;
static bool termios_saved = false;
static volatile sig_atomic_t got_resize = 0, got_quit = 0;

static void restore_terminal(void) {
    const char restore[] = "\x1b[0m\x1b[?7h\x1b[?25h\x1b[?1049l";
    (void)!write(STDOUT_FILENO, restore, sizeof restore - 1);
    if (termios_saved) tcsetattr(STDIN_FILENO, TCSANOW, &saved_termios);
}
static void on_signal(int sig) { if (sig == SIGWINCH) got_resize = 1; else got_quit = 1; }

// ---------- the rain (same functions as the shaders, in double precision) ----------
static double frac(double x) { return x - floor(x); }
static double random_float(double x, double y) {
    const double a = 12.9898, b = 78.233, c = 43758.5453;
    double dt = x * a + y * b, sn = fmod(dt, PI);
    return frac(sin(sn) * c);
}
static double hash13(double x, double y, double z) {
    x = frac(x * 0.1031); y = frac(y * 0.1031); z = frac(z * 0.1031);
    double d = x * (z + 31.32) + y * (y + 31.32) + z * (x + 31.32);
    x += d; y += d; z += d;
    return frac((x + y) * z);
}
// Raindrop brightness for a cell is 1 - fract(wobble((y * 0.01 + columnTime) / raindropLength)),
// where columnTime = random offset + t * fallSpeed * random speed; see the frame loop.
static double wobble(double x) { return x + 0.3 * sin(SQRT_2 * x) + 0.2 * sin(2.23606797749979 * x); }

// ---------- colours ----------
static double hsl_channel(double h, double s, double l, double n) {
    double a = s * fmin(l, 1.0 - l), k = fmod(n + h * 12.0, 12.0);
    return l - a * fmax(-1.0, fmin(fmin(k - 3.0, 9.0 - k), 1.0));
}
static void sgr_hsl(char *out, size_t n, double h, double s, double l, double gain) {
    int r = (int)(fmin(1.0, hsl_channel(h, s, l, 0) * gain) * 255 + 0.5);
    int g = (int)(fmin(1.0, hsl_channel(h, s, l, 8) * gain) * 255 + 0.5);
    int b = (int)(fmin(1.0, hsl_channel(h, s, l, 4) * gain) * 255 + 0.5);
    snprintf(out, n, "\x1b[38;2;%d;%d;%dm", r, g, b);
}

static void make_palette(Palette *p, const char *style, const char *color, bool flat) {
    memset(p, 0, sizeof *p);
    if (!strcmp(style, "classic")) {
        // The title-sequence gradient, quantised to a few shades of the green palette.
        p->levels = 9;
        p->baseBrightness = -0.5f; p->baseContrast = 1.1f; p->brightnessOverride = 0; p->brightnessThreshold = 0;
        for (int i = 1; i < p->levels - 1; i++) {
            double at = (double)i / (p->levels - 2);            // 0..1 across the visible range
            double l = at < 0.29 ? at / 0.29 * 0.2 : 0.2 + (at - 0.29) / 0.71 * 0.5; // palette stops 0.2@0.2, 0.7@0.7
            sgr_hsl(p->sgr[i], sizeof p->sgr[i], 0.3, 0.9, l, 1.0);
        }
        sgr_hsl(p->sgr[p->levels - 1], sizeof p->sgr[0], 0.242, 1.0, 0.73, 2.0);   // cursor
    } else {
        // Operator screens. The stream fades from bright green at the head to near-black at
        // the tail (a terminal has no alpha, but on black this reads as opacity); --flat
        // uses a single green like Rezmason's operator version.
        p->baseBrightness = -0.5f; p->baseContrast = 1.1f; p->brightnessThreshold = 0;
        if (flat) {
            p->levels = 3;
            p->brightnessOverride = 0.22f;
            sgr_hsl(p->sgr[1], sizeof p->sgr[1], 0.4, 0.8, 0.4, 1.0);
        } else {
            p->levels = 10;
            p->brightnessOverride = 0;
            for (int i = 1; i < p->levels - 1; i++) {
                double at = (double)(i - 1) / (p->levels - 3);          // 0 = tail, 1 = just behind the head
                sgr_hsl(p->sgr[i], sizeof p->sgr[i], 0.4, 0.8, 0.08 + at * 0.37, 1.0);
            }
        }
        int head = p->levels - 1;
        if (!strcmp(color, "white")) snprintf(p->sgr[head], sizeof p->sgr[head], "\x1b[38;2;255;255;255m");   // the drop: pure white
        else sgr_hsl(p->sgr[head], sizeof p->sgr[head], 0.375, 1.0, 0.66, 1.6);          // or the bright mint of Rezmason's operator
    }
}

// ---------- terminal ----------
static void get_size(int *cols, int *rows) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) { *cols = ws.ws_col; *rows = ws.ws_row; }
    else { *cols = 80; *rows = 24; }
}

static void usage(FILE *out) {
    fputs("usage: matrix-rain-tty [--screensaver] [--style operator|classic] [--head white|green] [--flat] [--trail N]\n"
          "                       [--speed-min F] [--speed-max F] [--period-k F] [--fps N] [--gap N]\n"
          "                       [--fall-speed F] [--cycle-speed F] [--raindrop-length F] [--animation-speed F]\n"
          "  --screensaver  exit on any key; otherwise q, Escape or Ctrl-C quits\n"
          "  --style        operator (flat, film operator screens; default) or classic (title gradient)\n"
          "  --head         colour of the leading glyph of each drop: white (default) or green; operator style only\n"
          "  --flat         operator style: one flat green for the whole stream instead of fading to black\n"
          "  --trail N      rows lit behind each head (default 20 operator; 0 = tied to the period, classic default)\n"
          "  --speed-min F  column speed band, as multiples of --fall-speed: ~95% of columns fall between\n"
          "  --speed-max F  these (defaults 0.5 and 1.5); the rest are faster or slower outliers, never stopped\n"
          "  --period-k F   how much faster columns thin out: period = base * (speed/mean)^k (default 1; 0 = constant)\n"
          "  --gap N        blank columns between rain columns (default 0)\n", out);
}

static bool parse_args(int argc, char **argv, Config *c) {
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
#define NUM(flag, field) if (!strcmp(a, flag)) { if (!v) return false; c->field = strtof(v, NULL); i++; continue; }
        NUM("--fall-speed", fallSpeed)
        NUM("--cycle-speed", cycleSpeed)
        NUM("--raindrop-length", raindropLength)
        NUM("--animation-speed", animationSpeed)
        NUM("--fps", fps)
        NUM("--trail", trail)
        NUM("--speed-min", speedMin)
        NUM("--speed-max", speedMax)
        NUM("--period-k", periodK)
#undef NUM
        if (!strcmp(a, "--gap")) { if (!v) return false; c->gap = atoi(v); if (c->gap < 0) return false; i++; continue; }
        if (!strcmp(a, "--style")) { if (!v) return false; c->style = v; i++; continue; }
        if (!strcmp(a, "--head")) { if (!v) return false; c->color = v; i++; continue; }
        if (!strcmp(a, "--screensaver")) { c->screensaver = true; continue; }
        if (!strcmp(a, "--flat")) { c->flat = true; continue; }
        if (!strcmp(a, "--help") || !strcmp(a, "-h")) { usage(stdout); exit(0); }
        fprintf(stderr, "matrix-rain-tty: unknown option %s\n", a);
        return false;
    }
    if (strcmp(c->style, "operator") && strcmp(c->style, "classic")) return false;
    if (strcmp(c->color, "white") && strcmp(c->color, "green")) return false;
    if (c->fps <= 0 || c->raindropLength < 0) return false;
    if (c->speedMin <= 0 || c->speedMax < c->speedMin || c->periodK < 0) return false;
    return true;
}

static double now_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

int main(int argc, char **argv) {
    Config cfg = { .style = "operator", .color = "white", .animationSpeed = 1, .fallSpeed = 0, .cycleSpeed = 0, .raindropLength = 0,
                   .fps = 30, .trail = -1, .speedMin = 0.5f, .speedMax = 1.5f, .periodK = 1.0f, .gap = -1, .screensaver = false, .flat = false };
    // Per-style defaults (after Rezmason's "classic" and "operator" versions), unless overridden.
    Config user = cfg;
    if (!parse_args(argc, argv, &user)) { usage(stderr); return 2; }
    cfg = user;
    bool operator_style = !strcmp(cfg.style, "operator");
    if (cfg.fallSpeed == 0) cfg.fallSpeed = operator_style ? 0.105f : 0.3f;
    if (cfg.cycleSpeed == 0) cfg.cycleSpeed = operator_style ? 0.005f : 0.03f;   // per 1/60 s, like the shaders
    if (cfg.gap < 0) cfg.gap = 0;
    if (cfg.trail < 0) cfg.trail = operator_style ? 20 : 0;   // rows lit behind the head; 0 = tied to the period like the shaders
    if (cfg.raindropLength == 0) cfg.raindropLength = operator_style ? 0.8f : 0.35f;   // terminals have few rows, so shorter drops than the GPU grid

    if (!isatty(STDOUT_FILENO)) { fputs("matrix-rain-tty: stdout is not a terminal\n", stderr); return 1; }

    Palette pal;
    make_palette(&pal, cfg.style, cfg.color, cfg.flat);

    // Raw-ish terminal: no echo, no line buffering, keep signals; alternate screen, no wrap, hidden cursor.
    if (tcgetattr(STDIN_FILENO, &saved_termios) == 0) {
        termios_saved = true;
        struct termios raw = saved_termios;
        raw.c_lflag &= ~(ECHO | ICANON);
        raw.c_cc[VMIN] = 0; raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    }
    atexit(restore_terminal);
    struct sigaction sa = { .sa_handler = on_signal };
    sigemptyset(&sa.sa_mask);
    for (int s = 0; s < 4; s++) sigaction((int[]){ SIGINT, SIGTERM, SIGHUP, SIGWINCH }[s], &sa, NULL);
    (void)!write(STDOUT_FILENO, "\x1b[?1049h\x1b[?25l\x1b[?7l\x1b[2J", 21);

    int cols = 0, rows = 0;
    Cell *cells = NULL;
    double *colOffset = NULL, *colSpeed = NULL, *colPeriod = NULL, *bright = NULL;   // per-column constants, per-frame brightness
    char *out = NULL;
    size_t outCap = 0;
    double last = now_seconds(), start = last;
    const double frameTime = 1.0 / cfg.fps;

    while (!got_quit) {
        if (!cells || got_resize) {
            got_resize = 0;
            get_size(&cols, &rows);
            free(cells);
            cells = calloc((size_t)cols * rows, sizeof *cells);
            for (int i = 0; i < cols * rows; i++) {
                int x = i % cols, y = i / cols;
                cells[i].glyph = (uint8_t)(NGLYPHS * hash13(x, y, 3.0));
                cells[i].age = (float)hash13(x, y, 7.0);
                cells[i].drawnLevel = 0xff;    // force a redraw
            }
            free(colOffset); free(colSpeed); free(colPeriod); free(bright);
            colOffset = malloc(sizeof *colOffset * cols);
            colSpeed = malloc(sizeof *colSpeed * cols);
            colPeriod = malloc(sizeof *colPeriod * cols);
            bright = malloc(sizeof *bright * cols * (rows + 1));
            for (int x = 0; x < cols; x++) {
                colOffset[x] = random_float(x + 0.5, 0.0) * 1000.0;
                // Column speed ~ Normal(mean, sigma) with ~95% of columns inside [speedMin, speedMax];
                // the tails are the fast and slow outliers, clamped at 3 sigma and never below a tenth of the mean.
                double mean = 0.5 * (cfg.speedMin + cfg.speedMax), sigma = 0.25 * (cfg.speedMax - cfg.speedMin);
                double u1 = fmax(1e-9, random_float(x + 0.5 + 0.1, 0.0)), u2 = random_float(x + 0.5, 0.3);
                double z = sqrt(-2.0 * log(u1)) * cos(2.0 * PI * u2);
                colSpeed[x] = fmin(mean + 3.0 * sigma, fmax(fmax(0.1 * mean, mean - 3.0 * sigma), mean + sigma * z));
                // Faster columns get a longer period (fewer drops): period = base * (speed / mean)^k.
                colPeriod[x] = cfg.raindropLength * pow(colSpeed[x] / mean, cfg.periodK);
            }
            outCap = (size_t)cols * rows * 24 + 64;
            out = realloc(out, outCap);
            (void)!write(STDOUT_FILENO, "\x1b[2J", 4);
        }

        double now = now_seconds();
        double dt = now - last;
        last = now;
        double t = (now - start) * cfg.animationSpeed;
        float ageStep = cfg.cycleSpeed * (float)(dt * 60.0) * cfg.animationSpeed;

        // Brightness of every cell (plus one row below the screen, for cursor detection),
        // one sawtooth evaluation per cell. Shader space: y grows upward.
        for (int x = 0; x < cols; x++) {
            if (cfg.gap && x % (cfg.gap + 1)) continue;
            double columnTime = colOffset[x] + t * cfg.fallSpeed * colSpeed[x];
            double *col = bright + (size_t)x * (rows + 1);
            for (int r = 0; r <= rows; r++) {
                double gy = (rows - 1 - r) + 0.5;
                col[r] = 1.0 - frac(wobble((gy * 0.01 + columnTime) / colPeriod[x]));
            }
        }

        size_t n = 0;
        int curLevel = -1, curRow = -1, curCol = -1;
        for (int r = 0; r < rows; r++) {
            for (int x = 0; x < cols; x++) {
                Cell *c = &cells[r * cols + x];
                int level = 0;
                if (cfg.gap == 0 || x % (cfg.gap + 1) == 0) {
                    const double *col = bright + (size_t)x * (rows + 1);
                    double b = col[r], below = col[r + 1];
                    bool cursor = b > below;
                    if (cursor) level = pal.levels - 1;
                    else if (cfg.trail > 0) {
                        // Trail measured in rows behind the head, independent of the period.
                        double d = (1.0 - b) * 100.0 * colPeriod[x];
                        if (d < cfg.trail) {
                            if (pal.brightnessOverride > 0) level = 1;
                            else { level = 1 + (int)((1.0 - d / cfg.trail) * (pal.levels - 2)); if (level > pal.levels - 2) level = pal.levels - 2; }
                        }
                    } else {
                        double base = b * pal.baseContrast + pal.baseBrightness;
                        if (base > pal.brightnessThreshold) {
                            if (pal.brightnessOverride > 0) level = 1;
                            else { level = 1 + (int)(base / 0.6 * (pal.levels - 2)); if (level > pal.levels - 2) level = pal.levels - 2; }
                        }
                    }
                }
                c->age += ageStep;
                if (c->age >= 1.0f) {
                    c->glyph = (uint8_t)(NGLYPHS * hash13(x, r, fmod(t * 7.0, 1024.0) + 0.5));
                    c->age -= 1.0f;
                }
                if (c->glyph >= NGLYPHS) c->glyph = NGLYPHS - 1;
                uint8_t glyph = level ? c->glyph : 0;
                c->level = (uint8_t)level;
                if (glyph == c->drawnGlyph && level == c->drawnLevel) continue;
                c->drawnGlyph = glyph; c->drawnLevel = (uint8_t)level;

                if (n + 64 > outCap) break;
                if (r != curRow || x != curCol) { n += (size_t)snprintf(out + n, outCap - n, "\x1b[%d;%dH", r + 1, x + 1); curRow = r; }
                if (level == 0) { out[n++] = ' '; }
                else {
                    if (level != curLevel) { size_t l = strlen(pal.sgr[level]); memcpy(out + n, pal.sgr[level], l); n += l; curLevel = level; }
                    size_t l = strlen(GLYPHS[glyph]); memcpy(out + n, GLYPHS[glyph], l); n += l;
                }
                curCol = x + 1;
            }
        }
        if (n) { size_t off = 0; while (off < n) { ssize_t w = write(STDOUT_FILENO, out + off, n - off); if (w <= 0) break; off += (size_t)w; } }

        // Wait for the next frame, leaving early on input.
        double remaining = frameTime - (now_seconds() - now);
        struct pollfd pfd = { .fd = STDIN_FILENO, .events = POLLIN };
        int rc = poll(&pfd, 1, remaining > 0 ? (int)(remaining * 1000) : 0);
        if (rc > 0 && (pfd.revents & POLLIN)) {
            char buf[64];
            ssize_t k = read(STDIN_FILENO, buf, sizeof buf);
            if (k > 0) {
                if (cfg.screensaver) break;
                for (ssize_t i = 0; i < k; i++) if (buf[i] == 'q' || buf[i] == 'Q' || buf[i] == 0x1b || buf[i] == 3) got_quit = 1;
            } else if (k == 0) break;   // terminal went away
        } else if (rc > 0 && (pfd.revents & (POLLHUP | POLLERR))) break;
    }

    free(cells);
    free(colOffset); free(colSpeed); free(colPeriod); free(bright);
    free(out);
    return 0;
}
