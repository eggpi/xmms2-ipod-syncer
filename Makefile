INCLUDES=-I/usr/include/xmms2/ -I/usr/lib/glib-2.0/include -I/usr/include/glib-2.0 -I/usr/include/gpod-1.0/
LIBS=-lxmmsclient -lgpod -lglib-2.0

ipod-syncer: ipod-syncer.c
	$(CC) -g -Wall $(INCLUDES) $(LIBS) $(CFLAGS) ipod-syncer.c -o $@

clean:
	rm -f ipod-syncer
