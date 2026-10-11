/* Progress of rclone uploads/downloads for the iCloud Drive mount.
 *
 * Reads the rclone remote-control API through a Unix socket
 * ($XDG_RUNTIME_DIR/rclone-icloud.sock, or $FYNDER_ICLOUD_RC), the way
 * icloud.sh starts the mount, and publishes the state to the sidebar. */

#ifndef NEMO_ICLOUD_PROGRESS_H
#define NEMO_ICLOUD_PROGRESS_H

#include <gtk/gtk.h>

#define NEMO_TYPE_ICLOUD_PROGRESS (nemo_icloud_progress_get_type ())
G_DECLARE_FINAL_TYPE (NemoICloudProgress, nemo_icloud_progress, NEMO, ICLOUD_PROGRESS, GObject)

/* Singleton. Starts polling on first use; emits "changed" on every new state. */
NemoICloudProgress *nemo_icloud_progress_get (void);

gboolean   nemo_icloud_progress_is_active  (NemoICloudProgress *self);
/* Pie icon for the current state, or NULL when nothing is transferring. */
GdkPixbuf *nemo_icloud_progress_get_pixbuf (NemoICloudProgress *self, int size);
void       nemo_icloud_progress_show_window (NemoICloudProgress *self, GtkWindow *parent);

#endif
