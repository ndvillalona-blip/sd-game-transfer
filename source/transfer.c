#include "transfer.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define SGT_CHUNK (256 * 1024)

static void set_err(char *err, const char *msg)
{
    if (err) snprintf(err, 80, "%s", msg ? msg : "");
}

#ifndef SGT_HOST
void sgt_storage(u64 *sd_total, u64 *sd_free, u64 *nand_total, u64 *nand_free)
{
    FS_ArchiveResource r;

    *sd_total = *sd_free = *nand_total = *nand_free = 0;
    if (R_SUCCEEDED(FSUSER_GetSdmcArchiveResource(&r))) {
        *sd_total = (u64)r.totalClusters * r.clusterSize;
        *sd_free  = (u64)r.freeClusters  * r.clusterSize;
    }
    if (R_SUCCEEDED(FSUSER_GetNandArchiveResource(&r))) {
        *nand_total = (u64)r.totalClusters * r.clusterSize;
        *nand_free  = (u64)r.freeClusters  * r.clusterSize;
    }
}
#endif /* SGT_HOST */

void sgt_mkdirs(const char *file)
{
    char tmp[SGT_PATH];
    char *p;

    if (!file) return;
    snprintf(tmp, sizeof(tmp), "%s", file);
    p = strchr(tmp, '/');
    if (!p) return;
    for (p = p + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0777);
            *p = '/';
        }
    }
}

static int is_dir(const char *path)
{
    struct stat st;

    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static u64 entry_size(const char *path)
{
    struct stat st;

    return stat(path, &st) == 0 ? (u64)st.st_size : 0;
}

static int scan_rec(const char *src, const char *dst, SgtFile *files, int max,
                    int *nfiles, u64 *total, char *err, int depth)
{
    DIR *d;
    struct dirent *e;

    if (depth > 32) {
        set_err(err, "carpetas demasiado profundas");
        return -1;
    }
    d = opendir(src);
    if (!d) {
        set_err(err, "no puedo abrir una carpeta");
        return -1;
    }
    while ((e = readdir(d)) != NULL) {
        char s[SGT_PATH], t[SGT_PATH];

        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        snprintf(s, sizeof(s), "%s/%s", src, e->d_name);
        snprintf(t, sizeof(t), "%s/%s", dst, e->d_name);

        if (is_dir(s)) {
            mkdir(t, 0777);
            if (scan_rec(s, t, files, max, nfiles, total, err, depth + 1) < 0) {
                closedir(d);
                return -1;
            }
        } else {
            u64 sz;

            if (*nfiles >= max) {
                set_err(err, "demasiados archivos (max 1024)");
                closedir(d);
                return -1;
            }
            sz = entry_size(s);
            snprintf(files[*nfiles].src, SGT_PATH, "%s", s);
            snprintf(files[*nfiles].dst, SGT_PATH, "%s", t);
            files[*nfiles].size = sz;
            *nfiles += 1;
            *total += sz;
        }
    }
    closedir(d);
    return 0;
}

int sgt_scan(const char *src, const char *dst, SgtFile *files, int max,
             int *nfiles, u64 *total, char *err)
{
    *nfiles = 0;
    *total = 0;

    if (is_dir(src)) {
        mkdir(dst, 0777);
        return scan_rec(src, dst, files, max, nfiles, total, err, 0);
    }
    if (access(src, F_OK) != 0) {
        set_err(err, "el origen no existe");
        return -1;
    }
    if (max < 1) {
        set_err(err, "sin espacio en la cola");
        return -1;
    }
    snprintf(files[0].src, SGT_PATH, "%s", src);
    snprintf(files[0].dst, SGT_PATH, "%s", dst);
    files[0].size = entry_size(src);
    *nfiles = 1;
    *total = files[0].size;
    return 0;
}

static int measure_rec(const char *path, u64 *bytes, int depth)
{
    DIR *d;
    struct dirent *e;

    if (depth > 32) return -1;
    d = opendir(path);
    if (!d) return -1;
    while ((e = readdir(d)) != NULL) {
        char child[SGT_PATH];

        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        snprintf(child, sizeof(child), "%s/%s", path, e->d_name);
        if (is_dir(child)) {
            if (measure_rec(child, bytes, depth + 1) != 0) { closedir(d); return -1; }
        } else {
            *bytes += entry_size(child);
        }
    }
    closedir(d);
    return 0;
}

int sgt_measure(const char *src, u64 *bytes, char *err)
{
    *bytes = 0;
    if (is_dir(src)) {
        if (measure_rec(src, bytes, 0) != 0) {
            set_err(err, "no puedo leer la carpeta");
            return -1;
        }
        return 0;
    }
    if (access(src, F_OK) != 0) {
        set_err(err, "el origen no existe");
        return -1;
    }
    *bytes = entry_size(src);
    return 0;
}

int sgt_copy_step(SgtCopy *c)
{
    static u8 buf[SGT_CHUNK];

    for (;;) {
        SgtFile *f;
        size_t want, got, wr;

        if (!c->in) {
            if (c->idx >= c->nfiles) return 1;
            f = &c->files[c->idx];
            sgt_mkdirs(f->dst);
            c->in = fopen(f->src, "rb");
            if (!c->in) return -1;
            c->out = fopen(f->dst, "wb");
            if (!c->out) {
                fclose(c->in);
                c->in = NULL;
                return -1;
            }
            c->left = f->size;
            if (c->left == 0) {
                fclose(c->in);
                fclose(c->out);
                c->in = c->out = NULL;
                c->idx++;
                continue;
            }
        }

        want = c->left < SGT_CHUNK ? (size_t)c->left : SGT_CHUNK;
        got = fread(buf, 1, want, c->in);
        if (got == 0) {
            fclose(c->in);
            fclose(c->out);
            c->in = c->out = NULL;
            return -1;
        }
        wr = fwrite(buf, 1, got, c->out);
        if (wr != got) {
            fclose(c->in);
            fclose(c->out);
            c->in = c->out = NULL;
            return -1;
        }
        c->left -= got;
        c->done += got;
        if (c->left == 0) {
            fclose(c->in);
            fclose(c->out);
            c->in = c->out = NULL;
            c->idx++;
        }
        return 0;
    }
}

int sgt_remove_tree(const char *path)
{
    struct stat st;
    DIR *d;
    struct dirent *e;
    int rc = 0;

    if (!path || stat(path, &st) != 0) return -1;
    if (!S_ISDIR(st.st_mode)) return remove(path);

    d = opendir(path);
    if (!d) return -1;
    while ((e = readdir(d)) != NULL) {
        char child[SGT_PATH];

        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        snprintf(child, sizeof(child), "%s/%s", path, e->d_name);
        if (sgt_remove_tree(child) != 0) rc = -1;
    }
    closedir(d);
    if (rmdir(path) != 0) rc = -1;
    return rc;
}
