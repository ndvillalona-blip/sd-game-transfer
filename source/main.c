/* SD Game Transfer - copia y respalda archivos y partidas desde la SD.
 * Creado por frisk.
 */
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include <3ds.h>

#include "app.h"
#include "saves.h"
#include "transfer.h"
#include "ui.h"

#define DEST_ROOT "sdmc:/SDGameTransfer"
#define DEST_COPY "sdmc:/SDGameTransfer/copias"
#define DEST_SAVE "sdmc:/SDGameTransfer/saves"

static SgtApp g_app;
static volatile int g_quit;

static void set_status(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(g_app.status, sizeof(g_app.status), fmt, ap);
    va_end(ap);
    g_app.status_until = osGetTime() + 4000;
}

static void join_path(char *out, size_t outsz, const char *base, const char *name)
{
    size_t n = strlen(base);

    if (n > 0 && base[n - 1] == '/')
        snprintf(out, outsz, "%s%s", base, name);
    else
        snprintf(out, outsz, "%s/%s", base, name);
}

static const char *last_name(const char *path)
{
    const char *p = strrchr(path, '/');

    return p ? p + 1 : path;
}

static void refresh_storage(void)
{
    sgt_storage(&g_app.sd_total, &g_app.sd_free, &g_app.nand_total, &g_app.nand_free);
}

static int cmp_entry(const void *a, const void *b)
{
    const SgtEntry *x = (const SgtEntry *)a;
    const SgtEntry *y = (const SgtEntry *)b;

    if (x->is_dir != y->is_dir) return y->is_dir - x->is_dir;
    return strcasecmp(x->name, y->name);
}

static void refresh_entries(void)
{
    DIR *d;
    struct dirent *e;
    int n = 0;

    g_app.nentries = 0;
    g_app.cursor = 0;
    g_app.top = 0;

    d = opendir(g_app.cwd);
    if (!d) {
        set_status("no puedo abrir %s", g_app.cwd);
        snprintf(g_app.cwd, sizeof(g_app.cwd), "sdmc:/");
        d = opendir(g_app.cwd);
        if (!d) return;
    }

    while ((e = readdir(d)) != NULL && n < SGT_MAX_ENTRIES) {
        char full[SGT_PATH];
        struct stat st;
        SgtEntry *en;

        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if (!e->d_name[0]) continue;
        join_path(full, sizeof(full), g_app.cwd, e->d_name);
        if (stat(full, &st) != 0) continue;

        en = &g_app.entries[n];
        memset(en, 0, sizeof(*en));
        snprintf(en->name, sizeof(en->name), "%s", e->d_name);
        en->is_dir = S_ISDIR(st.st_mode);
        en->size = (u64)st.st_size;
        n++;
    }
    closedir(d);

    qsort(g_app.entries, n, sizeof(SgtEntry), cmp_entry);
    g_app.nentries = n;
}

static int cmp_title(const void *a, const void *b)
{
    const SgtTitle *x = (const SgtTitle *)a;
    const SgtTitle *y = (const SgtTitle *)b;

    return strcasecmp(x->code, y->code);
}

static void load_titles(void)
{
    int n = sgt_save_list(g_app.titles, SGT_MAX_TITLES);

    if (n < 0) n = 0;
    qsort(g_app.titles, n, sizeof(SgtTitle), cmp_title);
    g_app.ntitles = n;
    g_app.tcur = 0;
    g_app.ttop = 0;
}

static int selected_files(void)
{
    int i, n = 0;

    for (i = 0; i < g_app.nentries; i++) if (g_app.entries[i].selected) n++;
    return n;
}

static int selected_titles(void)
{
    int i, n = 0;

    for (i = 0; i < g_app.ntitles; i++) if (g_app.titles[i].selected) n++;
    return n;
}

static int is_inside(const char *child, const char *parent)
{
    size_t n = strlen(parent);

    return !strncmp(child, parent, n) && (child[n] == '/' || child[n] == '\0');
}

static int begin_job(int idx)
{
    SgtJob *j = &g_app.jobs[idx];
    char err[80] = "";

    memset(&g_app.cp, 0, sizeof(g_app.cp));
    g_app.cp.move = j->move;
    snprintf(g_app.cp.root, sizeof(g_app.cp.root), "%s", j->src);

    if (sgt_scan(j->src, j->dst, g_app.cp.files, SGT_MAX_FILES,
                 &g_app.cp.nfiles, &g_app.cp.total, err) != 0) {
        j->state = JOB_FAIL;
        snprintf(j->error, sizeof(j->error), "%s", err);
        return -1;
    }

    j->state = JOB_RUN;
    j->bytes = g_app.cp.total;
    j->done = 0;
    g_app.job_cur = idx;
    g_app.running = 1;
    g_app.speed = 0;
    g_app.eta = 0;
    g_app.last_done = 0;
    g_app.last_ms = osGetTime();

    if (!g_app.sleep_off) {
        aptSetSleepAllowed(false);
        g_app.sleep_off = 1;
    }
    if (!g_app.home_off) {
        aptSetHomeAllowed(false);
        g_app.home_off = 1;
    }
    return 0;
}

static void end_transfer(void)
{
    g_app.running = 0;
    g_app.job_cur = -1;
    if (g_app.sleep_off) {
        aptSetSleepAllowed(true);
        g_app.sleep_off = 0;
    }
    if (g_app.home_off) {
        aptSetHomeAllowed(true);
        g_app.home_off = 0;
    }
    refresh_storage();
    refresh_entries();
}

static void advance_job(void)
{
    int i;

    for (i = g_app.job_cur + 1; i < g_app.njobs; i++) {
        if (begin_job(i) == 0) return;
    }
    end_transfer();
    set_status("transferencia terminada");
}

static void finish_job(int ok)
{
    SgtJob *j;

    if (g_app.job_cur < 0 || g_app.job_cur >= g_app.njobs) return;
    j = &g_app.jobs[g_app.job_cur];

    if (g_app.cp.in) { fclose(g_app.cp.in); g_app.cp.in = NULL; }
    if (g_app.cp.out) { fclose(g_app.cp.out); g_app.cp.out = NULL; }

    if (ok) {
        j->done = j->bytes;
        if (j->move) sgt_remove_tree(j->src);
        j->state = JOB_OK;
    } else {
        j->state = JOB_FAIL;
        if (!j->error[0]) snprintf(j->error, sizeof(j->error), "error de copia");
    }
    advance_job();
}

static void start_queue(void)
{
    int i, n = 0;
    u64 need = 0, freeb = 0;
    char root[SGT_PATH];

    if (g_app.running) return;

    mkdir(DEST_ROOT, 0777);
    mkdir(DEST_COPY, 0777);

    if (selected_files() == 0 && g_app.nentries > 0)
        g_app.entries[g_app.cursor].selected = 1;

    refresh_storage();
    freeb = g_app.sd_free;

    for (i = 0; i < g_app.nentries; i++) {
        if (!g_app.entries[i].selected) continue;
        {
            char src[SGT_PATH], dst[SGT_PATH], err[80];
            u64 bytes = 0;

            join_path(src, sizeof(src), g_app.cwd, g_app.entries[i].name);
            join_path(dst, sizeof(dst), DEST_COPY, g_app.entries[i].name);

            if (is_inside(dst, src)) {
                set_status("no puedo copiar una carpeta dentro de si misma");
                return;
            }
            if (!strcmp(src, dst)) continue;
            if (sgt_measure(src, &bytes, err) != 0) {
                set_status("%s: %s", g_app.entries[i].name, err);
                return;
            }
            need += bytes;
            n++;
        }
    }

    if (n == 0) {
        set_status("marca algo con A");
        return;
    }
    if (need + 8ULL * 1024 * 1024 > freeb) {
        set_status("no hay espacio libre suficiente");
        return;
    }

    g_app.njobs = 0;
    for (i = 0; i < g_app.nentries && g_app.njobs < SGT_MAX_QUEUE; i++) {
        SgtJob *j;
        char err[80];
        u64 bytes = 0;

        if (!g_app.entries[i].selected) continue;
        join_path(root, sizeof(root), g_app.cwd, g_app.entries[i].name);
        if (sgt_measure(root, &bytes, err) != 0) continue;

        j = &g_app.jobs[g_app.njobs];
        memset(j, 0, sizeof(*j));
        snprintf(j->src, sizeof(j->src), "%s", root);
        join_path(j->dst, sizeof(j->dst), DEST_COPY, g_app.entries[i].name);
        snprintf(j->name, sizeof(j->name), "%s", g_app.entries[i].name);
        j->bytes = bytes;
        j->move = (g_app.action == SGT_ACTION_MOVE);
        j->state = JOB_WAIT;
        g_app.njobs++;
    }

    if (g_app.njobs == 0) {
        set_status("nada que transferir");
        return;
    }

    g_app.job_cur = -1;
    set_status("transfiriendo %d elemento%s", g_app.njobs, g_app.njobs == 1 ? "" : "s");
    advance_job();
}

static void do_backup(void)
{
    int i, ok = 0, fail = 0;

    if (selected_titles() == 0 && g_app.ntitles > 0)
        g_app.titles[g_app.tcur].selected = 1;
    if (selected_titles() == 0) {
        set_status("marca una partida con A");
        return;
    }

    mkdir(DEST_ROOT, 0777);
    mkdir(DEST_SAVE, 0777);
    aptSetSleepAllowed(false);
    g_app.sleep_off = 1;

    for (i = 0; i < g_app.ntitles; i++) {
        char dir[SGT_PATH], err[80];

        if (!g_app.titles[i].selected) continue;
        snprintf(dir, sizeof(dir), "%s/%016llX", DEST_SAVE,
                 (unsigned long long)g_app.titles[i].id);
        mkdir(dir, 0777);
        if (sgt_save_backup(g_app.titles[i].id, dir, err) == 0) ok++;
        else {
            fail++;
            set_status("%s: %s", g_app.titles[i].code, err);
        }
    }

    aptSetSleepAllowed(true);
    g_app.sleep_off = 0;
    if (ok > 0) set_status("%d partida%s respaldada%s", ok, ok == 1 ? "" : "s", ok == 1 ? "" : "s");
    if (fail > 0) set_status("%d correctas, %d con error", ok, fail);
    refresh_storage();
}

static void do_restore(void)
{
    int i, ok = 0, fail = 0;

    if (selected_titles() == 0 && g_app.ntitles > 0)
        g_app.titles[g_app.tcur].selected = 1;
    if (selected_titles() == 0) {
        set_status("marca una partida con A");
        return;
    }

    aptSetSleepAllowed(false);
    g_app.sleep_off = 1;

    for (i = 0; i < g_app.ntitles; i++) {
        char dir[SGT_PATH], err[80];
        struct stat st;

        if (!g_app.titles[i].selected) continue;
        snprintf(dir, sizeof(dir), "%s/%016llX", DEST_SAVE,
                 (unsigned long long)g_app.titles[i].id);
        if (stat(dir, &st) != 0) {
            fail++;
            continue;
        }
        if (sgt_save_restore(g_app.titles[i].id, dir, err) == 0) ok++;
        else {
            fail++;
            set_status("%s: %s", g_app.titles[i].code, err);
        }
    }

    aptSetSleepAllowed(true);
    g_app.sleep_off = 0;
    if (ok > 0) set_status("%d partida%s restaurada%s", ok, ok == 1 ? "" : "s", ok == 1 ? "" : "s");
    if (fail > 0) set_status("%d restauradas, %d sin copia/error", ok, fail);
    refresh_storage();
}

static void move_cursor(int delta, int count, int *cursor, int *top)
{
    int c;

    if (count <= 0) return;
    c = *cursor + delta;
    if (c < 0) c = 0;
    if (c >= count) c = count - 1;
    *cursor = c;
    if (c < *top) *top = c;
    if (c >= *top + 4) *top = c - 3;
}

static void handle_input(u32 k)
{
    if (g_app.confirm != SGT_CONFIRM_NONE) {
        if (k & KEY_A) {
            int c = g_app.confirm;

            g_app.confirm = SGT_CONFIRM_NONE;
            if (c == SGT_CONFIRM_MOVE) start_queue();
            else if (c == SGT_CONFIRM_RESTORE) do_restore();
            else if (c == SGT_CONFIRM_START) g_quit = 1;
        } else if (k & KEY_B) {
            g_app.confirm = SGT_CONFIRM_NONE;
        }
        return;
    }

    if (k & KEY_START) {
        if (g_app.running) set_status("espera a que termine la transferencia");
        else g_quit = 1;
        return;
    }
    if (g_app.running) return;

    if (k & KEY_SELECT) {
        g_app.mode = (g_app.mode == SGT_MODE_FILES) ? SGT_MODE_SAVES : SGT_MODE_FILES;
        if (g_app.mode == SGT_MODE_SAVES) load_titles();
        return;
    }

    if (g_app.mode == SGT_MODE_SAVES) {
        if (k & KEY_B) { g_app.mode = SGT_MODE_FILES; return; }
        if (k & KEY_UP) move_cursor(-1, g_app.ntitles, &g_app.tcur, &g_app.ttop);
        if (k & KEY_DOWN) move_cursor(1, g_app.ntitles, &g_app.tcur, &g_app.ttop);
        if (k & KEY_L) move_cursor(-4, g_app.ntitles, &g_app.tcur, &g_app.ttop);
        if (k & KEY_R) move_cursor(4, g_app.ntitles, &g_app.tcur, &g_app.ttop);
        if (k & KEY_A) {
            if (g_app.ntitles > 0)
                g_app.titles[g_app.tcur].selected = !g_app.titles[g_app.tcur].selected;
            return;
        }
        if (k & KEY_Y) { do_backup(); return; }
        if (k & KEY_X) {
            if (selected_titles() > 0) g_app.confirm = SGT_CONFIRM_RESTORE;
            else set_status("marca una partida con A");
            return;
        }
        return;
    }

    if (k & KEY_UP) move_cursor(-1, g_app.nentries, &g_app.cursor, &g_app.top);
    if (k & KEY_DOWN) move_cursor(1, g_app.nentries, &g_app.cursor, &g_app.top);
    if (k & KEY_L) move_cursor(-4, g_app.nentries, &g_app.cursor, &g_app.top);
    if (k & KEY_R) move_cursor(4, g_app.nentries, &g_app.cursor, &g_app.top);

    if (k & KEY_A) {
        if (g_app.nentries > 0)
            g_app.entries[g_app.cursor].selected = !g_app.entries[g_app.cursor].selected;
        return;
    }

    if (k & KEY_X) {
        g_app.action = (g_app.action == SGT_ACTION_COPY) ? SGT_ACTION_MOVE : SGT_ACTION_COPY;
        set_status(g_app.action == SGT_ACTION_MOVE ? "modo mover: se borra el original" : "modo copiar");
        return;
    }

    if (k & KEY_Y) {
        if (g_app.action == SGT_ACTION_MOVE && selected_files() > 0)
            g_app.confirm = SGT_CONFIRM_MOVE;
        else
            start_queue();
        return;
    }

    if (k & KEY_B) {
        char *p;

        if (!strcmp(g_app.cwd, "sdmc:/")) return;
        p = strrchr(g_app.cwd, '/');
        if (p && p > g_app.cwd + 6) *p = '\0';
        else snprintf(g_app.cwd, sizeof(g_app.cwd), "sdmc:/");
        refresh_entries();
        return;
    }

    if (k & (KEY_RIGHT)) {
        char full[SGT_PATH];
        struct stat st;

        if (g_app.nentries <= 0) return;
        if (!g_app.entries[g_app.cursor].is_dir) return;
        join_path(full, sizeof(full), g_app.cwd, g_app.entries[g_app.cursor].name);
        if (stat(full, &st) != 0) return;
        snprintf(g_app.cwd, sizeof(g_app.cwd), "%s", full);
        refresh_entries();
    }
}

int main(void)
{
    u64 last_storage = 0, last_battery = 0;
    u64 last_speed_ms = 0;

    memset(&g_app, 0, sizeof(g_app));
    g_app.battery = -1;
    snprintf(g_app.cwd, sizeof(g_app.cwd), "sdmc:/");

    fsInit();
    amInit();
    cfguInit();
    mcuHwcInit();

    ui_init();
    refresh_storage();
    refresh_entries();

    if (g_app.sd_total == 0) set_status("no puedo leer la tarjeta SD");
    else set_status("listo: marca archivos con A");

    while (aptMainLoop() && !g_quit) {
        u32 k;
        u64 now = osGetTime();

        hidScanInput();
        k = hidKeysDown();
        handle_input(k);

        if (g_app.running) {
            int r = sgt_copy_step(&g_app.cp);

            if (r == 1) finish_job(1);
            else if (r < 0) finish_job(0);
            else if (g_app.job_cur >= 0) g_app.jobs[g_app.job_cur].done = g_app.cp.done;

            if (now - last_speed_ms >= 400) {
                u64 delta = g_app.cp.done - g_app.last_done;
                u64 ms = now - last_speed_ms;

                if (ms > 0) g_app.speed = delta * 1000 / ms;
                g_app.last_done = g_app.cp.done;
                last_speed_ms = now;
            }
            if (g_app.speed > 0 && g_app.cp.total > g_app.cp.done)
                g_app.eta = (g_app.cp.total - g_app.cp.done) / g_app.speed;
            else
                g_app.eta = 0;
        } else {
            last_speed_ms = now;
        }

        if (now - last_storage > 3000) {
            refresh_storage();
            last_storage = now;
        }
        if (now - last_battery > 3000) {
            u8 level = 0;

            if (R_SUCCEEDED(MCUHWC_GetBatteryLevel(&level))) g_app.battery = level;
            last_battery = now;
        }

        ui_draw(&g_app);
        svcSleepThread((g_app.running ? 8ULL : 16ULL) * 1000000ULL);
    }

    if (g_app.sleep_off) aptSetSleepAllowed(true);
    if (g_app.home_off) aptSetHomeAllowed(true);

    ui_exit();
    mcuHwcExit();
    cfguExit();
    amExit();
    fsExit();
    return 0;
}
