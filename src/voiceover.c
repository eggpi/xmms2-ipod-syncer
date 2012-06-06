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
 *
 * This file has been adapted from espeak in 2012, which is licensed under
 * the GNU General Public License version 3 and whose copyright notice is:
 *   Copyright (C) 2006 to 2007 by Jonathan Duddington
 *   email: jonsd@users.sourceforge.net
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <gpod/itdb.h>
#include <espeak/speak_lib.h>

#include "voiceover.h"

/* These are initialized along with espeak in voiceover_init.
 * Having espeak (de)initialize for every track makes it segfault,
 * so we only initialize it once and keep state in global variables.
 */
static gint samplerate;
static gchar *tracks_voiceoverd;

typedef struct {
    FILE *wavfile;
    gchar *wavpath;
} synth_context_t;

static void write_4_bytes (FILE *f, guint value);
static FILE *open_wav (gchar *path);
static void close_wav (FILE *wav);
static gint synth_cb (gshort *wav, gint numsamples, espeak_EVENT *events);
static gchar *voiceover_path (Itdb_Track *track, const gchar *voiceoverd);
static gchar *get_tracks_voiceover_dir (const gchar *mountpoint);

/**
 * Write 4 bytes into a file, from least to most significant.
 */
static void
write_4_bytes (FILE *f, guint value)
{
    guint i;

    for (i = 0; i < 4; i++) {
        fputc (value & 0xff, f);
        value = value >> 8;
    }

    return;
}

/**
 * Open a wav file.
 */
static FILE *
open_wav (gchar *path)
{
    const guchar wave_hdr[] = {
        'R', 'I', 'F', 'F', 0x24, 0xf0, 0xff, 0x7f, 'W', 'A', 'V', 'E', 'f',
        'm', 't', ' ', 0x10, 0, 0, 0, 1, 0, 1, 0, 9, 0x3d, 0, 0, 0x12, 0x7a,
        0, 0, 2, 0, 0x10, 0, 'd', 'a', 't', 'a', 0x00, 0xf0, 0xff, 0x7f
    };

    FILE *wav = NULL;

    if ((wav = fopen(path, "wb"))) {
        fwrite (wave_hdr, 1, 24, wav);
        write_4_bytes (wav, samplerate);
        write_4_bytes (wav, samplerate * 2);
        fwrite (&wave_hdr[32], 1, 12, wav);
    }

    return wav;
}

static void
close_wav (FILE *wav)
{
    guint pos;

    fflush (wav);

    pos = ftell (wav);

    fseek (wav, 4, SEEK_SET);
    write_4_bytes (wav, pos - 8);

    fseek (wav, 40, SEEK_SET);
    write_4_bytes (wav, pos - 44);

    fclose(wav);

    return;
}

static gint
synth_cb (gshort *wav, gint numsamples, espeak_EVENT *events)
{
    synth_context_t *ctx = (synth_context_t *) events[0].user_data;

    if (wav == NULL) {
        close_wav (ctx->wavfile);
        return 0;
    }

    if (!ctx->wavfile) {
        ctx->wavfile = open_wav (ctx->wavpath);
        if (!ctx->wavfile) {
            return 1;
        }
    }

    if (numsamples > 0) {
        fwrite (wav, numsamples * 2, 1, ctx->wavfile);
    }

    return 0;
}

/**
 * Build the path for a voiceover file for a track.
 */
static gchar *
voiceover_path (Itdb_Track *track, const gchar *voiceoverd)
{
    gchar *name, *path;

    g_return_val_if_fail (track->artist && track->title, NULL);

    name = g_strdup_printf ("%016lX.wav", track->dbid);
    path = g_build_filename (voiceoverd, name, NULL);

    g_free (name);
    return path;
}

/**
 * Returns the path to the device's track voiceover directory,
 * if it exists, or NULL.
 * The path must be free'd by the caller afterwards.
 */
static gchar *
get_tracks_voiceover_dir (const gchar *mountpoint)
{
    gchar *control, *voiceoverd;

    control = itdb_get_control_dir (mountpoint);
    voiceoverd = g_build_filename (control, "Speakable", "Tracks", NULL);

    if (!g_file_test (voiceoverd, G_FILE_TEST_IS_DIR)) {
        g_free (voiceoverd);
        voiceoverd = NULL;
    }

    g_free (control);

    return voiceoverd;
}

/**
 * Initialize voiceover support.
 * Returns false if the iPod doesn't support voiceover.
 */
gboolean
voiceover_init (const gchar *mountpoint)
{
    espeak_VOICE voice_props = {0};

    tracks_voiceoverd = get_tracks_voiceover_dir (mountpoint);
    g_return_val_if_fail (tracks_voiceoverd, FALSE);

    samplerate = espeak_Initialize (AUDIO_OUTPUT_SYNCHRONOUS, 0, NULL, 0);
    if (samplerate == EE_INTERNAL_ERROR) {
        g_free (tracks_voiceoverd);
        return FALSE;
    }

    /* try to get a young female voice with US accent */
    voice_props.languages = "en-us";
    voice_props.gender = 2;
    voice_props.age = 20;
    voice_props.variant = 0;
    espeak_SetVoiceByProperties (&voice_props);

    /* increase pitch and range to make robotic voice less scary */
    espeak_SetParameter (espeakPITCH, 70, 0);
    espeak_SetParameter (espeakRANGE, 80, 0);
    espeak_SetParameter (espeakWORDGAP, 1, 0);

    return TRUE;
}

/**
 * Free all resources related to voiceover support.
 */
void
voiceover_deinit (void)
{
    g_free (tracks_voiceoverd);
    espeak_Terminate ();

    return;
}

/**
 * Make a voiceover file for a track.
 */
gboolean
make_voiceover (Itdb_Track *track)
{
    gchar *text;
    synth_context_t ctx = {0};
    espeak_ERROR res = EE_INTERNAL_ERROR;

    g_return_val_if_fail (samplerate > 0 && tracks_voiceoverd, FALSE);

    ctx.wavpath = voiceover_path (track, tracks_voiceoverd);
    g_return_val_if_fail (ctx.wavpath, FALSE);

    espeak_SetSynthCallback (synth_cb);

    text = g_strdup_printf ("%s. %s.", track->artist, track->title);
    res = espeak_Synth (text, 0, 0, POS_SENTENCE, 0, espeakCHARS_AUTO, NULL, &ctx);

    g_free (text);
    g_free (ctx.wavpath);

    return res == EE_OK;
}

/**
 * Remove the voiceover file for a track.
 */
gboolean
remove_voiceover (Itdb_Track *track)
{
    gchar *path;

    path = voiceover_path (track, tracks_voiceoverd);
    g_return_val_if_fail (path, FALSE);

    return g_remove (path) == 0;
}
