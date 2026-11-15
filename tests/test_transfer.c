/* Prueba pesada del motor de copia (se compila para el PC con SGT_HOST).
 * Genera miles de archivos, los copia con el mismo codigo que la 3DS y
 * comprueba tamano y contenido de cada uno.
 */
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/stat.h>

#include "transfer.h"

#define TMP       "tests/tmp"
#define SRC       TMP "/src"
#define DST       TMP "/dst"
#define MOVESRC   TMP "/mover"
#define MOVEDST   TMP "/movido"
#define MANY      TMP "/muchos"

static int g_fail;

static void fail(const char *fmt, ...)
{
    va_list ap;

    g_fail = 1;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
}

static u64 fnv_file(const char *path, u64 *out_size)
{
    FILE *f = fopen(path, "rb");
    u64 h = 1469598103934665603ULL, n = 0;
    unsigned char buf[65536];
    size_t got;

    if (!f) return 0;
    while ((got = fread(buf, 1, sizeof(buf), f)) > 0) {
        size_t i;

        for (i = 0; i < got; i++) {
            h ^= buf[i];
            h *= 1099511628211ULL;
        }
        n += got;
    }
    fclose(f);
    if (out_size) *out_size = n;
    return h;
}

static int write_pattern(const char *path, u64 size, u64 seed)
{
    FILE *f;
    unsigned char buf[65536];
    u64 written = 0;

    sgt_mkdirs(path);
    f = fopen(path, "wb");
    if (!f) {
        fail("no puedo crear %s", path);
        return -1;
    }
    while (written < size) {
        size_t chunk = (size - written) > sizeof(buf) ? sizeof(buf) : (size_t)(size - written);
        size_t i;

        for (i = 0; i < chunk; i++) buf[i] = (unsigned char)((seed + written + i) * 31u + 7u);
        if (fwrite(buf, 1, chunk, f) != chunk) break;
        written += chunk;
    }
    fclose(f);
    return written == size ? 0 : -1;
}

static int make_tree(void)
{
    int d, i;

    mkdir(TMP, 0777);
    for (d = 0; d < 24; d++) {
        char dir[256];

        snprintf(dir, sizeof(dir), SRC "/carpeta_%02d", d);
        mkdir(dir, 0777);
        for (i = 0; i < 36; i++) {
            char path[400];
            u64 size = (u64)((d * 7919 + i * 104729) % 400000);

            if ((d * 36 + i) % 97 == 0) size = 8 * 1024 * 1024;
            snprintf(path, sizeof(path), "%s/archivo_%02d_%02d.bin", dir, d, i);
            if (write_pattern(path, size, (u64)(d * 1000 + i)) != 0) return -1;
        }
    }

    if (write_pattern(SRC "/vacio.bin", 0, 1) != 0) return -1;
    if (write_pattern(SRC "/con espacios y guiones.txt", 12345, 2) != 0) return -1;
    if (write_pattern(SRC "/a/b/c/d/e/f/g/h/i/j/fondo.bin", 1024 * 1024, 3) != 0) return -1;
    return 0;
}

static int copy_tree(const char *src, const char *dst, int verbose)
{
    static SgtFile files[SGT_MAX_FILES];
    SgtCopy *cp = (SgtCopy *)calloc(1, sizeof(SgtCopy));
    char err[80] = "";
    int n = 0, steps = 0, r;
    u64 total = 0;
    clock_t t0;

    if (!cp) return -1;
    if (sgt_scan(src, dst, files, SGT_MAX_FILES, &n, &total, err) != 0) {
        fail("scan de %s: %s", src, err);
        free(cp);
        return -1;
    }

    cp->nfiles = n;
    cp->total = total;
    memcpy(cp->files, files, sizeof(SgtFile) * (size_t)n);

    t0 = clock();
    while ((r = sgt_copy_step(cp)) == 0) steps++;
    if (verbose) {
        double secs = (double)(clock() - t0) / CLOCKS_PER_SEC;

        printf("  %d archivos, %.1f MB, %d pasos, %.2f s (%.1f MB/s CPU)\n",
               n, (double)total / 1048576.0, steps, secs,
               secs > 0 ? (double)total / 1048576.0 / secs : 0.0);
    }
    free(cp);
    if (r != 1) {
        fail("la copia de %s termino con %d", src, r);
        return -1;
    }
    return 0;
}

static int verify_tree(const char *src, const char *dst)
{
    /* Recorre el origen y compara hash y tamano con el destino. */
    DIR *d = opendir(src);
    struct dirent *e;
    int count = 0;

    if (!d) {
        fail("no puedo abrir %s para verificar", src);
        return -1;
    }
    while ((e = readdir(d)) != NULL) {
        char s[512], t[512];
        struct stat st;

        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        snprintf(s, sizeof(s), "%s/%s", src, e->d_name);
        snprintf(t, sizeof(t), "%s/%s", dst, e->d_name);
        if (stat(s, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            count += verify_tree(s, t);
        } else {
            u64 hs, hd, ss, sd;
            struct stat td;

            hs = fnv_file(s, &ss);
            if (stat(t, &td) != 0 || !S_ISREG(td.st_mode)) {
                fail("falta el destino %s", t);
                continue;
            }
            hd = fnv_file(t, &sd);
            if (hs != hd || ss != sd) {
                fail("contenido distinto en %s (%llu vs %llu bytes)", e->d_name,
                     (unsigned long long)ss, (unsigned long long)sd);
            }
            count++;
        }
    }
    closedir(d);
    return count;
}

static int test_limit(void)
{
    char path[256], err[80] = "";
    static SgtFile files[SGT_MAX_FILES];
    int i, n = 0;
    u64 total = 0;

    mkdir(MANY, 0777);
    for (i = 0; i < 1100; i++) {
        snprintf(path, sizeof(path), MANY "/f%04d.txt", i);
        if (write_pattern(path, 10, (u64)i) != 0) return -1;
    }
    if (sgt_scan(MANY, TMP "/muchos_dst", files, SGT_MAX_FILES, &n, &total, err) == 0) {
        fail("el limite de 1024 archivos no salto");
        return -1;
    }
    printf("  limite de archivos: OK (%s)\n", err);
    return 0;
}

int main(void)
{
    int copied;
    char err[80] = "";
    static SgtFile files[SGT_MAX_FILES];
    int n = 0;
    u64 total = 0;

    printf("== SD Game Transfer: prueba pesada del motor ==\n");
    sgt_remove_tree(TMP);
    mkdir(TMP, 0777);

    printf("[1] generando arbol de prueba...\n");
    if (make_tree() != 0) return 1;

    printf("[2] escaneando...\n");
    if (sgt_scan(SRC, DST, files, SGT_MAX_FILES, &n, &total, err) != 0) {
        printf("FALLO en scan: %s\n", err);
        return 1;
    }
    printf("  %d archivos, %.1f MB\n", n, (double)total / 1048576.0);

    printf("[3] copiando con el motor real...\n");
    if (copy_tree(SRC, DST, 1) != 0) return 1;

    printf("[4] verificando contenido...\n");
    copied = verify_tree(SRC, DST);
    printf("  %d archivos verificados\n", copied);
    if (copied != n) {
        fail("se esperaban %d archivos y se verificaron %d", n, copied);
    }

    printf("[5] copiando otra vez (sobrescribir)...\n");
    if (copy_tree(SRC, DST, 0) != 0) return 1;

    printf("[6] mover y borrar origen...\n");
    if (copy_tree(SRC "/carpeta_00", MOVEDST, 0) != 0) return 1;
    if (sgt_remove_tree(SRC "/carpeta_00") != 0) {
        fail("no pude borrar el origen movido");
    } else if (access(SRC "/carpeta_00", F_OK) == 0) {
        fail("el origen movido sigue existiendo");
    } else {
        printf("  origen borrado: OK\n");
    }

    printf("[7] limite de 1024 archivos...\n");
    if (test_limit() != 0) return 1;

    printf("[8] limpiando...\n");
    sgt_remove_tree(TMP);

    if (g_fail) {
        printf("\nRESULTADO: FALLOS\n");
        return 1;
    }
    printf("\nRESULTADO: TODO OK\n");
    return 0;
}
