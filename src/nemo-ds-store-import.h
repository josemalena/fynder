/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*-
 *
 * Importa los ajustes de carpeta que macOS guardó en el .DS_Store (tipo de
 * vista, columnas, orden, tamaño de íconos y posiciones) la primera vez que
 * se abre una carpeta en Fynder. Solo lee: nunca escribe en el .DS_Store.
 */

#ifndef NEMO_DS_STORE_IMPORT_H
#define NEMO_DS_STORE_IMPORT_H

#include <libnemo-private/nemo-file.h>

void nemo_ds_store_import (NemoFile *directory);

#endif /* NEMO_DS_STORE_IMPORT_H */
