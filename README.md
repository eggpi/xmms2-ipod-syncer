# xmms2-ipod-syncer

A simple service client for xmms2 that syncs tracks to an iPod device.

It is currently possible to copy the results of arbitrary xmms2 queries to an
iPod, and to clear all tracks in the device. For instance:

        $ ipod-syncer "artist:'The Beatles' AND NOT album:'Revolver'"

copies all songs from The Beatles, except for those in Revolver, to the iPod.

This client also supports Voiceover, that mildly cool feature in the iPod
Shuffle 3G where a synthesized voice speaks the metadata of a track, mainly to
compensate for the lack of a screen in the device's body.  The Voiceover tracks
are automatically created when you sync music to your iPod.

There is also experimental support for conversion of tracks. All tracks will be
converted to mp3 if needed before being synced to the iPod. Currently supported
input formats include ogg, flac and m4a.

This client uses the GNU GPL license.

## why?

- I like how xmms2 lets you make complex queries to your media library, and
  thought it would be nice to use these queries to select the songs I want to
  carry around;

- I implemented support for service clients in xmms2 during the 2011 Google
  Summer of Code, and this is the first non-trivial service client I have ever
  developed;

- My usecase is generally so simple I didn't even want to bother installing
  gtkpod or iTunes;

- I was bored on a rainy Sunday.

## how do I build it?

You will need libgpod and, of course, libxmmsclient.

For Voiceover support, you'll also need libespeak, which generally comes
installed with espeak.

For track conversion support, you will need vorbis-tools (ogg support), flac
(flac support), faad (m4a support) and ffmpeg (all other formats).

Finally, you will need the SCons build system. In order to build the client,
simply issue:

        $ scons

and everything should go fine. If you don't want Voiceover support, just add
the --without-voiceover option to the command above.

## how do I use it?

### as a standalone client

All you need to do is run the client with a query in the command line:

        $ ipod-syncer "album:'Strange Days'"

(beware of quoting issues with your shell).

There are a few other options, which you can read about using:

        $ ipod-syncer -h

### as a service client

The client currently exports a single method:

**sync (id1, id2, ...)**

        Sync tracks given by their medialib ids.

        Expects any nymber of positional arguments, all of which are medialib
        ids. Upon error, none of the tracks are synced.
        Returns NONE or ERROR.

Make sure you use the -s command line option, which tells the client to stick
around as a service after running the query (if any).

## issues?

Glad you asked! There are plenty.

- No playlist support;
- No way to selectively remove tracks;
- No checking if a track is already in the iPod before copying;
- Probably some memory leaks;
- Anything else that bugs you.

I do think the issues above should be fixed. However, my usecase currently
consists of:

        $ ipod-syncer --clear <query-that-resolves-to-handful-of-tracks>

so it is unlikely that I will be annoyed enough to work on them in the near
future.
