/* SD Game Transfer - estructura compartida */
#ifndef SGT_APP_H
#define SGT_APP_H

#include <stdio.h>

#include <3ds.h>

#define SGT_PATH       512
#define SGT_MAX_ENTRIES 200
#define SGT_MAX_QUEUE   48
#define SGT_MAX_FILES   1024
#define SGT_MAX_TITLES  64

typedef struct {
    char name[256];
    int  is_dir;
    u64  size;
    int  selected;
} SgtEntry;

typedef struct {
    char src[SGT_PATH];
    char dst[SGT_PATH];
    u64  size;
} SgtFile;

typedef enum { JOB_WAIT = 0, JOB_RUN, JOB_OK, JOB_FAIL } SgtJobState;

typedef struct {
    char        src[SGT_PATH];
    char        dst[SGT_PATH];
    char        name[128];
    u64         bytes;
    u64         done;
    int         move;
    SgtJobState state;
    char        error[80];
} SgtJob;

typedef struct {
    SgtFile files[SGT_MAX_FILES];
    int     nfiles;
    u64     total;
    u64     done;
    int     idx;
    FILE   *in;
    FILE   *out;
    u64     left;
    int     move;
    char    root[SGT_PATH];
} SgtCopy;

typedef struct {
    u64  id;
    char code[16];
    u64  size;
    int  selected;
    int  media; /* 0 = instalado en la SD, 1 = cartucho */
} SgtTitle;

enum { SGT_MODE_FILES = 0, SGT_MODE_SAVES = 1 };
enum { SGT_ACTION_COPY = 0, SGT_ACTION_MOVE = 1 };
enum { SGT_CONFIRM_NONE = 0, SGT_CONFIRM_MOVE, SGT_CONFIRM_RESTORE, SGT_CONFIRM_START };

typedef struct {
    int mode;
    int action;

    char     cwd[SGT_PATH];
    SgtEntry entries[SGT_MAX_ENTRIES];
    int      nentries;
    int      cursor;
    int      top;

    SgtTitle titles[SGT_MAX_TITLES];
    int      ntitles;
    int      tcur;
    int      ttop;

    SgtJob jobs[SGT_MAX_QUEUE];
    int    njobs;
    int    running;
    int    job_cur;
    SgtCopy cp;

    u64 speed;
    u64 eta;
    u64 last_done;
    u64 last_ms;

    u64 sd_total, sd_free;
    u64 nand_total, nand_free;

    char status[160];
    u64  status_until;
    int  confirm;

    int sleep_off;
    int home_off;
    int dirty;
    int battery;
} SgtApp;

#endif
