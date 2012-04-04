/* Copyright 2012, Guilherme P. Gonçalves (guilherme.p.gonc@gmail.com)
 *
 * This file is part of ipod-syncer.
 *
 * ipod-syncer is free software: you can redistribute it and/or modify
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
 * along with ipod-syncer.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <glib.h>
#include <glib/gprintf.h>
#include <glib/gstdio.h>
#include <gpod/itdb.h>
#include <xmmsclient/xmmsclient.h>
#include <xmmsclient/xmmsclient-glib.h>

#ifdef VOICEOVER
    #include "voiceover.h"
#endif

#include "conversion.h"

#define DEFAULT_MOUNTPOINT "/media/IPOD"

/* Rudimentary logging */
#define LOG_MESSAGE(context, ...) \
    if (context->verbose) { \
        g_printf (__VA_ARGS__); \
    }

#define LOG_ERROR(...) \
    g_fprintf (stderr, __VA_ARGS__);

typedef struct {
    GMainLoop *mainloop;
    gboolean verbose;
#ifdef VOICEOVER
    gboolean voiceover;
#endif
    Itdb_iTunesDB *itdb;
    xmmsc_connection_t *connection;
} context_t;

static xmmsv_t *xmmsv_error_from_GError (const gchar *format, GError **err);
static void import_track_properties (Itdb_Track *track, xmmsv_t *properties);
static gchar *filepath_from_medialib_info (xmmsv_t *info);
static void remove_track (Itdb_Track *track, context_t *context);
static gboolean clear_tracks (context_t *context, GError **err);
static Itdb_Track *sync_track (xmmsv_t *idv, context_t *context, GError **err);
static xmmsv_t *sync_method (xmmsv_t *args, xmmsv_t *kwargs, void *udata);
static void run_query (const gchar *query, context_t *context);
static void setup_service (context_t *context);

/**
 * Build a xmmsv_t error from a GError.
 * Resets the GError.
 */
static xmmsv_t *
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
static void
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
static gchar *
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
static void
remove_track (Itdb_Track *track, context_t *context)
{
    GList *n;
    gchar *filepath;

    LOG_MESSAGE(context, "Deleting track %s\n", track->title);

    /* remove track from all playlists */
    for (n = context->itdb->playlists; n; n = g_list_next (n)) {
        itdb_playlist_remove_track ((Itdb_Playlist *) n->data, track);
    }

    filepath = itdb_filename_on_ipod (track);
    if (filepath) {
        g_remove (filepath);
        g_free (filepath);
    }

#ifdef VOICEOVER
    if (context->voiceover) {
        remove_voiceover (track);
    }
#endif

    itdb_track_remove (track);

    return;
}

/**
 * Remove all tracks from the iPod.
 * Playlists are kept, even if empty.
 */
static gboolean
clear_tracks (context_t *context, GError **err)
{
    GList *n, *next;

    for (n = context->itdb->tracks; n; n = next) {
        next = g_list_next (n);
        remove_track (n->data, context);
    }

    return itdb_write (context->itdb, err);
}

/**
 * Internal, sync a track given by its medialib id to the iPod.
 * Returns the newly created Itdb_Track, or NULL upon error.
 * It is the caller's responsibility to write the database back to the device.
 */
static Itdb_Track *
sync_track (xmmsv_t *idv, context_t *context, GError **err)
{
    gint32 id;
    xmmsc_result_t *res;
    xmmsv_t *properties;
    GError *tmp_err = NULL;

    Itdb_Track *track;
    Itdb_Playlist *mpl;

    gchar *filepath, *mp3path = NULL;

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

    LOG_MESSAGE (context, "Syncing track %s by %s\n", track->title, track->artist);

    itdb_track_add (context->itdb, track, -1);
    itdb_playlist_add_track (mpl, track, -1);

    filepath = filepath_from_medialib_info (properties);
    if (!filepath) {
        g_set_error_literal (&tmp_err, 0, 0, "can't determine path for track");
    } else if (!is_mp3 (filepath)) {
        LOG_MESSAGE (context, "  converting track to mp3\n");

        mp3path = convert_to_mp3 (filepath, &tmp_err);

        /* does nothing if tmp_err is NULL */
        g_prefix_error (&tmp_err, "conversion to mp3 failed. Reason: ");

        g_free (filepath);
        filepath = mp3path;
    }

    if (!tmp_err) {
        g_assert (filepath && track);
        itdb_cp_track_to_ipod (track, filepath, &tmp_err);

#ifdef VOICEOVER
        if (!tmp_err && context->voiceover) {
            LOG_MESSAGE (context, "  creating voiceover track\n");

            make_voiceover (track);
        }
#endif
    }

    if (tmp_err) {
        remove_track (track, context);
        track = NULL;
        g_propagate_error (err, tmp_err);
    }

    if (mp3path) {
        g_assert (filepath == mp3path);
        LOG_MESSAGE (context, "  removing temporary mp3 file\n");
        g_remove (mp3path);
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
static xmmsv_t *
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
static void
run_query (const gchar *query, context_t *context)
{
    xmmsv_t *idl, *err;
    xmmsc_result_t *res;
    xmmsv_coll_t *coll;
    const char *errstr;

    if (!xmmsv_coll_parse (query, &coll)) {
        LOG_ERROR ("Failed to parse query.\n");
        return;
    }

    res = xmmsc_coll_query_ids (context->connection, coll, NULL, 0, 0);
    xmmsc_result_wait (res);

    idl = xmmsc_result_get_value (res);
    if (xmmsv_get_error (idl, &errstr)) {
        LOG_ERROR ("Failed to get collection: %s\n", errstr);
    } else {
        if ((err = sync_method (idl, NULL, context))) {
            xmmsv_get_error (err, &errstr);
            LOG_ERROR ("%s\n", errstr);
        }
    }

    xmmsv_coll_unref (coll);
    xmmsc_result_unref (res);

    return;
}

/**
 * Set up a service for syncing tracks.
 */
static void
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
        LOG_ERROR ("Failed to parse options: %s\n", err->message);
        ret = 1;
        goto out;
    }

    if (!(service || argc > 1 || clear)) {
        LOG_ERROR ("Need either --service, --clear or a query string.\n");
        ret = 1;
        goto out;
    }

    context.connection = xmmsc_init ("ipod-syncer");
    if (!xmmsc_connect (context.connection, getenv ("XMMS_PATH"))) {
        LOG_ERROR ("Failed to connect to xmms2 daemon, leaving.\n");
        ret = 1;
        goto out;
    }

    context.itdb = itdb_parse (mountpoint, &err);
    if (!context.itdb) {
        LOG_ERROR ("Failed to parse iPod database: %s\n", err->message);
        ret = 1;
        goto out;
    }

#ifdef VOICEOVER
    context.voiceover = voiceover_init (mountpoint);
#endif

    if (clear && !clear_tracks (&context, &err)) {
        LOG_ERROR ("Failed to clear tracks: %s\n", err->message);
        ret = 1;
        goto out;
    }

    if (argc > 1) {
        query = g_strjoinv (" ", argv + 1);
        run_query (query, &context);
        g_free (query);
    }

    if (service) {
        /* FIXME: leaks */
        context.mainloop = g_main_loop_new (NULL, FALSE);
        xmmsc_mainloop_gmain_init (context.connection);
        setup_service (&context);
        g_main_loop_run (context.mainloop);
    }

out:
    g_free (mountpoint);
    if (err) { g_error_free (err); err = NULL; }

    if (optc) g_option_context_free (optc);
    if (context.connection) xmmsc_unref (context.connection);
    if (context.itdb) itdb_free (context.itdb);
#ifdef VOICEOVER
    if (context.voiceover) voiceover_deinit ();
#endif

    return ret;
}
