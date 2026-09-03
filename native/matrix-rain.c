// matrix-rain: the Matrix digital rain as a native SDL3 + OpenGL ES 3 program.
//
// Same pipeline as ../matrix.html (after Rezmason's "classic" mode): stationary film glyphs
// in a fixed grid, lit by wobbling sawtooth "raindrops" per column, a 5-level bloom
// pyramid, then a green palette tone-map with dither. The shaders and glyph atlas are
// embedded at compile time (C23 #embed), so the binary is self-contained.
//
//   matrix-rain [--screensaver] [--app-id ID] [--windowed] [--columns N] [--fall-speed F]
//               [--cycle-speed F] [--raindrop-length F] [--bloom-strength F] [--bloom-size F]
//               [--resolution F] [--fps N]
//
// --screensaver exits on any key, click, scroll, or mouse movement (after a short grace
// period); otherwise Escape or Q quits.

#include <SDL3/SDL.h>
#include <GLES3/gl3.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char VERT_SRC[] = {
#embed "shaders/quad.vert.glsl"
, 0 };
static const char RAIN_FRAG[] = {
#embed "shaders/rain.frag.glsl"
, 0 };
static const char HIGHPASS_FRAG[] = {
#embed "shaders/highpass.frag.glsl"
, 0 };
static const char BLUR_FRAG[] = {
#embed "shaders/blur.frag.glsl"
, 0 };
static const char COMBINE_FRAG[] = {
#embed "shaders/combine.frag.glsl"
, 0 };
static const char PALETTE_FRAG[] = {
#embed "shaders/palette.frag.glsl"
, 0 };
static const unsigned char ATLAS_BMP[] = {
#embed "matrixcode_msdf.bmp"
};

// ---------- configuration (defaults match matrix.html) ----------
typedef struct {
    float numColumns, animationSpeed, fallSpeed, cycleSpeed, raindropLength;
    float bloomStrength, bloomSize, highPassThreshold;
    float baseBrightness, baseContrast, cursorIntensity, ditherMagnitude, glyphEdgeCrop;
    float resolution, fps;
    bool screensaver, windowed;
    const char *appId;
} Config;

static const float PALETTE_HSL[][4] = { // h, s, l, at
    { 0.3f, 0.9f, 0.0f, 0.0f }, { 0.3f, 0.9f, 0.2f, 0.2f }, { 0.3f, 0.9f, 0.7f, 0.7f }, { 0.3f, 0.9f, 0.8f, 0.8f },
};
static const float CURSOR_HSL[3] = { 0.242f, 1.0f, 0.73f };
static const float GLYPH_SEQUENCE_LENGTH = 57.0f, GLYPH_PX_RANGE = 4.0f;
static const float GLYPH_GRID[2] = { 8.0f, 8.0f };
enum { PYRAMID = 5, PALETTE_SIZE = 2048 };

static void die(const char *what) {
    fprintf(stderr, "matrix-rain: %s: %s\n", what, SDL_GetError());
    exit(1);
}

static float hsl_channel(float h, float s, float l, float n) {
    float a = s * fminf(l, 1.0f - l);
    float k = fmodf(n + h * 12.0f, 12.0f);
    float m = fminf(fminf(k - 3.0f, 9.0f - k), 1.0f);
    return l - a * fmaxf(-1.0f, m);
}
static void hsl_to_rgb(const float hsl[3], float rgb[3]) {
    rgb[0] = hsl_channel(hsl[0], hsl[1], hsl[2], 0.0f);
    rgb[1] = hsl_channel(hsl[0], hsl[1], hsl[2], 8.0f);
    rgb[2] = hsl_channel(hsl[0], hsl[1], hsl[2], 4.0f);
}

// ---------- GL helpers ----------
static GLuint compile(GLenum type, const char *src, const char *label) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        fprintf(stderr, "matrix-rain: %s failed to compile:\n%s\n", label, log);
        exit(1);
    }
    return s;
}

static GLuint make_program(const char *frag, const char *label) {
    GLuint p = glCreateProgram();
    glAttachShader(p, compile(GL_VERTEX_SHADER, VERT_SRC, "vertex shader"));
    glAttachShader(p, compile(GL_FRAGMENT_SHADER, frag, label));
    glBindAttribLocation(p, 0, "aPosition");
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetProgramInfoLog(p, sizeof log, NULL, log);
        fprintf(stderr, "matrix-rain: %s failed to link:\n%s\n", label, log);
        exit(1);
    }
    return p;
}

static GLuint make_texture(void) {
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

typedef struct { GLuint tex, fbo; int w, h; } Target;

static void target_init(Target *t) {
    t->tex = make_texture();
    glGenFramebuffers(1, &t->fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, t->fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t->tex, 0);
    t->w = t->h = 0;
}

static void target_resize(Target *t, float w, float h) {
    t->w = (int)fmaxf(1.0f, floorf(w));
    t->h = (int)fmaxf(1.0f, floorf(h));
    glBindTexture(GL_TEXTURE_2D, t->tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, t->w, t->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
}

static void begin_pass(GLuint prog, const Target *t, int screenW, int screenH) {
    glBindFramebuffer(GL_FRAMEBUFFER, t ? t->fbo : 0);
    glViewport(0, 0, t ? t->w : screenW, t ? t->h : screenH);
    glUseProgram(prog);
}
static void u1f(GLuint p, const char *n, float v) { glUniform1f(glGetUniformLocation(p, n), v); }
static void u2f(GLuint p, const char *n, float x, float y) { glUniform2f(glGetUniformLocation(p, n), x, y); }
static void u3f(GLuint p, const char *n, const float v[3]) { glUniform3fv(glGetUniformLocation(p, n), 1, v); }
static void utex(GLuint p, const char *n, GLuint tex, int unit) {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform1i(glGetUniformLocation(p, n), unit);
}
static void draw_triangle(void) { glDrawArrays(GL_TRIANGLES, 0, 3); }

// ---------- resources ----------
static GLuint make_palette(void) {
    // Sorted stops, capped at both ends, linearly interpolated into a 2048x1 gradient.
    int n = (int)(sizeof PALETTE_HSL / sizeof PALETTE_HSL[0]);
    float rgb[8][3];
    int idx[8];
    int count = 0;
    idx[count] = 0;
    hsl_to_rgb(PALETTE_HSL[0], rgb[count]);
    count++;
    for (int i = 0; i < n; i++) {
        hsl_to_rgb(PALETTE_HSL[i], rgb[count]);
        idx[count] = (int)floorf(fminf(1.0f, fmaxf(0.0f, PALETTE_HSL[i][3])) * (PALETTE_SIZE - 1));
        count++;
    }
    idx[count] = PALETTE_SIZE - 1;
    memcpy(rgb[count], rgb[count - 1], sizeof rgb[0]);
    count++;

    static unsigned char data[PALETTE_SIZE * 4];
    for (int k = 0; k + 1 < count; k++) {
        int span = idx[k + 1] - idx[k];
        if (span < 1) span = 1;
        for (int i = idx[k]; i <= idx[k + 1]; i++) {
            float r = (float)(i - idx[k]) / (float)span;
            for (int c = 0; c < 3; c++)
                data[i * 4 + c] = (unsigned char)floorf((rgb[k][c] * (1.0f - r) + rgb[k + 1][c] * r) * 255.0f);
            data[i * 4 + 3] = 255;
        }
    }
    GLuint t = make_texture();
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, PALETTE_SIZE, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    return t;
}

static GLuint make_atlas(int *w, int *h) {
    SDL_Surface *bmp = SDL_LoadBMP_IO(SDL_IOFromConstMem(ATLAS_BMP, sizeof ATLAS_BMP), true);
    if (!bmp) die("loading glyph atlas");
    SDL_Surface *rgb = SDL_ConvertSurface(bmp, SDL_PIXELFORMAT_RGB24);
    SDL_DestroySurface(bmp);
    if (!rgb) die("converting glyph atlas");
    // Match WebGL's UNPACK_FLIP_Y: the image's top row must sit at texture v = 1.
    if (!SDL_FlipSurface(rgb, SDL_FLIP_VERTICAL)) die("flipping glyph atlas");
    GLuint t = make_texture();
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, rgb->pitch / 3);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, rgb->w, rgb->h, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb->pixels);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    *w = rgb->w;
    *h = rgb->h;
    SDL_DestroySurface(rgb);
    return t;
}

// ---------- command line ----------
static void usage(FILE *out) {
    fputs("usage: matrix-rain [--screensaver] [--app-id ID] [--windowed] [--columns N] [--fall-speed F]\n"
          "                   [--cycle-speed F] [--raindrop-length F] [--bloom-strength F] [--bloom-size F]\n"
          "                   [--resolution F] [--fps N] [--animation-speed F]\n"
          "  --screensaver   exit on any input (after 1.5 s); otherwise Escape or Q quits\n"
          "  --app-id ID     Wayland app id / window class (e.g. org.omarchy.screensaver)\n"
          "  --windowed      run in a resizable window instead of fullscreen\n"
          "  --fps N         frame cap, default 60; 0 = follow the display refresh (vsync)\n",
          out);
}

static bool parse_args(int argc, char **argv, Config *c) {
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *v = (i + 1 < argc) ? argv[i + 1] : NULL;
#define NUM(flag, field) if (!strcmp(a, flag)) { if (!v) return false; c->field = strtof(v, NULL); i++; continue; }
        NUM("--columns", numColumns)
        NUM("--fall-speed", fallSpeed)
        NUM("--cycle-speed", cycleSpeed)
        NUM("--raindrop-length", raindropLength)
        NUM("--bloom-strength", bloomStrength)
        NUM("--bloom-size", bloomSize)
        NUM("--resolution", resolution)
        NUM("--fps", fps)
        NUM("--animation-speed", animationSpeed)
#undef NUM
        if (!strcmp(a, "--screensaver")) { c->screensaver = true; continue; }
        if (!strcmp(a, "--windowed")) { c->windowed = true; continue; }
        if (!strcmp(a, "--app-id")) { if (!v) return false; c->appId = v; i++; continue; }
        if (!strcmp(a, "--help") || !strcmp(a, "-h")) { usage(stdout); exit(0); }
        fprintf(stderr, "matrix-rain: unknown option %s\n", a);
        return false;
    }
    if (c->numColumns < 1.0f || c->resolution <= 0.0f || c->fps < 0.0f) return false;
    return true;
}

int main(int argc, char **argv) {
    Config cfg = {
        .numColumns = 160, .animationSpeed = 1, .fallSpeed = 0.3f, .cycleSpeed = 0.03f, .raindropLength = 0.75f,
        .bloomStrength = 0.7f, .bloomSize = 0.4f, .highPassThreshold = 0.1f,
        .baseBrightness = -0.5f, .baseContrast = 1.1f, .cursorIntensity = 2, .ditherMagnitude = 0.05f, .glyphEdgeCrop = 0,
        .resolution = 1, .fps = 60, .screensaver = false, .windowed = false, .appId = NULL,
    };
    if (!parse_args(argc, argv, &cfg)) { usage(stderr); return 2; }

    if (cfg.appId) SDL_SetHint(SDL_HINT_APP_ID, cfg.appId);
    SDL_SetAppMetadata("Matrix Digital Rain", "1.0", cfg.appId ? cfg.appId : "io.github.chrisfebbraro.matrix-rain");
    if (!SDL_Init(SDL_INIT_VIDEO)) die("SDL_Init");

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);

    SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    flags |= cfg.windowed ? SDL_WINDOW_RESIZABLE : SDL_WINDOW_FULLSCREEN;
    SDL_Window *window = SDL_CreateWindow("Matrix Digital Rain", 1280, 720, flags);
    if (!window) die("SDL_CreateWindow");
    SDL_GLContext ctx = SDL_GL_CreateContext(window);
    if (!ctx || !SDL_GL_MakeCurrent(window, ctx)) die("creating OpenGL ES context");
    // With a frame cap we present on our own schedule (Wayland never tears); fps 0 means vsync.
    SDL_GL_SetSwapInterval(cfg.fps > 0 ? 0 : 1);
    if (!cfg.windowed) SDL_HideCursor();

    // Fullscreen triangle
    GLuint vao, vbo;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    static const float tri[] = { -4, -4, 4, -4, 0, 4 };
    glBufferData(GL_ARRAY_BUFFER, sizeof tri, tri, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    GLuint rainProg = make_program(RAIN_FRAG, "rain shader");
    GLuint highPassProg = make_program(HIGHPASS_FRAG, "high-pass shader");
    GLuint blurProg = make_program(BLUR_FRAG, "blur shader");
    GLuint combineProg = make_program(COMBINE_FRAG, "combine shader");
    GLuint paletteProg = make_program(PALETTE_FRAG, "palette shader");

    GLuint paletteTex = make_palette();
    int atlasW, atlasH;
    GLuint atlasTex = make_atlas(&atlasW, &atlasH);
    float cursorRgb[3];
    hsl_to_rgb(CURSOR_HSL, cursorRgb);

    Target primary, bloom, highPass[PYRAMID], hBlur[PYRAMID], vBlur[PYRAMID];
    target_init(&primary);
    target_init(&bloom);
    for (int i = 0; i < PYRAMID; i++) { target_init(&highPass[i]); target_init(&hBlur[i]); target_init(&vBlur[i]); }

    int screenW = 0, screenH = 0, sizedW = -1, sizedH = -1;
    bool bloomOn = cfg.bloomStrength > 0 && cfg.bloomSize > 0;

    const Uint64 start = SDL_GetTicksNS();
    const Uint64 frameNs = cfg.fps > 0 ? (Uint64)(1e9 / cfg.fps) : 0;
    Uint64 nextFrame = start;
    bool armed = false, haveMouse = false;
    float mouseX = 0, mouseY = 0;
    bool running = true;

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_EVENT_QUIT: running = false; break;
            case SDL_EVENT_KEY_DOWN:
                if (cfg.screensaver ? armed : (e.key.key == SDLK_ESCAPE || e.key.key == SDLK_Q)) running = false;
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN: case SDL_EVENT_MOUSE_WHEEL: case SDL_EVENT_FINGER_DOWN:
                if (cfg.screensaver && armed) running = false;
                break;
            case SDL_EVENT_MOUSE_MOTION:
                if (!cfg.screensaver) break;
                if (!haveMouse) { mouseX = e.motion.x; mouseY = e.motion.y; haveMouse = true; break; }
                if (armed && hypotf(e.motion.x - mouseX, e.motion.y - mouseY) > 24.0f) running = false;
                break;
            default: break;
            }
        }
        if (!running) break;

        Uint64 now = SDL_GetTicksNS();
        if (!armed && now - start > 1500000000ULL) armed = true;
        if (frameNs) {
            if (now < nextFrame) { SDL_DelayPrecise(nextFrame - now); now = SDL_GetTicksNS(); }
            nextFrame += frameNs;
            if (nextFrame < now) nextFrame = now; // don't try to catch up after a stall
        }

        SDL_GetWindowSizeInPixels(window, &screenW, &screenH);
        if (screenW != sizedW || screenH != sizedH) {
            sizedW = screenW; sizedH = screenH;
            float w = ceilf(screenW * cfg.resolution), h = ceilf(screenH * cfg.resolution);
            target_resize(&primary, w, h);
            target_resize(&bloom, w, h);
            for (int i = 0; i < PYRAMID; i++) {
                float s = cfg.bloomSize / (float)(1 << i);
                target_resize(&highPass[i], w * s, h * s);
                target_resize(&hBlur[i], w * s, h * s);
                target_resize(&vBlur[i], w * s, h * s);
            }
        }

        float time = (float)((double)(now - start) / 1e9) * cfg.animationSpeed;

        // 1. Rain: glyph brightness (R) and cursor brightness (G)
        begin_pass(rainProg, &primary, screenW, screenH);
        u2f(rainProg, "resolution", (float)primary.w, (float)primary.h);
        u1f(rainProg, "time", time);
        u1f(rainProg, "numColumns", cfg.numColumns);
        u1f(rainProg, "fallSpeed", cfg.fallSpeed);
        u1f(rainProg, "raindropLength", cfg.raindropLength);
        u1f(rainProg, "cycleSpeed", cfg.cycleSpeed);
        u1f(rainProg, "baseContrast", cfg.baseContrast);
        u1f(rainProg, "baseBrightness", cfg.baseBrightness);
        u1f(rainProg, "glyphSequenceLength", GLYPH_SEQUENCE_LENGTH);
        u1f(rainProg, "glyphEdgeCrop", cfg.glyphEdgeCrop);
        u1f(rainProg, "msdfPxRange", GLYPH_PX_RANGE);
        u2f(rainProg, "glyphTextureGridSize", GLYPH_GRID[0], GLYPH_GRID[1]);
        u2f(rainProg, "glyphMSDFSize", (float)atlasW, (float)atlasH);
        utex(rainProg, "glyphMSDF", atlasTex, 0);
        draw_triangle();

        // 2. Bloom: high-pass + separable blur down a pyramid, then summed
        if (bloomOn) {
            for (int i = 0; i < PYRAMID; i++) {
                begin_pass(highPassProg, &highPass[i], screenW, screenH);
                utex(highPassProg, "tex", i == 0 ? primary.tex : highPass[i - 1].tex, 0);
                u1f(highPassProg, "highPassThreshold", cfg.highPassThreshold);
                draw_triangle();

                float tx = 1.0f / (float)highPass[i].w, ty = 1.0f / (float)highPass[i].h;
                begin_pass(blurProg, &hBlur[i], screenW, screenH);
                utex(blurProg, "tex", highPass[i].tex, 0);
                u2f(blurProg, "texel", tx, ty);
                u2f(blurProg, "direction", 1, 0);
                draw_triangle();

                begin_pass(blurProg, &vBlur[i], screenW, screenH);
                utex(blurProg, "tex", hBlur[i].tex, 0);
                u2f(blurProg, "texel", tx, ty);
                u2f(blurProg, "direction", 0, 1);
                draw_triangle();
            }
            begin_pass(combineProg, &bloom, screenW, screenH);
            static const char *pyr[PYRAMID] = { "pyr_0", "pyr_1", "pyr_2", "pyr_3", "pyr_4" };
            for (int i = 0; i < PYRAMID; i++) utex(combineProg, pyr[i], vBlur[i].tex, i);
            u1f(combineProg, "bloomStrength", cfg.bloomStrength);
            draw_triangle();
        }

        // 3. Palette: brightness + bloom mapped to green, cursors added, dithered; straight to screen
        begin_pass(paletteProg, NULL, screenW, screenH);
        utex(paletteProg, "tex", primary.tex, 0);
        utex(paletteProg, "bloomTex", bloom.tex, 1);
        utex(paletteProg, "paletteTex", paletteTex, 2);
        u1f(paletteProg, "time", time);
        u1f(paletteProg, "ditherMagnitude", cfg.ditherMagnitude);
        u1f(paletteProg, "cursorIntensity", cfg.cursorIntensity);
        u3f(paletteProg, "cursorColor", cursorRgb);
        draw_triangle();

        SDL_GL_SwapWindow(window);
    }

    SDL_GL_DestroyContext(ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
