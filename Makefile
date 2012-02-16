ipod-syncer: ipod-syncer.o voiceover.o
	$(CC) -g -Wall $(shell pkg-config --libs glib-2.0 xmms2-client xmms2-client-glib) -lespeak -lgpod ipod-syncer.o voiceover.o -o ipod-syncer

ipod-syncer.o: ipod-syncer.c
	$(CC) -c -g -Wall $(shell pkg-config --cflags glib-2.0 xmms2-client) -I/usr/include/gpod-1.0/ ipod-syncer.c -o ipod-syncer.o

voiceover.o: voiceover.c
	$(CC) -c -g -Wall $(shell pkg-config --cflags glib-2.0) -I/usr/include/gpod-1.0/ voiceover.c -o voiceover.o

clean:
	rm -f *.o ipod-syncer
