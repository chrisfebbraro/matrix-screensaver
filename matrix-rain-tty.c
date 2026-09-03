// matrix-rain-tty: the Matrix digital rain for a terminal.
//
// The code as it appears on the operators' screens in the films: half-width katakana and
// digits in plain text, a bright head leading each drop, and a stream that fades to black
// behind it. Every column has its own speed, drawn from a normal distribution, and spawns
// drops at intervals that grow with that speed, so fast columns carry fewer drops. Glyphs
// sit in a fixed grid and change in place now and then. Drawn with raw ANSI escapes and
// 24-bit colour; only cells that changed since the previous frame are redrawn.
//
//   matrix-rain-tty [--screensaver] [--head C] [--stream RRGGBB] [--flat] [--trail N] [--gap N]
//                   [--fall-speed F] [--period F] [--period-k F] [--speed-min F] [--speed-max F]
//                   [--speed-k F] [--cycle-speed F] [--animation-speed F] [--fps N]
//
// --screensaver exits on any key; otherwise q, Escape or Ctrl-C quits.

#define _POSIX_C_SOURCE 200809L
#define PI 3.14159265358979323846
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

// Half-width katakana (always one cell wide), digits and a few symbols, as on the film's screens.
static const char *GLYPHS[] = {
    "ｱ", "ｲ", "ｳ", "ｴ", "ｵ", "ｶ", "ｷ", "ｸ", "ｹ", "ｺ", "ｻ", "ｼ", "ｽ", "ｾ", "ｿ", "ﾀ", "ﾁ", "ﾂ", "ﾃ", "ﾄ",
    "ﾅ", "ﾆ", "ﾇ", "ﾈ", "ﾉ", "ﾊ", "ﾋ", "ﾌ", "ﾍ", "ﾎ", "ﾏ", "ﾐ", "ﾑ", "ﾒ", "ﾓ", "ﾔ", "ﾕ", "ﾖ", "ﾗ", "ﾘ",
    "ﾙ", "ﾚ", "ﾛ", "ﾜ", "ﾝ", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", ":", "=", "*", "+", "-",
    "<", ">", "|", "\"", ".",
};
enum { NGLYPHS = sizeof GLYPHS / sizeof GLYPHS[0] };

typedef struct {
    const char *head, *stream;     // head colour: white | green | RRGGBB; stream colour: RRGGBB or NULL for green
    float animationSpeed, fallSpeed, cycleSpeed, period, periodK, trail, fps;
    float speedMin, speedMax, speedK;
    int gap;
    bool screensaver, flat;
} Config;

// Colour levels: 0 = off (space), 1..levels-2 = stream from tail to head, levels-1 = head.
enum { MAX_LEVELS = 12 };
typedef struct { int levels; char sgr[MAX_LEVELS][32]; } Palette;

typedef struct { uint8_t glyph, drawnGlyph, drawnLevel; float age; } Cell;

enum { MAX_DROPS = 64 };
typedef struct {
    double speed;              // rows per second
    double spacing;            // typical rows between drop heads in this column
    double head[MAX_DROPS];    // head row of each live drop, oldest first; may be above the screen (negative)
    int count;
    double nextGap;            // rows between the newest head and the one that spawns after it
} Column;

// ---------- randomness (xorshift64, seeded per run) ----------
static uint64_t rng_state = 0x9E3779B97F4A7C15ULL;
static uint32_t rnd(void) {
    rng_state ^= rng_state << 13; rng_state ^= rng_state >> 7; rng_state ^= rng_state << 17;
    return (uint32_t)(rng_state >> 16);
}
static double rndf(void) { return (rnd() + 0.5) / 4294967296.0; }          // (0, 1)
static double rnd_normal(void) { return sqrt(-2.0 * log(rndf())) * cos(2.0 * PI * rndf()); }

// ---------- terminal state ----------
static struct termios saved_termios;
static bool termios_saved = false;
static volatile sig_atomic_t got_resize = 0, got_quit = 0;

static void restore_terminal(void) {
    const char restore[] = "\x1b[0m\x1b[?7h\x1b[?25h\x1b[?1049l";
    (void)!write(STDOUT_FILENO, restore, sizeof restore - 1);
    if (termios_saved) tcsetattr(STDIN_FILENO, TCSANOW, &saved_termios);
}
static void on_signal(int sig) { if (sig == SIGWINCH) got_resize = 1; else got_quit = 1; }

static void get_size(int *cols, int *rows) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) { *cols = ws.ws_col; *rows = ws.ws_row; }
    else { *cols = 80; *rows = 24; }
}

static double now_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

// ---------- colours ----------
static bool parse_hex(const char *hex, int rgb[3]) {
    if (hex[0] == '#') hex++;
    if (strlen(hex) != 6) return false;
    for (int i = 0; i < 3; i++) {
        char pair[3] = { hex[2 * i], hex[2 * i + 1], 0 }, *end;
        rgb[i] = (int)strtol(pair, &end, 16);
        if (*end) return false;
    }
    return true;
}
static void sgr_rgb(char *out, size_t n, const int rgb[3], double gain) {
    snprintf(out, n, "\x1b[38;2;%d;%d;%dm", (int)(rgb[0] * gain + 0.5), (int)(rgb[1] * gain + 0.5), (int)(rgb[2] * gain + 0.5));
}

static void make_palette(Palette *p, const Config *c) {
    static const int GREEN[3] = { 20, 184, 86 }, MINT[3] = { 168, 255, 208 }, WHITE[3] = { 255, 255, 255 };
    int streamRgb[3], headRgb[3];
    const int *stream = (c->stream && parse_hex(c->stream, streamRgb)) ? streamRgb : GREEN;

    memset(p, 0, sizeof *p);
    if (c->flat) {
        p->levels = 3;
        sgr_rgb(p->sgr[1], sizeof p->sgr[1], stream, 0.8);
    } else {
        // The stream fades from the full colour just behind the head to near-black at the tail;
        // a terminal has no alpha, but on black this reads as opacity.
        p->levels = 10;
        for (int i = 1; i < p->levels - 1; i++) {
            double at = (double)(i - 1) / (p->levels - 3);     // 0 = tail, 1 = just behind the head
            sgr_rgb(p->sgr[i], sizeof p->sgr[i], stream, 0.12 + at * 0.88);
        }
    }
    int head = p->levels - 1;
    const int *headColor = WHITE;
    if (!strcmp(c->head, "green")) headColor = MINT;
    else if (strcmp(c->head, "white") && parse_hex(c->head, headRgb)) headColor = headRgb;
    sgr_rgb(p->sgr[head], sizeof p->sgr[head], headColor, 1.0);
}

// ---------- columns and drops ----------
static double column_gap(const Column *col, const Config *c) {
    // Spacing between drop heads, jittered so columns don't tick like clocks; never closer
    // than the trail plus a blank row, so drops in a column never touch.
    double g = col->spacing * (0.7 + 0.6 * rndf());
    double minimum = c->trail + 2.0;
    return g < minimum ? minimum : g;
}

static void init_column(Column *col, int rows, const Config *c) {
    // Speed ~ Normal(mean, sigma), with ~95% of columns inside [speedMin, speedMax] (scaled by
    // speedK) and the tails as fast and slow outliers, clamped at 3 sigma and never below a
    // tenth of the mean, so no column ever stops.
    double lo = c->speedMin * c->speedK, hi = c->speedMax * c->speedK;
    double mean = 0.5 * (lo + hi), sigma = 0.25 * (hi - lo);
    double rel = mean + sigma * rnd_normal();
    rel = fmin(mean + 3.0 * sigma, fmax(fmax(0.1 * mean, mean - 3.0 * sigma), rel));
    col->speed = 100.0 * c->fallSpeed * rel;                              // rows per second
    // Faster columns get wider spacing (fewer drops): spacing = base * (speed / mean)^k.
    col->spacing = 100.0 * c->period * pow(rel / mean, c->periodK);

    // Start with drops already spread up the column so the screen is busy from the first frame.
    // Oldest (lowest) first.
    col->count = 0;
    double h = rows * rndf();
    while (h > -c->trail && col->count < MAX_DROPS) {
        col->head[col->count++] = h;
        h -= column_gap(col, c);
    }
    col->nextGap = column_gap(col, c);
}

static void advance_column(Column *col, double dt, int rows, const Config *c) {
    double step = col->speed * dt;
    for (int i = 0; i < col->count; i++) col->head[i] += step;

    // Retire drops whose whole trail has left the bottom of the screen (oldest first).
    int keep = 0;
    while (keep < col->count && col->head[keep] - c->trail > rows) keep++;
    if (keep) { memmove(col->head, col->head + keep, sizeof col->head[0] * (col->count - keep)); col->count -= keep; }

    // Spawn the next drop exactly nextGap rows above the newest one as soon as any of it
    // could be on screen, so spacing does not depend on the frame rate.
    while (col->count < MAX_DROPS) {
        double next = col->count ? col->head[col->count - 1] - col->nextGap : 0.0;
        if (next < -1.0) break;                   // still entirely above the screen: wait
        col->head[col->count++] = next;
        col->nextGap = column_gap(col, c);
    }
}

// ---------- command line ----------
static void usage(FILE *out) {
    fputs("usage: matrix-rain-tty [--screensaver] [--head C] [--stream RRGGBB] [--flat] [--trail N] [--gap N]\n"
          "                       [--fall-speed F] [--period F] [--period-k F] [--speed-min F] [--speed-max F]\n"
          "                       [--speed-k F] [--cycle-speed F] [--animation-speed F] [--fps N]\n"
          "  --screensaver   exit on any key; otherwise q, Escape or Ctrl-C quits\n"
          "  --head C        head colour: white (default), green, or hex RRGGBB\n"
          "  --stream C      stream colour as hex RRGGBB (default green); it fades to black behind the head\n"
          "  --flat          one colour for the whole stream instead of fading\n"
          "  --trail N       rows lit behind each head (default 20)\n"
          "  --gap N         blank columns between streams (default 0)\n"
          "  --fall-speed F  base speed: a column at relative speed 1 falls 100*F rows/s (default 0.105)\n"
          "  --period F      base spacing between drops in a column, 100*F rows at mean speed (default 0.8)\n"
          "  --period-k F    faster columns space drops out more: spacing = base * (speed/mean)^k (default 0.8; 0 = same for all)\n"
          "  --speed-min F   column speed band, relative to --fall-speed: about 95% of columns fall inside\n"
          "  --speed-max F   [min, max] (defaults 0.5, 1.5); the rest are faster or slower outliers, never stopped\n"
          "  --speed-k F     scales the whole band (default 0.9)\n"
          "  --cycle-speed F how often glyphs change, per 1/60 s (default 0.005)\n"
          "  --fps N         frames per second (default 30)\n", out);
}

static bool parse_args(int argc, char **argv, Config *c) {
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
#define NUM(flag, field) if (!strcmp(a, flag)) { if (!v) return false; c->field = strtof(v, NULL); i++; continue; }
        NUM("--fall-speed", fallSpeed)
        NUM("--cycle-speed", cycleSpeed)
        NUM("--period", period)
        NUM("--raindrop-length", period)      // older name
        NUM("--period-k", periodK)
        NUM("--speed-min", speedMin)
        NUM("--speed-max", speedMax)
        NUM("--speed-k", speedK)
        NUM("--trail", trail)
        NUM("--animation-speed", animationSpeed)
        NUM("--fps", fps)
#undef NUM
        if (!strcmp(a, "--gap")) { if (!v) return false; c->gap = atoi(v); i++; continue; }
        if (!strcmp(a, "--head")) { if (!v) return false; c->head = v; i++; continue; }
        if (!strcmp(a, "--stream")) { if (!v) return false; c->stream = v; i++; continue; }
        if (!strcmp(a, "--screensaver")) { c->screensaver = true; continue; }
        if (!strcmp(a, "--flat")) { c->flat = true; continue; }
        if (!strcmp(a, "--help") || !strcmp(a, "-h")) { usage(stdout); exit(0); }
        fprintf(stderr, "matrix-rain-tty: unknown option %s\n", a);
        return false;
    }
    int rgb[3];
    if (strcmp(c->head, "white") && strcmp(c->head, "green") && !parse_hex(c->head, rgb)) return false;
    if (c->stream && !parse_hex(c->stream, rgb)) return false;
    if (c->fps <= 0 || c->gap < 0 || c->trail < 1 || c->period <= 0 || c->fallSpeed <= 0) return false;
    if (c->speedMin <= 0 || c->speedMax < c->speedMin || c->speedK <= 0 || c->periodK < 0) return false;
    return true;
}

int main(int argc, char **argv) {
    Config cfg = {
        .head = "white", .stream = NULL, .animationSpeed = 1, .fallSpeed = 0.105f, .cycleSpeed = 0.005f,
        .period = 0.8f, .periodK = 0.8f, .trail = 20, .fps = 30,
        .speedMin = 0.5f, .speedMax = 1.5f, .speedK = 0.9f, .gap = 0, .screensaver = false, .flat = false,
    };
    if (!parse_args(argc, argv, &cfg)) { usage(stderr); return 2; }
    if (!isatty(STDOUT_FILENO)) { fputs("matrix-rain-tty: stdout is not a terminal\n", stderr); return 1; }

    rng_state ^= (uint64_t)time(NULL) * 0x2545F4914F6CDD1DULL ^ (uint64_t)getpid() << 32;
    for (int i = 0; i < 8; i++) rnd();

    Palette pal;
    make_palette(&pal, &cfg);

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
    Column *columns = NULL;
    int *level = NULL;             // per-column scratch: colour level of each row
    char *out = NULL;
    size_t outCap = 0;
    double last = now_seconds();
    const double frameTime = 1.0 / cfg.fps;

    while (!got_quit) {
        if (!cells || got_resize) {
            got_resize = 0;
            get_size(&cols, &rows);
            free(cells); free(columns); free(level); free(out);
            cells = calloc((size_t)cols * rows, sizeof *cells);
            columns = calloc((size_t)cols, sizeof *columns);
            level = calloc((size_t)rows, sizeof *level);
            outCap = (size_t)cols * rows * 24 + 64;
            out = malloc(outCap);
            for (int i = 0; i < cols * rows; i++) {
                cells[i].glyph = (uint8_t)(rnd() % NGLYPHS);
                cells[i].age = (float)rndf();
                cells[i].drawnLevel = 0xff;    // force a redraw
            }
            for (int x = 0; x < cols; x++) init_column(&columns[x], rows, &cfg);
            (void)!write(STDOUT_FILENO, "\x1b[2J", 4);
            last = now_seconds();
        }

        double now = now_seconds();
        double dt = (now - last) * cfg.animationSpeed;
        last = now;
        float ageStep = cfg.cycleSpeed * (float)(dt * 60.0);

        size_t n = 0;
        int curLevel = -1, curRow = -1, curCol = -1;
        for (int x = 0; x < cols; x++) {
            bool active = cfg.gap == 0 || x % (cfg.gap + 1) == 0;
            memset(level, 0, sizeof *level * rows);
            if (active) {
                Column *col = &columns[x];
                advance_column(col, dt, rows, &cfg);
                for (int d = 0; d < col->count; d++) {
                    int h = (int)floor(col->head[d]);
                    for (int i = 0; i < (int)cfg.trail; i++) {
                        int r = h - i;
                        if (r < 0 || r >= rows) continue;
                        if (i == 0) level[r] = pal.levels - 1;
                        else if (pal.levels == 3) level[r] = 1;
                        else { int l = 1 + (int)((1.0 - (double)i / cfg.trail) * (pal.levels - 2)); level[r] = l > pal.levels - 2 ? pal.levels - 2 : l; }
                    }
                }
            }
            for (int r = 0; r < rows; r++) {
                Cell *c = &cells[r * cols + x];
                c->age += ageStep;
                if (c->age >= 1.0f) { c->glyph = (uint8_t)(rnd() % NGLYPHS); c->age -= 1.0f; }
                int lv = level[r];
                uint8_t glyph = lv ? c->glyph : 0;
                if (glyph == c->drawnGlyph && lv == c->drawnLevel) continue;
                c->drawnGlyph = glyph; c->drawnLevel = (uint8_t)lv;

                if (n + 64 > outCap) break;
                if (r != curRow || x != curCol) { n += (size_t)snprintf(out + n, outCap - n, "\x1b[%d;%dH", r + 1, x + 1); curRow = r; }
                if (lv == 0) out[n++] = ' ';
                else {
                    if (lv != curLevel) { size_t l = strlen(pal.sgr[lv]); memcpy(out + n, pal.sgr[lv], l); n += l; curLevel = lv; }
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

    free(cells); free(columns); free(level); free(out);
    return 0;
}
