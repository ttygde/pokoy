VERSION		= 0.2.5
CC          ?= gcc
STRIP		?= strip
CFLAGS      = -std=c99 -D_DEFAULT_SOURCE -O2
LDFLAGS 	= -lxcb -lxcb-keysyms -lxcb-icccm -lxcb-screensaver

SRCR 		= pokoy.c
OBJS 		= ${SRCR:.c=.o}
EXEC		= pokoy

PREFIX		?= /usr/local

all: pokoy

.c.o:
	${CC} ${CFLAGS} -o $@ -c $<

pokoy: ${OBJS}
	${CC} ${OBJS} -o ${EXEC} ${LDFLAGS}
	${STRIP} -s ${EXEC}

clean:
	rm -f ./*.o ./${EXEC}

install:
	install -D -m 755 pokoy   ${DESTDIR}${PREFIX}/bin/pokoy
	install -D -m 644 pokoyrc ${DESTDIR}${PREFIX}/share/pokoy/pokoyrc
	install -D -m 644 pokoy.1 ${DESTDIR}${PREFIX}/share/man/man1/pokoy.1

uninstall:
	rm -f ${DESTDIR}${PREFIX}/bin/pokoy
	rm -f ${DESTDIR}${PREFIX}/share/pokoy/pokoyrc
	rm -f ${DESTDIR}${PREFIX}/share/man/man1/pokoy.1


.PHONY: all clean install
