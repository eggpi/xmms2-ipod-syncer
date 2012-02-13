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
    g_error_free (*err);
    *err = NULL;

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
    g_remove (filepath);
    g_free (filepath);

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
 * Internal, sync a track given by its id to the iPod.
 * It is the caller's responsibility to write the database back to the device.
 */
xmmsv_t *
sync_id (xmmsv_t *idv, context_t *context)
{
    gint32 id;
    GError *err = NULL;
    xmmsc_result_t *res;
    xmmsv_t *properties, *ret = NULL;

    Itdb_Track *track;
    Itdb_Playlist *mpl;

    gchar *filepath;

    if (!xmmsv_get_int (idv, &id)) {
        ret = xmmsv_new_error ("can't parse track id");
    } else if (id <= 0) {
        ret = xmmsv_new_error ("invalid track id");
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
    if (!filepath) {
        ret = xmmsv_new_error ("can't determine path for track");
    } else if (!itdb_cp_track_to_ipod (track, filepath, &err)) {
        ret = xmmsv_error_from_GError ("can't copy track to iPod: %s", &err);
    }

    g_free (filepath);

    xmmsv_unref (properties);
    xmmsc_result_unref (res);

    return ret;
}

/**
 * Internal, sync tracks identified by a list of ids.
 */
xmmsv_t *
sync_idlist (xmmsv_t *idl, context_t *context)
{
    GError *err;
    xmmsv_t *v, *ret = NULL;
    xmmsv_list_iter_t *it;

    xmmsv_get_list_iter (idl, &it);
    while (xmmsv_list_iter_valid (it)) {
        xmmsv_list_iter_entry (it, &v);

        if ((v = sync_id (v, context))) {
            /* FIXME: nothing could ever go wrong! */
            xmmsv_unref (v);
        }

        xmmsv_list_iter_next (it);
    }

    if (!itdb_write (context->itdb, &err)) {
        ret = xmmsv_error_from_GError ("can't write data: %s", &err);
    }

    return ret;
}

/**
 * Sync a track given by its medialib id to the iPod.
 * Exported for other clients.
 */
xmmsv_t *
sync_id_method (xmmsv_t *args, xmmsv_t *kwargs, void *udata)
{
    GError *err;
    xmmsv_t *idv, *ret;
    context_t *context = (context_t *) udata;

    xmmsv_list_get (args, 0, &idv);
    ret = sync_id (idv, udata);

    /* sync_id doesn't write the contents of the db,
     * do it here if everything went well.
     */
    if (!ret && !itdb_write (context->itdb, &err)) {
        ret = xmmsv_error_from_GError ("can't write data: %s", &err);
    }

    return ret;
}

/**
 * Sync a list of medialib ids to the iPod.
 * Exported for other clients.
 */
xmmsv_t *
sync_idlist_method (xmmsv_t *args, xmmsv_t *kwargs, void *udata)
{
    xmmsv_t *idlist;

    xmmsv_list_get (args, 0, &idlist);
    return sync_idlist (idlist, udata);
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
    const gchar *errstr;

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
        err = sync_idlist (idl, context);
        if (err) {
            if (xmmsv_get_error (err, &errstr)) {
                g_printf ("Failed to sync tracks: %s\n", errstr);
            }

            xmmsv_unref (err);
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
    xmmsv_t *a, *args;

    a = xmmsv_sc_argument_new ("id",
                               "The track's medialib id",
                               XMMSV_TYPE_INT32,
                               NULL);
    args = xmmsv_build_list (a, XMMSV_LIST_END);

    xmmsc_sc_method_new_positional (context->connection,
                                    NULL,
                                    sync_id_method,
                                    "sync_id",
                                    "Sync a single track to the iPod",
                                    args,
                                    false,
                                    false,
                                    context);
    xmmsv_unref (args);

    a = xmmsv_sc_argument_new ("idlist",
                               "A list of medialib ids",
                               XMMSV_TYPE_LIST,
                               NULL);
    args = xmmsv_build_list (a, XMMSV_LIST_END);

    xmmsc_sc_method_new_positional (context->connection,
                                    NULL,
                                    sync_idlist_method,
                                    "sync_idlist",
                                    "Sync a list of ids to the iPod",
                                    args,
                                    false,
                                    false,
                                    context);
    xmmsv_unref (args);

    xmmsc_sc_setup (context->connection);
}

int main(int argc, char **argv) {
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
