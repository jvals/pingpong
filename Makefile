CC=mpiicc
CFLAGS=-std=c99
TARGETS=pingpong
all: ${TARGETS}
clean:
	-rm -f ${TARGETS}
