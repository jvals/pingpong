CC=mpiicc
CFLAGS=-std=gnu99
TARGETS=pingpong
all: ${TARGETS}
clean:
	-rm -f ${TARGETS}
