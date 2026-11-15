#ifndef SGT_SAVES_H
#define SGT_SAVES_H

#include "app.h"

/* Lista los titulos instalados en la SD. Devuelve cuantos hay o -1. */
int sgt_save_list(SgtTitle *out, int max);

/* Copia la partida del titulo a dst_root (carpeta). 0 ok. */
int sgt_save_backup(u64 title_id, const char *dst_root, char *err);

/* Restaura la partida desde src_root. 0 ok. */
int sgt_save_restore(u64 title_id, const char *src_root, char *err);

#endif
