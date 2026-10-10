/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*-
 *
 * Lector (solo lectura) del .DS_Store de macOS: la base de datos oculta donde
 * el Finder guarda los ajustes de cada carpeta (tipo de vista, columnas,
 * orden, posiciones de los íconos...). Formato según la documentación de
 * Mark Mentovai y Wim Lewis, y la ingeniería inversa de Alastair Houghton.
 */

#ifndef NEMO_DS_STORE_H
#define NEMO_DS_STORE_H

#include <glib.h>

typedef struct _NemoDSStore NemoDSStore;

/* NULL si no existe o no se puede leer. */
NemoDSStore *nemo_ds_store_open  (const char *path);
void         nemo_ds_store_free  (NemoDSStore *store);

/* Entrada (nombre de archivo, código de 4 letras) -> valor.
 * El resultado es un GVariant flotante-ref (hay que unref):
 *   'bool' -> b, 'long'/'shor' -> i, 'comp'/'dutc' -> x, 'type' -> s (4 letras),
 *   'ustr' -> s, 'blob' -> ay (bytes crudos). NULL si no hay entrada. */
GVariant    *nemo_ds_store_lookup (NemoDSStore *store, const char *filename, const char *code);

/* Los 'blob' que son plist binarios (bwsp, lsvp, icvp...) ya convertidos a GVariant
 * (diccionarios a{sv}, listas av, enteros x, reales d, booleanos b, cadenas s, datos ay). */
GVariant    *nemo_ds_store_lookup_plist (NemoDSStore *store, const char *filename, const char *code);

/* Posición del ícono de un archivo ('Iloc'): TRUE si hay. */
gboolean     nemo_ds_store_get_icon_location (NemoDSStore *store, const char *filename, int *x, int *y);

/* Nombres de archivo que tienen alguna entrada (sin repetir, sin "."). */
GPtrArray   *nemo_ds_store_list_files (NemoDSStore *store);

/* Convierte un plist binario (bplist00) en GVariant; NULL si no es válido. */
GVariant    *nemo_bplist_parse (const guchar *data, gsize length);

#endif /* NEMO_DS_STORE_H */
