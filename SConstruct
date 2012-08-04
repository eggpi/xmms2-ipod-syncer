import os
import commands

SRCDIR = "src/"
INCLUDEDIR = os.path.join(SRCDIR, "include")

def CheckDeps():
    global env
    conf = Configure(env)

    # Override compilation settings with environment variables
    if "CC" in os.environ:
        conf.env.Replace(CC = os.environ["CC"])

    if "CFLAGS" in os.environ:
        conf.env.Append(CFLAGS = os.environ["CFLAGS"])

    if "LDFLAGS" in os.environ:
        conf.env.Append(LDFLAGS = os.environ["LDFLAGS"])

    LIBS = ["xmmsclient",
            "xmmsclient-glib",
            "glib-2.0",
            "gpod",
    ]

    # Basic check for libraries
    for l in LIBS:
        if not conf.CheckLib(l):
            print "ERROR: Can't find %s library" % l
            Exit(1)

    env.ParseConfig("pkg-config --cflags xmms2-client xmms2-client-glib glib-2.0")

    # pkg-config flags includes a whole lot more than we need for libgpod.
    # we are only really interested in the include dir for gpod itself
    fail, libgpod_CFLAGS = commands.getstatusoutput("pkg-config --cflags-only-I libgpod-1.0")
    if fail:
        print "ERROR: Can't find libgpod-1.0"
        Exit(1)

    for include in libgpod_CFLAGS.split():
        if "gpod-1.0" in include:
            env.Append(CFLAGS = include)

    conf.env["voiceover"] = env.GetOption("voiceover")
    if conf.env["voiceover"]:
        if not conf.CheckLib("espeak"):
            print "WARNING: Can't find libespeak. Building without Voiceover support"
            conf.env["voiceover"] = False
        else:
            print
            print "Trying to build with Voiceover support."
            print "I can't determine the path to the libespeak header file, so you'll"
            print "need to override the environment CFLAGS if it's not in the default"
            print "location."
            print

    env = conf.Finish()

AddOption("--without-voiceover",
          dest = "voiceover",
          action = "store_false",
          default = True,
          help = "Don't build voiceover support (default: false)")

env = Environment()
if not env.GetOption("clean"):
    CheckDeps()

if env.get("voiceover"):
    env.Append(CFLAGS = "-DVOICEOVER")

env.Append(CFLAGS = "-I" + INCLUDEDIR)

syncer_node = env.Object(os.path.join(SRCDIR, "ipod-syncer.c"))
conversion_node = env.Object(os.path.join(SRCDIR, "conversion.c"))

voiceover_node = []
if env.GetOption("clean") or env["voiceover"]:
    voiceover_node = env.Object(os.path.join(SRCDIR, "voiceover.c"))

env.Program("ipod-syncer", syncer_node + voiceover_node + conversion_node)
