#ifndef SGT_TRANSFER_H
#define SGT_TRANSFER_H

#include "app.h"

/* Espacio total/libre en bytes. 0 si no se pudo consultar. */
void sgt_storage(u64 *sd_total, u64 *sd_free, u64 *nand_total, u64 *nand_free);

/* Crea la carpeta de un archivo destino (todas las intermedias). */
void sgt_mkdirs(const char *file);

/* Recorre src (archivo o carpeta) y llena la lista de archivos a copiar.
 * dst es la ruta destino completa para ese elemento. */
int sgt_scan(const char *src, const char *dst, SgtFile *files, int max,
             int *nfiles, u64 *total, char *err);

/* Solo suma el tamano de un archivo o carpeta, sin listar nada. */
int sgt_measure(const char *src, u64 *bytes, char *err);

/* Copia un trozo y vuelve. 0 = sigue, 1 = termino, -1 = error. */
int sgt_copy_step(SgtCopy *c);

/* Borra un archivo o carpeta recursivamente. */
int sgt_remove_tree(const char *path);

#endif
