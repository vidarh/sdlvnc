#ARCH=-m32
DEBUG=#-DDEBUG
CFLAGS=-g -I. -Wall -std=c11 -pedantic $(ARCH) $(DEBUG)
LDFLAGS=g -lSDL -lm $(ARCH)

test: d3des.o SDL_vnc.o support.o
	gcc -g -o test SDL_vnc.o d3des.o support.o -I . -lSDL -lm  Test/TestVNC.c $(ARCH)

d3des.o: d3des.c

support.o: support.c

SDL_vnc.o: SDL_vnc.c
