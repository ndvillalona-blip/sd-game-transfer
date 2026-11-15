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

static void background(float w, float h)
{
    int i;
    const int bands = 20;

    for (i = 0; i < bands; i++) {
        float t = (float)i / (float)(bands - 1);
        C2D_DrawRectSolid(0, h * i / bands, 0, w, h / bands + 1, lerp_color(COL_BG0, COL_BG1, t));
    }
    C2D_DrawCircleSolid(w - 40, 10, 0, 80, C_RGBA(34, 224, 255, 10));
    C2D_DrawCircleSolid(30, h - 10, 0, 70, C_RGBA(255, 63, 164, 9));
}

static void bar(float x, float y, float w, float h, float p, u32 fill, u32 track)
{
    if (p < 0) p = 0;
    if (p > 1) p = 1;
    round_rect(x, y, w, h, h / 2, track);
    if (p > 0.005f) {
        float fw = w * p;

        if (fw < h) fw = h;
        round_rect(x, y, fw, h, h / 2, fill);
    }
}

static void checkbox(float x, float y, int on, u32 accent)
{
    C2D_DrawCircleSolid(x + 6, y + 6, 0, 6, on ? accent : COL_LINE);
    if (!on) C2D_DrawCircleSolid(x + 6, y + 6, 0, 3.5f, C_RGBA(12, 18, 38, 255));
    else C2D_DrawCircleSolid(x + 6, y + 6, 0, 2.4f, COL_DARK);
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
              app->mode == SGT_MODE_SAVES ? "PARTIDAS" : (app->action == SGT_ACTION_MOVE ? "MOVER" : "COPIAR"));

    C2D_DrawRectSolid(12, 40, 0, 376, 1, C_RGBA(34, 224, 255, 90));
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
            round_rect(11, y - 1, 378, row_h - 2, 6, C_RGBA(34, 224, 255, 26));
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
    key_pill(206, 216, "SEL", "partidas", COL_PINK);
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
    snprintf(title, sizeof(title), "PARTIDAS  -  %d titulos en la SD", app->ntitles);
    txt(16, 98, 0.31f, COL_DIM, "%s", title);

    if (app->ntitles == 0) {
        txt_center(200, 150, 0.42f, COL_DIM, "Sin titulos instalados");
    }
    for (i = 0; i < rows; i++) {
        int idx = app->ttop + i;
        float y = y0 + i * row_h;
        SgtTitle *t;
        char sz[24], nm[32];

        if (idx >= app->ntitles) break;
        t = (SgtTitle *)&app->titles[idx];

        if (idx == app->tcur) {
            round_rect(11, y - 1, 378, row_h - 2, 6, C_RGBA(255, 63, 164, 26));
            C2D_DrawRectSolid(11, y - 1, 0, 3, row_h - 2,
                              lerp_color(COL_PINK, COL_CYAN, pulse));
        }
        icon_file(18, y + 1, COL_PINK);
        ellipsize(nm, sizeof(nm), t->code, 20);
        txt(40, y + 2, 0.38f, idx == app->tcur ? COL_TEXT : C_RGBA(200, 212, 236, 255), "%s", nm);
        txt(120, y + 3, 0.28f, COL_DIM, "%08lX",
            (unsigned long)((t->id >> 8) & 0xFFFFFFFFULL));
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
            round_rect(13, y0 + i * row_h - 1, 294, row_h - 1, 5, C_RGBA(34, 224, 255, 18));
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
    float shine = (float)(osGetTime() % 1600) / 1600.0f;
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
    if (p > 0.02f) {
        float sx = 18 + (284 - 20) * shine * p;
        round_rect(sx, 149, 12, 8, 4, C_RGBA(255, 255, 255, 60));
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
    txt_right(308, 10, 0.3f, COL_DIM, app->mode == SGT_MODE_SAVES ? "PARTIDAS" : "ARCHIVOS");

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

void ui_draw(const SgtApp *app)
{
    C2D_TextBufClear(g_buf);

    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

    C2D_TargetClear(g_top, COL_BG0);
    C2D_SceneBegin(g_top);
    background(SGT_SCREEN_W, 240);
    if (app->mode == SGT_MODE_SAVES) saves_view(app);
    else files_view(app);

    C2D_TargetClear(g_bot, COL_BG0);
    C2D_SceneBegin(g_bot);
    bottom(app);

    C3D_FrameEnd(0);
}
