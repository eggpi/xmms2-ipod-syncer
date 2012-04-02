#include <glib.h>
#include "conversion.h"

/**
 * Check whether a file is an mp3 file.
 */
gboolean
is_mp3 (const gchar *filepath)
{
    return g_str_has_suffix (filepath, ".mp3");
}

/**
 * Convert a file to mp3 format.
 * Returns the path to the converted mp3 file.
 */
gchar *
convert_to_mp3 (gchar *filepath, GError **err)
{
    gchar *mp3path;
    gint status;
    gchar *argv[] = { SCRIPTDIR "convert-2mp3.sh", filepath, NULL };

    g_spawn_sync (NULL,
                  argv,
                  NULL,
                  G_SPAWN_STDERR_TO_DEV_NULL,
                  NULL,
                  NULL,
                  &mp3path,
                  NULL,
                  &status,
                  err);

    if (status) {
        g_free (mp3path);
        mp3path = NULL;
    }

    return g_strchomp (mp3path);
}
