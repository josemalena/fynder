/* Etiquetas de color y comentarios de Finder guardados por macOS.
 *
 * Viven en atributos extendidos del archivo (com.apple.metadata:_kMDItemUserTags
 * y com.apple.metadata:kMDItemFinderComment). En discos que no los admiten (exFAT,
 * FAT, SMB) macOS los guarda en un archivo «._nombre» (AppleDouble) junto al
 * original; se leen de ahí. */

#ifndef NEMO_MAC_METADATA_H
#define NEMO_MAC_METADATA_H

#include <glib.h>

typedef struct {
	char *name;
	int   color; /* 0 ninguno, 1 gris, 2 verde, 3 morado, 4 azul, 5 amarillo, 6 rojo, 7 naranja */
} NemoMacTag;

/* Lista de NemoMacTag (liberar con nemo_mac_tags_free); NULL si no tiene. */
GList *nemo_mac_metadata_get_tags    (const char *path);
/* Comentario de Finder, o NULL. */
char  *nemo_mac_metadata_get_comment (const char *path);

void   nemo_mac_tags_free            (GList *tags);
/* Texto con markup de Pango: un punto del color de cada etiqueta y su nombre. */
char  *nemo_mac_tags_to_markup       (GList *tags);

#endif
