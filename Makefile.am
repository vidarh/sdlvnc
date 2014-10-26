# Makefile.am for the SDL_vnc library/DLL

lib_LTLIBRARIES = libSDL_vnc.la

libSDL_vncincludedir = $(includedir)/SDL
libSDL_vncinclude_HEADERS =	\
	SDL_vnc.h

libSDL_vnc_la_SOURCES =		\
	SDL_vnc.c		\
	d3des.c

DEFS = @DEFS@ -DBUILD_DLL
libSDL_vnc_la_LDFLAGS = 	\
	-version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE)

DISTCLEANFILES = *~ *~c *~h *.cross.cache inc
