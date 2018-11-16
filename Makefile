all: pbtp-fw-writer

PREFIX:=/usr/local
BINDIR:=${PREFIX}/bin
CC:=c99

PBTP_FW_WRITER_SRC=pbtp-fw-writer.c
PBTP_FW_WRITER_OBJ=${PBTP_FW_WRITER_SRC:.c=.o}

HIDAPI_CFLAGS=$(shell pkg-config --cflags hidapi-libusb)
HIDAPI_LIBS=$(shell pkg-config --libs hidapi-libusb)

pbtp-fw-writer: ${PBTP_FW_WRITER_OBJ}
	${CC} -pedantic -Wall -o $@ ${PBTP_FW_WRITER_OBJ} ${LDFLAGS} ${HIDAPI_LIBS}

%.o : %.c
	${CC} -pedantic -Wall -D_XOPEN_SOURCE=500 ${CFLAGS} ${HIDAPI_CFLAGS} -c -o $@ $<

clean:
	${RM} ${PBTP_FW_WRITER_OBJ} pbtp-fw-writer

install: pbtp-fw-writer
	install -d ${DESTDIR}${BINDIR}
	install -m755 pbtp-fw-writer ${DESTDIR}${BINDIR}/

