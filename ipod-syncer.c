/* Copyright 2012, Guilherme P. Gonçalves (guilherme.p.gonc@gmail.com)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <glib.h>
#include <glib/gprintf.h>
#include <glib/gstdio.h>
#include <gpod/itdb.h>
#include <xmmsclient/xmmsclient.h>
#include <xmmsclient/xmmsclient-glib.h>

#define DEFAULT_MOUNTPOINT "/media/IPOD"

typedef struct {
    GMainLoop *mainloop;
    gboolean verbose;
    Itdb_iTunesDB *itdb;
    xmmsc_connection_t *connection;
} context_t;

/**
 * Build a xmmsv_t error from a GError.
 * Resets the GError.
 */
xmmsv_t *
xmmsv_error_from_GError (const gchar *format, GError **err)
{
    xmmsv_t *ret;
    gchar *errmsg;

    errmsg = g_strdup_printf (format, (*err)->message);
    ret = xmmsv_new_error (errmsg);

    g_free (errmsg);
    g_clear_error (err);

    return ret;
}

/**
 * Import track properties from the medialib into an Itdb_Track.
 */
void
import_track_properties (Itdb_Track *track, xmmsv_t *properties)
{
    /* Convenience macros -- extract keys from the properties dict
     * and set the corresponding field in the track object.
     */
    #define TRANSLATE_STRING_PROPERTY(name, key) \
        do { \
            const gchar *prop; \
            xmmsv_dict_entry_get_string (properties, key, &prop); \
            track->name = g_strdup (prop); \
        } while (0)

    #define IMPORT_STRING_PROPERTY(name) TRANSLATE_STRING_PROPERTY(name, #name)

    #define TRANSLATE_INT_PROPERTY(name, key) \
        xmmsv_dict_entry_get_int (properties, key, &track->name);

    #define IMPORT_INT_PROPERTY(name) TRANSLATE_INT_PROPERTY(name, #name)

    IMPORT_STRING_PROPERTY(title);
    IMPORT_STRING_PROPERTY(album);
    IMPORT_STRING_PROPERTY(artist);
    IMPORT_STRING_PROPERTY(genre);

    IMPORT_INT_PROPERTY(size);
    IMPORT_INT_PROPERTY(bitrate);

    TRANSLATE_INT_PROPERTY(tracklen, "duration");
    TRANSLATE_INT_PROPERTY(track_nr, "tracknr");

    return;
}

/**
 * Helper function to extract a file's path from its medialib info.
 */
gchar *
filepath_from_medialib_info (xmmsv_t *info)
{
    xmmsv_t *url;
    unsigned int len;
    gchar *decoded, *filepath;
    const unsigned char *buf;

    xmmsv_dict_get (info, "url", &url);
    url = xmmsv_decode_url (url);

    if (!xmmsv_get_bin (url, &buf, &len)) {
        return NULL;
    }

    decoded = g_strndup ((const gchar *) buf, len);

    filepath = g_filename_from_uri (decoded, NULL, NULL);
    g_free (decoded);

    return filepath;
}

/**
 * Remove a track from the iPod.
 * It is the caller's responsibility to write the database back
 * to the device after calling this function.
 */
void
remove_track (Itdb_Track *track, context_t *context)
{
    GList *n;
    gchar *filepath;

    if (context->verbose) {
        g_printf ("Deleting track %s\n", track->title);
    }

    /* remove track from all playlists */
    for (n = context->itdb->playlists; n; n = g_list_next (n)) {
        itdb_playlist_remove_track ((Itdb_Playlist *) n->data, track);
    }

    filepath = itdb_filename_on_ipod (track);
    if (filepath) {
        g_remove (filepath);
        g_free (filepath);
    }

    itdb_track_remove (track);

    return;
}

/**
 * Remove all tracks from the iPod.
 * Playlists are kept, even if empty.
 */
void
clear_tracks (context_t *context) {
    GList *n, *next;
    GError *err = NULL;

    for (n = context->itdb->tracks; n; n = next) {
        next = g_list_next (n);
        remove_track (n->data, context);
    }

    if (!itdb_write (context->itdb, &err)) {
        g_printf ("Can't write database: %s\n", err->message);
    }

    return;
}

/**
 * Internal, sync a track given by its medialib id to the iPod.
 * Returns the newly created Itdb_Track, or NULL upon error.
 * It is the caller's responsibility to write the database back to the device.
 */
Itdb_Track *
sync_track (xmmsv_t *idv, context_t *context, GError **err)
{
    gint32 id;
    xmmsc_result_t *res;
    xmmsv_t *properties;

    Itdb_Track *track;
    Itdb_Playlist *mpl;

    gchar *filepath;

    if (!xmmsv_get_int (idv, &id)) {
        g_set_error_literal (err, 0, 0, "can't parse track id");
        return NULL;
    } else if (id <= 0) {
        g_set_error_literal (err, 0, 0, "invalid track id");
        return NULL;
    }

    track = itdb_track_new ();
    mpl = itdb_playlist_mpl (context->itdb);

    res = xmmsc_medialib_get_info (context->connection, id);
    xmmsc_result_wait (res);
    properties = xmmsv_propdict_to_dict (xmmsc_result_get_value (res), NULL);

    import_track_properties (track, properties);

    if (context->verbose) {
        g_printf ("Syncing track %s by %s\n", track->title, track->artist);
    }

    itdb_track_add (context->itdb, track, -1);
    itdb_playlist_add_track (mpl, track, -1);

    filepath = filepath_from_medialib_info (properties);
    if (!filepath || !itdb_cp_track_to_ipod (track, filepath, err)) {
        remove_track (track, context);
        track = NULL;

        if (!filepath) {
            g_set_error_literal (err, 0, 0, "can't determine path for track");
        }
    }

    g_free (filepath);

    xmmsv_unref (properties);
    xmmsc_result_unref (res);

    return track;
}

/**
 * Sync medialib ids to the iPod.
 * Exported for other clients.
 * This function is atomic: either all or none of the tracks are synced.
 */
xmmsv_t *
sync_method (xmmsv_t *args, xmmsv_t *kwargs, void *udata)
{
    Itdb_Track *t;
    xmmsv_t *id;
    xmmsv_list_iter_t *it;
    GError *err = NULL;
    GList *n, *tracks = NULL;
    context_t *context = (context_t *) udata;

    xmmsv_get_list_iter (args, &it);
    while (xmmsv_list_iter_valid (it)) {
        xmmsv_list_iter_entry (it, &id);

        if (!(t = sync_track (id, context, &err))) {
            break;
        }

        tracks = g_list_prepend (tracks, t);
        xmmsv_list_iter_next (it);
    }

    if (!err && itdb_write (context->itdb, &err)) {
        return NULL;
    }

    /* Something went wrong -- remove all tracks we copied */
    for (n = tracks; n; n = g_list_next (n)) {
        remove_track ((Itdb_Track *) n->data, context);
    }

    return xmmsv_error_from_GError ("Sync failed: %s", &err);
}

/**
 * Run a collection query and sync the resulting ids.
 */
void
run_query (const gchar *query, context_t *context)
{
    xmmsv_t *idl, *err;
    xmmsc_result_t *res;
    xmmsv_coll_t *coll;
    const char *errstr;

    if (!xmmsv_coll_parse (query, &coll)) {
        g_printf ("Failed to parse query.\n");
        return;
    }

    res = xmmsc_coll_query_ids (context->connection, coll, NULL, 0, 0);
    xmmsc_result_wait (res);

    idl = xmmsc_result_get_value (res);
    if (xmmsv_get_error (idl, &errstr)) {
        g_printf ("Failed to get collection: %s\n", errstr);
    } else {
        if ((err = sync_method (idl, NULL, context))) {
            xmmsv_get_error (idl, &errstr);
            g_printf ("Failed to sync tracks: %s\n", errstr);
        }
    }

    xmmsv_coll_unref (coll);
    xmmsc_result_unref (res);

    return;
}

/**
 * Set up a service for syncing tracks.
 */
void
setup_service (context_t *context)
{
    xmmsc_sc_method_new_noargs (context->connection,
                                NULL,
                                sync_method,
                                "sync",
                                "Sync tracks to the iPod",
                                true,
                                false,
                                context);

    xmmsc_sc_setup (context->connection);
    return;
}

int
main(int argc, char **argv)
{
    guint ret = 0;
    GError *err = NULL;
    gboolean service = false, clear = false;
    gchar *mountpoint = g_strdup (DEFAULT_MOUNTPOINT), *query = NULL;
    context_t context = {0};

    GOptionContext *optc;
    GOptionEntry entries[] = {
        {"mountpoint", 'm', 0, G_OPTION_ARG_STRING, &mountpoint, "The mountpoint for the iPod. Default: " DEFAULT_MOUNTPOINT, NULL},
        {"service", 's', 0, G_OPTION_ARG_NONE, &service, "Run as a service.", NULL},
        {"verbose", 'v', 0, G_OPTION_ARG_NONE, &context.verbose, "Display more messages", NULL},
        {"clear", 0, 0, G_OPTION_ARG_NONE, &clear, "Remove all tracks in the iPod", NULL},
        {NULL}
    };

    optc = g_option_context_new ("- sync tracks from the medialib to an iPod");
    g_option_context_add_main_entries (optc, entries, NULL);

    if (!g_option_context_parse (optc, &argc, &argv, &err)) {
        g_printf ("Failed to parse options: %s\n", err->message);
        g_error_free (err); err = NULL;
        ret = 1;
        goto out;
    }

    if (!(service || argc > 1 || clear)) {
        g_printf ("Need either --service, --clear or a query string.\n");
        ret = 1;
        goto out;
    }

    context.connection = xmmsc_init ("ipod-syncer");
    if (!xmmsc_connect (context.connection, getenv ("XMMS_PATH"))) {
        g_printf ("Failed to connect to xmms2 daemon, leaving.\n");
        ret = 1;
        goto out;
    }

    context.itdb = itdb_parse (mountpoint, &err);
    if (!context.itdb) {
        g_printf ("Failed to parse iPod database: %s\n", err->message);
        g_error_free (err);
        ret = 1;
        goto out;
    }

    if (clear) {
        clear_tracks (&context);
    }

    if (argc > 1) {
        query = g_strjoinv (" ", argv + 1);
        run_query (query, &context);
        g_free (query);
    }

    if (service) {
        /* FIXME: leaks */
        context.mainloop = g_main_loop_new (NULL, FALSE);
        setup_service (&context);
        g_main_loop_run (context.mainloop);
    }

out:
    g_free (mountpoint);
    if (err) { g_error_free (err); err = NULL; }

    if (optc) g_option_context_free (optc);
    if (context.connection) xmmsc_unref (context.connection);
    if (context.itdb) itdb_free (context.itdb);

    return ret;
}
