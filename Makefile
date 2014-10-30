#ARCH=-m32
CFLAGS=-g -I. -DDEBUG -Wall -std=c11 -pedantic $(ARCH)
LDFLAGS=g -lSDL -lm $(ARCH)

test: d3des.o SDL_vnc.o
	gcc -g -o test SDL_vnc.o d3des.o -I . -lSDL -lm  Test/TestVNC.c $(ARCH)

d3des.o: d3des.c

SDL_vnc.o: SDL_vnc.c
