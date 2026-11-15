#include "ui.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <3ds.h>
#include <citro2d.h>

#define SGT_SCREEN_W 400.0f
#define SGT_BOTTOM_W 320.0f

#define C_RGBA(r, g, b, a) C2D_Color32((r), (g), (b), (a))

static C3D_RenderTarget *g_top;
static C3D_RenderTarget *g_bot;
static C2D_Font          g_font;
static C2D_TextBuf       g_buf;
static u64               g_boot_ms;
static int               g_last_mode = -1;
static u64               g_mode_ms;
static float             g_px[18], g_py[18], g_ps[18];

#define COL_BG0   C_RGBA(7, 10, 24, 255)
#define COL_BG1   C_RGBA(16, 20, 48, 255)
#define COL_PANEL C_RGBA(18, 26, 52, 235)
#define COL_LINE  C_RGBA(38, 54, 96, 255)
#define COL_TEXT  C_RGBA(232, 240, 255, 255)
#define COL_DIM   C_RGBA(140, 158, 196, 255)
#define COL_CYAN  C_RGBA(34, 224, 255, 255)
#define COL_PINK  C_RGBA(255, 63, 164, 255)
#define COL_LIME  C_RGBA(124, 255, 107, 255)
#define COL_AMBER C_RGBA(255, 197, 61, 255)
#define COL_RED   C_RGBA(255, 77, 109, 255)
#define COL_DARK  C_RGBA(6, 9, 20, 255)

void ui_init(void)
{
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();

    g_top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    g_bot = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    g_font = C2D_FontLoadSystem(CFG_REGION_EUR);
    g_buf = C2D_TextBufNew(16384);
    g_boot_ms = osGetTime();

    for (int i = 0; i < 18; i++) {
        g_px[i] = fmodf(i * 0.618034f + 0.11f, 1.0f);
        g_py[i] = fmodf(i * 0.377000f + 0.23f, 1.0f);
        g_ps[i] = 0.010f + (float)(i % 6) * 0.0035f;
    }
}

void ui_exit(void)
{
    if (g_buf) C2D_TextBufDelete(g_buf);
    if (g_font) C2D_FontFree(g_font);
    C2D_Fini();
    C3D_Fini();
    gfxExit();
}

static u32 lerp_color(u32 a, u32 b, float t)
{
    int ar = (a >> 0) & 0xFF, ag = (a >> 8) & 0xFF, ab = (a >> 16) & 0xFF, aa = (a >> 24) & 0xFF;
    int br = (b >> 0) & 0xFF, bg = (b >> 8) & 0xFF, bb = (b >> 16) & 0xFF, ba = (b >> 24) & 0xFF;

    return C2D_Color32((u8)(ar + (br - ar) * t),
                       (u8)(ag + (bg - ag) * t),
                       (u8)(ab + (bb - ab) * t),
                       (u8)(aa + (ba - aa) * t));
}

/* Esquinas escalonadas en vez de circulos: mismos pixeles bonitos pero sin
 * cambiar el modo de dibujado de la GPU en cada panel (importante en 2DS). */
static void round_rect(float x, float y, float w, float h, float r, u32 color)
{
    int i, ri;

    if (r <= 0 || w <= 0 || h <= 0) { C2D_DrawRectSolid(x, y, 0, w, h, color); return; }
    if (r > h / 2) r = h / 2;
    if (r > w / 2) r = w / 2;
    ri = (int)r;
    for (i = 0; i < ri; i++) {
        float inset = r - i;

        C2D_DrawRectSolid(x + inset, y + i, 0, w - 2 * inset, 1, color);
        C2D_DrawRectSolid(x + inset, y + h - 1 - i, 0, w - 2 * inset, 1, color);
    }
    C2D_DrawRectSolid(x, y + r, 0, w, h - 2 * r, color);
}

static void panel(float x, float y, float w, float h, float r, u32 fill, u32 border)
{
    if (border) round_rect(x - 1, y - 1, w + 2, h + 2, r + 1, border);
    round_rect(x, y, w, h, r, fill);
}

static void txt(float x, float y, float scale, u32 color, const char *fmt, ...)
{
    char buf[256];
    C2D_Text t;
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (!buf[0]) return;

    C2D_TextFontParse(&t, g_font, g_buf, buf);
    C2D_TextOptimize(&t);
    C2D_DrawText(&t, C2D_WithColor, x, y, 0.5f, scale, scale, color);
}

static float text_w(const char *s, float scale)
{
    C2D_Text t;
    float w = 0, h = 0;

    if (!s || !*s) return 0;
    C2D_TextFontParse(&t, g_font, g_buf, s);
    C2D_TextOptimize(&t);
    C2D_TextGetDimensions(&t, scale, scale, &w, &h);
    return w;
}

static void txt_right(float right, float y, float scale, u32 color, const char *s)
{
    txt(right - text_w(s, scale), y, scale, color, "%s", s);
}

static void txt_center(float cx, float y, float scale, u32 color, const char *s)
{
    txt(cx - text_w(s, scale) / 2, y, scale, color, "%s", s);
}

static void ellipsize(char *out, size_t outsz, const char *in, int maxchars)
{
    int n;

    if (!in) { out[0] = 0; return; }
    n = (int)strlen(in);
    if (maxchars < 1) maxchars = 1;
    if (n <= maxchars) {
        snprintf(out, outsz, "%s", in);
        return;
    }
    if (maxchars <= 3) {
        snprintf(out, outsz, "%.*s", maxchars, in);
        return;
    }
    snprintf(out, outsz, "%.*s...", maxchars - 3, in);
}

static void fmt_size(u64 bytes, char *out, size_t outsz)
{
    if (bytes >= 1024ULL * 1024 * 1024)
        snprintf(out, outsz, "%.1f GB", (double)bytes / 1073741824.0);
    else if (bytes >= 1024ULL * 1024)
        snprintf(out, outsz, "%.1f MB", (double)bytes / 1048576.0);
    else if (bytes >= 1024)
        snprintf(out, outsz, "%llu KB", (unsigned long long)(bytes / 1024));
    else
        snprintf(out, outsz, "%llu B", (unsigned long long)bytes);
}

static void fmt_eta(u64 secs, char *out, size_t outsz)
{
    if (secs >= 3600)
        snprintf(out, outsz, "%lluh %02llum", (unsigned long long)(secs / 3600),
                 (unsigned long long)((secs % 3600) / 60));
    else
        snprintf(out, outsz, "%02llu:%02llu", (unsigned long long)(secs / 60),
                 (unsigned long long)(secs % 60));
}

static float now_s(void)
{
    return (float)(osGetTime() % 3600000ULL) / 1000.0f;
}

static void background(float w, float h)
{
    int i;
    const int bands = 20;
    float t = now_s();

    for (i = 0; i < bands; i++) {
        float k = (float)i / (float)(bands - 1);
        C2D_DrawRectSolid(0, h * i / bands, 0, w, h / bands + 1, lerp_color(COL_BG0, COL_BG1, k));
    }

    /* Auroras que se mueven lento, sin texturas. */
    for (i = 0; i < 3; i++) {
        float cx = w * (0.5f + 0.44f * sinf(t * 0.11f + i * 2.1f));
        float cy = h * (0.5f + 0.42f * sinf(t * 0.07f + i * 1.7f));
        float r = 52.0f + 22.0f * sinf(t * 0.23f + i);
        u32 c = i == 0 ? C_RGBA(34, 224, 255, 11) :
                i == 1 ? C_RGBA(255, 63, 164, 8) : C_RGBA(124, 255, 107, 5);

        C2D_DrawCircleSolid(cx, cy, 0, r, c);
    }

    /* Haz de luz diagonal que barre la pantalla. */
    {
        float bx = fmodf(t * 42.0f, w + 160.0f) - 80.0f;
        u32 c0 = C_RGBA(120, 235, 255, 13);
        u32 c1 = C_RGBA(120, 235, 255, 2);

        C2D_DrawTriangle(bx, 0, c0, bx + 54, 0, c1, bx + 28, h, c0, 0);
        C2D_DrawTriangle(bx + 54, 0, c1, bx + 86, h, c1, bx + 28, h, c0, 0);
    }

    /* Particulas flotantes con parpadeo. */
    for (i = 0; i < 18; i++) {
        float x = g_px[i] * w;
        float y = fmodf(g_py[i] - t * g_ps[i], 1.0f);
        float tw = 0.5f + 0.5f * sinf(t * 2.7f + i * 1.31f);
        u32 c = C_RGBA(190, 235, 255, (int)(26.0f + 52.0f * tw));

        if (y < 0) y += 1.0f;
        C2D_DrawCircleSolid(x, y * h, 0, 1.1f + 1.0f * tw, c);
    }

    C2D_DrawRectSolid(0, 0, 0, w, 1, C_RGBA(34, 224, 255, 30));
    C2D_DrawRectSolid(0, h - 1, 0, w, 1, C_RGBA(255, 63, 164, 26));
}

static void bar(float x, float y, float w, float h, float p, u32 fill, u32 track)
{
    if (p < 0) p = 0;
    if (p > 1) p = 1;
    round_rect(x, y, w, h, h / 2, track);
    if (p > 0.005f) {
        float fw = w * p;
        float gx, gw;

        if (fw < h) fw = h;
        round_rect(x, y, fw, h, h / 2, fill);

        /* Brillo que recorre la barra. */
        gx = x + fmodf(now_s() * 44.0f, fw + 24.0f) - 12.0f;
        if (gx < x) gx = x;
        gw = fminf(12.0f, x + fw - gx);
        if (gw > 0)
            C2D_DrawRectSolid(gx, y + 1, 0, gw, h > 3 ? h - 2 : h, C_RGBA(255, 255, 255, 30));
    }
}

static void checkbox(float x, float y, int on, u32 accent)
{
    if (on) {
        float t = now_s();
        float r = 7.5f + 1.2f * sinf(t * 5.0f + x * 0.1f);

        C2D_DrawCircleSolid(x + 6, y + 6, 0, r, C_RGBA(124, 255, 107, 40));
        C2D_DrawCircleSolid(x + 6, y + 6, 0, 6, accent);
        C2D_DrawCircleSolid(x + 6, y + 6, 0, 2.4f, COL_DARK);
    } else {
        C2D_DrawCircleSolid(x + 6, y + 6, 0, 6, COL_LINE);
        C2D_DrawCircleSolid(x + 6, y + 6, 0, 3.5f, C_RGBA(12, 18, 38, 255));
    }
}

static void icon_folder(float x, float y, u32 color)
{
    round_rect(x, y + 2, 9, 6, 2, color);
    round_rect(x, y + 5, 16, 10, 3, color);
}

static void icon_file(float x, float y, u32 color)
{
    round_rect(x + 2, y, 12, 16, 2, color);
    C2D_DrawRectSolid(x + 4.5f, y + 5, 0, 7, 1.4f, C_RGBA(12, 18, 38, 200));
    C2D_DrawRectSolid(x + 4.5f, y + 8, 0, 7, 1.4f, C_RGBA(12, 18, 38, 200));
    C2D_DrawRectSolid(x + 4.5f, y + 11, 0, 5, 1.4f, C_RGBA(12, 18, 38, 200));
}

static void key_pill(float x, float y, const char *key, const char *label, u32 accent)
{
    float kw = text_w(key, 0.34f) + 10;

    round_rect(x, y, kw, 16, 5, accent);
    txt_center(x + kw / 2, y + 2, 0.34f, COL_DARK, key);
    txt(x + kw + 4, y + 2, 0.34f, COL_DIM, "%s", label);
}

static void header(const SgtApp *app)
{
    char bat[32];
    float t = now_s();
    float glow = 0.5f + 0.5f * sinf(t * 2.6f);

    round_rect(10.0f - glow * 3.0f, 7.0f - glow * 3.0f, 34.0f + glow * 6.0f, 32.0f + glow * 6.0f,
               11, C_RGBA(34, 224, 255, (int)(16.0f + 24.0f * glow)));
    round_rect(12, 9, 30, 28, 8, COL_CYAN);
    txt_center(27, 13, 0.46f, COL_DARK, "SD");
    txt(50, 6, 0.62f, COL_TEXT, "SD GAME TRANSFER");
    txt(50, 26, 0.34f, COL_DIM, "by frisk  -  2DS / 3DS");

    if (app->battery >= 0 && app->battery <= 100) {
        snprintf(bat, sizeof(bat), "%d%%", app->battery);
        round_rect(330, 12, 56, 18, 6, C_RGBA(20, 30, 58, 220));
        C2D_DrawRectSolid(338, 18, 0, 26, 7, COL_LINE);
        C2D_DrawRectSolid(340, 20, 0, 22.0f * app->battery / 100.0f, 4,
                          app->battery < 20 ? COL_RED : COL_LIME);
        txt(368, 14, 0.32f, COL_DIM, "%s", bat);
    }
    txt_right(388, 32, 0.32f,
              app->action == SGT_ACTION_MOVE ? COL_AMBER : COL_CYAN,
              app->mode == SGT_MODE_SAVES ? "JUEGOS" : (app->action == SGT_ACTION_MOVE ? "MOVER" : "COPIAR"));

    C2D_DrawRectSolid(12, 40, 0, 376, 1, C_RGBA(34, 224, 255, 90));

    /* Chispa que recorre la linea del encabezado. */
    {
        float mx = 12.0f + fmodf(t * 62.0f, 340.0f);

        C2D_DrawRectSolid(mx, 38.5f, 0, 36, 3, C_RGBA(34, 224, 255, 150));
        C2D_DrawRectSolid(mx + 8, 39.0f, 0, 20, 2, C_RGBA(255, 255, 255, 90));
    }
}

static void storage_card(float x, float y, float w, float h,
                         const char *label, u64 used, u64 total, u32 accent,
                         const char *note)
{
    char a[24], b[24], freebuf[24], note2[56];
    u64 freeb = total > used ? total - used : 0;
    float p = (total > 0) ? (float)((double)used / (double)total) : 0;

    panel(x, y, w, h, 8, COL_PANEL, COL_LINE);
    txt(x + 10, y + 4, 0.33f, accent, "%s", label);
    fmt_size(used, a, sizeof(a));
    fmt_size(total, b, sizeof(b));
    txt(x + 10, y + 16, 0.4f, COL_TEXT, "%s / %s", a, b);
    bar(x + 10, y + 30, w - 20, 5, p, accent, C_RGBA(30, 42, 76, 255));
    fmt_size(freeb, freebuf, sizeof(freebuf));
    snprintf(note2, sizeof(note2), "%s %s", note, freebuf);
    txt(x + 10, y + 34, 0.3f, COL_DIM, "%s", note2);
}

static void files_view(const SgtApp *app)
{
    char path[80], count[32];
    int i, rows = 4;
    float row_h = 21, y0 = 114;
    float pulse = 0.5f + 0.5f * sinf((float)(osGetTime() % 2000) / 2000.0f * 6.28318f);

    header(app);
    storage_card(12, 45, 184, 44, "TARJETA SD", app->sd_total - app->sd_free, app->sd_total, COL_CYAN, "libre");
    storage_card(204, 45, 184, 44, "MEMORIA 3DS", app->nand_total - app->nand_free, app->nand_total, COL_PINK,
                 "sistema, solo lectura");

    panel(8, 94, 384, 112, 8, COL_PANEL, COL_LINE);
    ellipsize(path, sizeof(path), app->cwd, 44);
    txt(16, 98, 0.31f, COL_DIM, "%s", path);
    snprintf(count, sizeof(count), "%d elementos", app->nentries);
    txt_right(384, 98, 0.31f, COL_DIM, count);

    if (app->nentries == 0) {
        txt_center(200, 150, 0.42f, COL_DIM, "Carpeta vacia");
    }
    for (i = 0; i < rows; i++) {
        int idx = app->top + i;
        float y = y0 + i * row_h;
        SgtEntry *e;
        char nm[64], sz[24];

        if (idx >= app->nentries) break;
        e = (SgtEntry *)&app->entries[idx];

        if (idx == app->cursor) {
            int a = 20 + (int)(12.0f * pulse);

            round_rect(11, y - 1, 378, row_h - 2, 6, C_RGBA(34, 224, 255, a));
            C2D_DrawRectSolid(11, y - 1, 0, 3, row_h - 2,
                              lerp_color(COL_CYAN, COL_PINK, pulse));
        }
        if (e->is_dir) icon_folder(18, y + 2, COL_CYAN);
        else icon_file(18, y + 1, C_RGBA(150, 170, 210, 255));

        ellipsize(nm, sizeof(nm), e->name, e->is_dir ? 30 : 27);
        txt(40, y + 2, 0.38f, idx == app->cursor ? COL_TEXT : C_RGBA(200, 212, 236, 255), "%s", nm);

        if (!e->is_dir) {
            fmt_size(e->size, sz, sizeof(sz));
            txt_right(360, y + 3, 0.31f, COL_DIM, sz);
        }
        checkbox(372, y + 5, e->selected, COL_LIME);
    }

    key_pill(12, 216, "A", "marcar", COL_CYAN);
    key_pill(78, 216, "Y", "cola", COL_LIME);
    key_pill(136, 216, "X", app->action == SGT_ACTION_MOVE ? "mover" : "copiar", COL_AMBER);
    key_pill(206, 216, "SEL", "juegos", COL_PINK);
    key_pill(286, 216, "B", "subir", COL_DIM);
    key_pill(348, 216, "STA", "salir", COL_DIM);
}

static void saves_view(const SgtApp *app)
{
    char title[64];
    int i, rows = 4;
    float row_h = 21, y0 = 114;
    float pulse = 0.5f + 0.5f * sinf((float)(osGetTime() % 2000) / 2000.0f * 6.28318f);

    header(app);
    storage_card(12, 45, 184, 44, "TARJETA SD", app->sd_total - app->sd_free, app->sd_total, COL_CYAN, "libre");
    storage_card(204, 45, 184, 44, "MEMORIA 3DS", app->nand_total - app->nand_free, app->nand_total, COL_PINK,
                 "sistema, solo lectura");

    panel(8, 94, 384, 112, 8, COL_PANEL, COL_LINE);
    snprintf(title, sizeof(title), "JUEGOS  -  %d detectados en el HOME", app->ntitles);
    txt(16, 98, 0.31f, COL_DIM, "%s", title);

    if (app->ntitles == 0) {
        txt_center(200, 150, 0.42f, COL_DIM, "No hay juegos visibles");
    }
    for (i = 0; i < rows; i++) {
        int idx = app->ttop + i;
        float y = y0 + i * row_h;
        SgtTitle *t;
        char sz[24], nm[32];

        if (idx >= app->ntitles) break;
        t = (SgtTitle *)&app->titles[idx];

        if (idx == app->tcur) {
            int a = 20 + (int)(12.0f * pulse);

            round_rect(11, y - 1, 378, row_h - 2, 6, C_RGBA(255, 63, 164, a));
            C2D_DrawRectSolid(11, y - 1, 0, 3, row_h - 2,
                              lerp_color(COL_PINK, COL_CYAN, pulse));
        }
        icon_file(18, y + 1, COL_PINK);
        ellipsize(nm, sizeof(nm), t->code, 20);
        txt(40, y + 2, 0.38f, idx == app->tcur ? COL_TEXT : C_RGBA(200, 212, 236, 255), "%s", nm);
        txt(128, y + 3, 0.28f, t->media ? COL_AMBER : COL_CYAN, t->media ? "CARTUCHO" : "SD");
        fmt_size(t->size, sz, sizeof(sz));
        txt_right(360, y + 3, 0.31f, COL_DIM, sz);
        checkbox(372, y + 5, t->selected, COL_LIME);
    }

    key_pill(12, 216, "A", "marcar", COL_CYAN);
    key_pill(78, 216, "Y", "respaldar", COL_LIME);
    key_pill(154, 216, "X", "restaurar", COL_AMBER);
    key_pill(226, 216, "B", "archivos", COL_DIM);
    key_pill(300, 216, "STA", "salir", COL_DIM);
}

static const char *state_color(SgtJobState s)
{
    switch (s) {
    case JOB_RUN:  return "COPIANDO";
    case JOB_OK:   return "HECHO";
    case JOB_FAIL: return "ERROR";
    default:       return "EN COLA";
    }
}

static u32 state_u32(SgtJobState s)
{
    switch (s) {
    case JOB_RUN:  return COL_CYAN;
    case JOB_OK:   return COL_LIME;
    case JOB_FAIL: return COL_RED;
    default:       return COL_DIM;
    }
}

static void queue_panel(const SgtApp *app)
{
    int i, shown = 5;
    float y0 = 36, row_h = 16;
    float t = now_s();

    panel(8, 30, 304, 88, 8, COL_PANEL, COL_LINE);
    txt(16, 33, 0.34f, COL_DIM, "COLA  -  %d trabajo%s", app->njobs, app->njobs == 1 ? "" : "s");

    for (i = 0; i < shown; i++) {
        int idx = i;
        SgtJob *j;
        char nm[40];
        u32 c;

        if (idx >= app->njobs) break;
        j = (SgtJob *)&app->jobs[idx];
        c = state_u32(j->state);
        if (idx == app->job_cur && app->running) {
            float pulse = 0.5f + 0.5f * sinf(t * 5.0f);

            round_rect(13, y0 + i * row_h - 1, 294, row_h - 1, 5,
                       C_RGBA(34, 224, 255, 16 + (int)(14.0f * pulse)));
            C2D_DrawCircleSolid(20, y0 + i * row_h + 5, 0, 5.5f + pulse, C_RGBA(34, 224, 255, 35));
        }
        C2D_DrawCircleSolid(20, y0 + i * row_h + 5, 0, 2.5f, c);
        ellipsize(nm, sizeof(nm), j->name, 30);
        txt(28, y0 + i * row_h - 1, 0.31f, j->state == JOB_FAIL ? COL_RED : COL_TEXT, "%s", nm);
        txt_right(304, y0 + i * row_h - 1, 0.28f, c, state_color(j->state));
    }
    if (app->njobs == 0) {
        txt_center(160, 70, 0.36f, COL_DIM, "Sin trabajos");
    }
}

static void progress_panel(const SgtApp *app)
{
    char name[60], done[24], total[24], eta[24], speed[24], pct[16], tail[40];
    float p = 0;
    float t = now_s();
    const SgtJob *j = NULL;

    panel(8, 122, 304, 78, 8, COL_PANEL, COL_LINE);
    if (!app->running || app->job_cur >= app->njobs) {
        int sel = 0, i;
        u64 bytes = 0;
        char selsize[24];

        for (i = 0; i < app->nentries; i++) if (app->entries[i].selected) { sel++; bytes += app->entries[i].size; }
        for (i = 0; i < app->ntitles; i++) if (app->titles[i].selected) sel++;
        fmt_size(bytes, selsize, sizeof(selsize));

        txt(18, 128, 0.42f, COL_DIM, "LISTO PARA TRANSFERIR");
        txt(18, 148, 0.3f, COL_DIM, "MODO");
        txt(64, 146, 0.45f, app->action == SGT_ACTION_MOVE ? COL_AMBER : COL_CYAN,
            "%s", app->action == SGT_ACTION_MOVE ? "MOVER" : "COPIAR");
        if (app->mode == SGT_MODE_SAVES)
            txt(18, 166, 0.32f, COL_DIM, "Marcados: %d titulos", sel);
        else
            txt(18, 166, 0.32f, COL_DIM, "Marcados: %d  -  %s", sel, selsize);
        txt(18, 182, 0.3f, COL_DIM, "Cierra la tapa: la transferencia sigue.");
        return;
    }

    j = &app->jobs[app->job_cur];
    if (app->cp.total > 0)
        p = (float)((double)app->cp.done / (double)app->cp.total);
    else
        p = (float)((double)j->done / (double)(j->bytes ? j->bytes : 1));

    ellipsize(name, sizeof(name), j->name, 40);
    txt(18, 126, 0.4f, COL_TEXT, "%s", name);
    snprintf(pct, sizeof(pct), "%d%%", (int)(p * 100.0f + 0.5f));
    txt_right(302, 122, 0.75f, COL_CYAN, pct);

    bar(18, 148, 284, 10, p, COL_CYAN, C_RGBA(30, 42, 76, 255));
    if (p > 0.01f) {
        float fw = 284.0f * p;
        float off = fmodf(t * 28.0f, 18.0f);
        float sx;

        /* Rayas en movimiento dentro de la barra. */
        for (sx = 18.0f - off; sx < 18.0f + fw; sx += 18.0f) {
            float a = fmaxf(sx, 18.0f);
            float b = fminf(sx + 8.0f, 18.0f + fw);

            if (b > a)
                C2D_DrawRectSolid(a, 150, 0, b - a, 6, C_RGBA(255, 255, 255, 24));
        }

        /* Punta luminosa. */
        C2D_DrawCircleSolid(18.0f + fw, 153, 0, 7.0f + 1.0f * sinf(t * 6.0f),
                            C_RGBA(34, 224, 255, 46));
    }

    fmt_size(app->cp.done, done, sizeof(done));
    fmt_size(app->cp.total, total, sizeof(total));
    fmt_eta(app->eta, eta, sizeof(eta));
    if (app->speed >= 1048576)
        snprintf(speed, sizeof(speed), "%.1f MB/s", (double)app->speed / 1048576.0);
    else
        snprintf(speed, sizeof(speed), "%llu KB/s", (unsigned long long)(app->speed / 1024));

    txt(18, 165, 0.3f, COL_DIM, "%s / %s", done, total);
    txt(18, 181, 0.3f, COL_LIME, "%s", speed);
    snprintf(tail, sizeof(tail), "faltan %s", eta);
    txt_right(302, 181, 0.3f, COL_AMBER, tail);
}

static void confirm_overlay(const SgtApp *app)
{
    const char *title = "CONFIRMAR";
    const char *msg = "";
    u32 c = COL_AMBER;

    if (app->confirm == SGT_CONFIRM_MOVE) {
        title = "MOVER ARCHIVOS";
        msg = "Se borrara el original tras copiar.";
    } else if (app->confirm == SGT_CONFIRM_RESTORE) {
        title = "RESTAURAR PARTIDAS";
        msg = "Se sobrescribira la partida actual.";
        c = COL_RED;
    } else if (app->confirm == SGT_CONFIRM_START) {
        title = "SALIR";
        msg = "Hay una transferencia en curso.";
        c = COL_RED;
    }

    C2D_DrawRectSolid(0, 0, 0, SGT_BOTTOM_W, 240, C_RGBA(3, 5, 12, 200));
    panel(36, 74, 248, 96, 12, C_RGBA(16, 22, 44, 255), c);
    txt_center(160, 86, 0.5f, c, title);
    txt_center(160, 112, 0.31f, COL_TEXT, msg);
    txt_center(160, 140, 0.36f, COL_LIME, "A = SI");
    txt_center(160, 156, 0.36f, COL_RED, "B = NO");
}

static void bottom(const SgtApp *app)
{
    background(SGT_BOTTOM_W, 240);

    txt(12, 8, 0.5f, COL_TEXT, "PANEL DE TRANSFERENCIA");
    txt_right(308, 10, 0.3f, COL_DIM, app->mode == SGT_MODE_SAVES ? "JUEGOS" : "ARCHIVOS");

    queue_panel(app);
    progress_panel(app);

    {
        int live = app->status[0] && osGetTime() < app->status_until;

        char line[160];

        snprintf(line, sizeof(line), "%s", live ? app->status : (app->running
                 ? "La consola no se suspende mientras copia"
                 : "START salir  -  L/R pagina"));
        txt_center(160, 206, 0.29f, live ? COL_AMBER : COL_DIM, line);
    }

    if (app->confirm != SGT_CONFIRM_NONE) confirm_overlay(app);
}

static int splash_active(void)
{
    return (osGetTime() - g_boot_ms) < 2300;
}

static void splash_draw(void)
{
    u64 ms = osGetTime() - g_boot_ms;
    float t = (float)ms / 1000.0f;
    float e = 1.0f - powf(1.0f - fminf(t / 0.65f, 1.0f), 3.0f);
    float p = fminf(t / 1.5f, 1.0f);
    int fade = ms > 2000 ? (int)((ms - 2000) * 255 / 300) : 0;
    int i;

    if (fade > 255) fade = 255;

    C2D_TargetClear(g_top, COL_BG0);
    C2D_SceneBegin(g_top);
    background(SGT_SCREEN_W, 240);

    /* Ondas detras del logo. */
    for (i = 0; i < 3; i++) {
        float rr = fmodf(t * 66.0f + i * 34.0f, 118.0f);
        int a = (int)(55.0f * (1.0f - rr / 118.0f));

        C2D_DrawCircleSolid(200, 86, 0, rr, C_RGBA(34, 224, 255, a));
    }

    round_rect(164, 50, 72, 72, 20, C_RGBA(7, 11, 24, 245));
    round_rect(166, 52, 68, 68, 19, C_RGBA(34, 224, 255, 24 + (int)(20.0f * (0.5f + 0.5f * sinf(t * 4.0f)))));
    txt_center(200, 64, 2.1f, COL_CYAN, "SD");

    /* El titulo entra deslizandose. */
    {
        float tx = 200.0f + (1.0f - e) * 46.0f;
        int a = (int)(255.0f * e);

        txt_center(tx, 132, 0.82f, C2D_Color32(232, 240, 255, a), "SD GAME TRANSFER");
        txt_center(tx, 154, 0.4f, C2D_Color32(255, 63, 164, a), "by frisk");
    }

    bar(88, 182, 224, 7, p, COL_CYAN, C_RGBA(30, 42, 76, 255));
    txt_center(200, 196, 0.3f, COL_DIM, "preparando la SD...");

    if (fade > 0)
        C2D_DrawRectSolid(0, 0, 0, SGT_SCREEN_W, 240, C_RGBA(3, 5, 12, fade));

    C2D_TargetClear(g_bot, COL_BG0);
    C2D_SceneBegin(g_bot);
    background(SGT_BOTTOM_W, 240);

    /* Spinner de puntos. */
    for (i = 0; i < 8; i++) {
        float ang = t * 3.2f + i * 0.785398f;
        int a = 40 + (int)(180.0f * (float)i / 7.0f);

        C2D_DrawCircleSolid(160 + cosf(ang) * 30, 92 + sinf(ang) * 30, 0, 2.2f + i * 0.22f,
                            C_RGBA(34, 224, 255, a));
    }
    txt_center(160, 86, 0.62f, COL_CYAN, "SD");
    txt_center(160, 140, 0.62f, COL_TEXT, "SD GAME TRANSFER");
    txt_center(160, 160, 0.32f, COL_PINK, "by frisk");

    {
        char dots[8], line[32];
        int n = (int)(t * 3.0f) % 4;

        snprintf(dots, sizeof(dots), "%.*s", n, "...");
        snprintf(line, sizeof(line), "cargando%s", dots);
        txt_center(160, 178, 0.3f, COL_DIM, line);
    }

    if (fade > 0)
        C2D_DrawRectSolid(0, 0, 0, SGT_BOTTOM_W, 240, C_RGBA(3, 5, 12, fade));
}

void ui_draw(const SgtApp *app)
{
    u64 now = osGetTime();
    int fade = 0;

    C2D_TextBufClear(g_buf);

    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

    if (splash_active()) {
        splash_draw();
        C3D_FrameEnd(0);
        return;
    }

    if (app->mode != g_last_mode) {
        g_last_mode = app->mode;
        g_mode_ms = now;
    }
    if (now - g_mode_ms < 230)
        fade = (int)(210.0f * (1.0f - (float)(now - g_mode_ms) / 230.0f));

    C2D_TargetClear(g_top, COL_BG0);
    C2D_SceneBegin(g_top);
    background(SGT_SCREEN_W, 240);
    if (app->mode == SGT_MODE_SAVES) saves_view(app);
    else files_view(app);
    if (fade > 0)
        C2D_DrawRectSolid(0, 0, 0, SGT_SCREEN_W, 240, C_RGBA(3, 5, 12, fade));

    C2D_TargetClear(g_bot, COL_BG0);
    C2D_SceneBegin(g_bot);
    bottom(app);
    if (fade > 0)
        C2D_DrawRectSolid(0, 0, 0, SGT_BOTTOM_W, 240, C_RGBA(3, 5, 12, fade));

    C3D_FrameEnd(0);
}
