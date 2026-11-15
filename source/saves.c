#include "saves.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <3ds/util/utf.h>

#define SGT_SAVE_CHUNK (64 * 1024)

static u8 g_buf[SGT_SAVE_CHUNK];

static void set_err(char *err, const char *msg)
{
    if (err) snprintf(err, 80, "%s", msg ? msg : "");
}

static FS_Path path_utf8(u16 *buf, size_t units, const char *utf8)
{
    ssize_t n = utf8_to_utf16(buf, (const u8 *)utf8, units - 1);

    if (n < 0) n = 0;
    buf[n] = 0;
    return fsMakePath(PATH_UTF16, buf);
}

static void name_utf8(const u16 *name, char *out, size_t outsz)
{
    ssize_t n = utf16_to_utf8((u8 *)out, name, outsz - 1);

    if (n < 0) n = 0;
    out[n] = '\0';
}

static void join_path(char *out, size_t outsz, const char *base, const char *name)
{
    if (!strcmp(base, "/"))
        snprintf(out, outsz, "/%s", name);
    else
        snprintf(out, outsz, "%s/%s", base, name);
}

static int open_save(u64 title_id, int media, FS_Archive *archive)
{
    u64 id = title_id;
    FS_Path p = { PATH_BINARY, sizeof(id), &id };

    if (media == 1) {
        FS_Path empty = { PATH_EMPTY, 0, NULL };

        return R_SUCCEEDED(FSUSER_OpenArchive(archive, ARCHIVE_GAMECARD_SAVEDATA, empty)) ? 0 : -1;
    }
    return R_SUCCEEDED(FSUSER_OpenArchive(archive, ARCHIVE_USER_SAVEDATA, p)) ? 0 : -1;
}

static int save_file_out(FS_Archive a, const char *apath, const char *outfile)
{
    u16 ubuf[300];
    FS_Path p = path_utf8(ubuf, 300, apath);
    Handle fh;
    u64 size = 0, off = 0;
    FILE *out;

    if (R_FAILED(FSUSER_OpenFile(&fh, a, p, FS_OPEN_READ, 0))) return -1;
    FSFILE_GetSize(fh, &size);
    out = fopen(outfile, "wb");
    if (!out) {
        FSFILE_Close(fh);
        return -1;
    }
    while (off < size) {
        u32 want = (size - off) > SGT_SAVE_CHUNK ? SGT_SAVE_CHUNK : (u32)(size - off);
        u32 got = 0;

        if (R_FAILED(FSFILE_Read(fh, &got, off, g_buf, want)) || got == 0) break;
        if (fwrite(g_buf, 1, got, out) != got) break;
        off += got;
    }
    fclose(out);
    FSFILE_Close(fh);
    return off == size ? 0 : -1;
}

static int backup_dir(FS_Archive a, const char *apath, const char *outdir, char *err, int depth)
{
    u16 ubuf[300];
    FS_Path p = path_utf8(ubuf, 300, apath);
    Handle dh;
    FS_DirectoryEntry *ents;
    u32 n = 0;

    if (depth > 24) return -1;
    if (R_FAILED(FSUSER_OpenDirectory(&dh, a, p))) {
        set_err(err, "no puedo abrir la partida");
        return -1;
    }
    ents = (FS_DirectoryEntry *)malloc(sizeof(FS_DirectoryEntry) * 16);
    if (!ents) {
        FSDIR_Close(dh);
        set_err(err, "sin memoria");
        return -1;
    }

    while (R_SUCCEEDED(FSDIR_Read(dh, &n, 16, ents)) && n > 0) {
        u32 i;

        for (i = 0; i < n; i++) {
            char name[300], child_ap[SGT_PATH], child_out[SGT_PATH];

            name_utf8(ents[i].name, name, sizeof(name));
            if (!name[0]) continue;
            join_path(child_ap, sizeof(child_ap), apath, name);
            snprintf(child_out, sizeof(child_out), "%s/%s", outdir, name);

            if (ents[i].attributes & FS_ATTRIBUTE_DIRECTORY) {
                mkdir(child_out, 0777);
                if (backup_dir(a, child_ap, child_out, err, depth + 1) != 0) {
                    free(ents);
                    FSDIR_Close(dh);
                    return -1;
                }
            } else if (save_file_out(a, child_ap, child_out) != 0) {
                set_err(err, "fallo al leer un archivo");
                free(ents);
                FSDIR_Close(dh);
                return -1;
            }
        }
    }

    free(ents);
    FSDIR_Close(dh);
    return 0;
}

static int clear_dir(FS_Archive a, const char *apath)
{
    u16 ubuf[300];
    FS_Path p = path_utf8(ubuf, 300, apath);
    Handle dh;
    FS_DirectoryEntry *ents;
    u32 n = 0;
    int rc = 0;

    if (R_FAILED(FSUSER_OpenDirectory(&dh, a, p))) return -1;
    ents = (FS_DirectoryEntry *)malloc(sizeof(FS_DirectoryEntry) * 16);
    if (!ents) {
        FSDIR_Close(dh);
        return -1;
    }

    while (R_SUCCEEDED(FSDIR_Read(dh, &n, 16, ents)) && n > 0) {
        u32 i;

        for (i = 0; i < n; i++) {
            char name[300], child[SGT_PATH];
            u16 cbuf[300];
            FS_Path cp;

            name_utf8(ents[i].name, name, sizeof(name));
            if (!name[0]) continue;
            join_path(child, sizeof(child), apath, name);
            cp = path_utf8(cbuf, 300, child);

            if (ents[i].attributes & FS_ATTRIBUTE_DIRECTORY) {
                if (R_FAILED(FSUSER_DeleteDirectoryRecursively(a, cp))) rc = -1;
            } else {
                if (R_FAILED(FSUSER_DeleteFile(a, cp))) rc = -1;
            }
        }
    }

    free(ents);
    FSDIR_Close(dh);
    return rc;
}

static int restore_file_in(FS_Archive a, const char *apath, const char *infile)
{
    u16 ubuf[300];
    FS_Path p = path_utf8(ubuf, 300, apath);
    Handle fh;
    FILE *in;
    u64 off = 0;
    int rc = 0;

    if (R_FAILED(FSUSER_OpenFile(&fh, a, p, FS_OPEN_WRITE | FS_OPEN_CREATE, 0))) return -1;
    in = fopen(infile, "rb");
    if (!in) {
        FSFILE_Close(fh);
        return -1;
    }
    for (;;) {
        size_t got = fread(g_buf, 1, SGT_SAVE_CHUNK, in);
        u32 written = 0;

        if (got == 0) break;
        if (R_FAILED(FSFILE_Write(fh, &written, off, g_buf, (u32)got, 0)) || written != got) {
            rc = -1;
            break;
        }
        off += got;
    }
    fclose(in);
    FSFILE_Close(fh);
    return rc;
}

static int restore_dir(FS_Archive a, const char *apath, const char *indir, char *err, int depth)
{
    DIR *d;
    struct dirent *e;

    if (depth > 24) return -1;
    d = opendir(indir);
    if (!d) {
        set_err(err, "no encuentro la copia");
        return -1;
    }
    while ((e = readdir(d)) != NULL) {
        char child_ap[SGT_PATH], child_in[SGT_PATH];
        struct stat st;

        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        join_path(child_ap, sizeof(child_ap), apath, e->d_name);
        snprintf(child_in, sizeof(child_in), "%s/%s", indir, e->d_name);
        if (stat(child_in, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            u16 ubuf[300];
            FS_Path p = path_utf8(ubuf, 300, child_ap);

            FSUSER_CreateDirectory(a, p, 0);
            if (restore_dir(a, child_ap, child_in, err, depth + 1) != 0) {
                closedir(d);
                return -1;
            }
        } else if (restore_file_in(a, child_ap, child_in) != 0) {
            set_err(err, "fallo al escribir un archivo");
            closedir(d);
            return -1;
        }
    }
    closedir(d);
    return 0;
}

int sgt_save_list(SgtTitle *out, int max)
{
    static u64 ids[256];
    static AM_TitleInfo infos[256];
    u32 count = 0, read = 0, i;
    int n = 0;

    if (!out || max <= 0) return -1;
    if (R_FAILED(AM_GetTitleCount(MEDIATYPE_SD, &count)) || count == 0) return 0;
    if (count > 256) count = 256;
    if (R_FAILED(AM_GetTitleList(&read, MEDIATYPE_SD, count, ids))) return -1;
    if (R_FAILED(AM_GetTitleInfo(MEDIATYPE_SD, read, ids, infos))) memset(infos, 0, sizeof(infos));

    for (i = 0; i < read && n < max; i++) {
        char code[16] = "-";

        if (((ids[i] >> 32) & 0xFFFFFFFFULL) != 0x00040000ULL) continue;
        if (R_FAILED(AM_GetTitleProductCode(MEDIATYPE_SD, ids[i], code))) snprintf(code, sizeof(code), "-");
        out[n].id = ids[i];
        snprintf(out[n].code, sizeof(out[n].code), "%s", code);
        out[n].size = infos[i].size;
        out[n].selected = 0;
        out[n].media = 0;
        n++;
    }

    /* El cartucho insertado tambien aparece en el HOME: lo anadimos al final. */
    {
        u32 gcount = 0, gread = 0;
        u64 gid = 0;
        AM_TitleInfo ginfo;

        memset(&ginfo, 0, sizeof(ginfo));
        if (n < max &&
            R_SUCCEEDED(AM_GetTitleCount(MEDIATYPE_GAME_CARD, &gcount)) && gcount > 0 &&
            R_SUCCEEDED(AM_GetTitleList(&gread, MEDIATYPE_GAME_CARD, 1, &gid)) && gread > 0) {
            char code[16] = "-";

            AM_GetTitleInfo(MEDIATYPE_GAME_CARD, 1, &gid, &ginfo);
            if (R_FAILED(AM_GetTitleProductCode(MEDIATYPE_GAME_CARD, gid, code)))
                snprintf(code, sizeof(code), "-");
            out[n].id = gid;
            snprintf(out[n].code, sizeof(out[n].code), "%s", code);
            out[n].size = ginfo.size;
            out[n].selected = 0;
            out[n].media = 1;
            n++;
        }
    }
    return n;
}

int sgt_save_backup(u64 title_id, int media, const char *dst_root, char *err)
{
    FS_Archive a;

    if (open_save(title_id, media, &a) != 0) {
        set_err(err, "sin partida o sin acceso (CFW)");
        return -1;
    }
    if (backup_dir(a, "/", dst_root, err, 0) != 0) {
        FSUSER_CloseArchive(a);
        return -1;
    }
    FSUSER_CloseArchive(a);
    return 0;
}

int sgt_save_restore(u64 title_id, int media, const char *src_root, char *err)
{
    FS_Archive a;
    int rc;

    if (open_save(title_id, media, &a) != 0) {
        set_err(err, "sin partida o sin acceso (CFW)");
        return -1;
    }
    clear_dir(a, "/");
    rc = restore_dir(a, "/", src_root, err, 0);
    FSUSER_CloseArchive(a);
    return rc;
}
