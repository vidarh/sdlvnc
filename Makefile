
CFLAGS=-g -I. -DDEBUG -m32 -Wall -std=c11 -pedantic
LDFLAGS=g -lSDL -lm -m32

test: d3des.o SDL_vnc.o
	gcc -m32 -g -o test SDL_vnc.o d3des.o -I . -lSDL -lm  Test/TestVNC.c

d3des.o: d3des.c

SDL_vnc.o: SDL_vnc.c
