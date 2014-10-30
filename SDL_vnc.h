
/*

SDL_vnc.h - VNC client implementation

LGPL (c) A. Schiffler, aschiffler at ferzkopp dot net
Additions by B. Slawik, info at bernhardslawik dot de

*/

#ifndef _SDL_vnc_h
#define _SDL_vnc_h

#include <math.h>
#include <stdint.h>

/* Set up for C function definitions, even when using C++ */
#ifdef __cplusplus
extern "C" {
#endif

#define TRACE_LAST_ERROR
#ifdef TRACE_LAST_ERROR
// For external debugging purposes (see compiler switch "TRA
char vncLastError[512];
#endif


#if defined(WIN32) || defined(WIN64)
#include <SDL.h>
#include <SDL_thread.h>
#else
#include <SDL/SDL.h>
#include <SDL/SDL_thread.h>
#endif

	/* ----- Versioning */

#define SDL_VNC_MAJOR	1
#define SDL_VNC_MINOR	0
#define SDL_VNC_MICRO	2

	/* ---- Defines */

#define VNC_BUFSIZE	1024

	/* ---- VNC Protocol Structures */

    /* ---- helpers */
    typedef struct tSDL_vnc_rect {
        uint16_t x;
        uint16_t y;
        uint16_t width;
        uint16_t height;
    } tSDL_vnc_rect;


	/* ---- connection messages */

	typedef struct tSDL_vnc_pixelFormat {
		uint8_t bpp;
	    uint8_t depth;
		uint8_t bigendian;
		uint8_t truecolor;
		uint16_t redmax;
		uint16_t greenmax;
		uint16_t bluemax;
		uint8_t redshift;
		uint8_t greenshift;
		uint8_t blueshift;
		uint8_t padding[3];
	} tSDL_vnc_pixelFormat;
	
	typedef struct tSDL_vnc_serverFormat {
		uint16_t width;
		uint16_t height;
		tSDL_vnc_pixelFormat pixel_format;
		uint32_t namelength;
		uint8_t name[VNC_BUFSIZE];
	} tSDL_vnc_serverFormat;

	/* --- server messages --- */

	typedef struct tSDL_vnc_updateRequest {
		uint8_t messagetype;
		uint8_t incremental;
        tSDL_vnc_rect rect;
	} tSDL_vnc_updateRequest;

	typedef struct tSDL_vnc_serverMessage {
		uint8_t messagetype;
	} tSDL_vnc_serverMessage;

	typedef struct tSDL_vnc_serverUpdate {
		uint8_t padding;
		uint16_t rectangles;
	} tSDL_vnc_serverUpdate;

	typedef struct tSDL_vnc_serverRectangle {
        tSDL_vnc_rect rect;
		unsigned int encoding;
	} tSDL_vnc_serverRectangle;

	typedef struct tSDL_vnc_serverColormap {
		uint8_t padding;
		uint16_t first;
	    uint16_t number;
	} tSDL_vnc_serverColormap;

	typedef struct tSDL_vnc_serverText {
		uint8_t padding[3];
		uint32_t length;
	} tSDL_vnc_serverText;

	typedef struct tSDL_vnc_serverCopyrect {
	    uint16_t x;
        uint16_t y;
	} tSDL_vnc_serverCopyrect;

	typedef struct tSDL_vnc_serverRRE {
        uint32_t number;
        uint32_t background;
	} tSDL_vnc_serverRRE;

	typedef struct tSDL_vnc_serverRREdata {
		uint32_t color;
        tSDL_vnc_rect rect;
	} tSDL_vnc_serverRREdata;

	typedef struct tSDL_vnc_serverHextile {
		uint8_t mode;
	} tSDL_vnc_serverHextile;

	typedef struct tSDL_vnc_serverHextileBg {
		uint32_t color;
	} tSDL_vnc_serverHextileBg;

	typedef struct tSDL_vnc_serverHextileFg {
        uint32_t color;
	} tSDL_vnc_serverHextileFg;

	typedef struct tSDL_vnc_serverHextileSubrects {
		uint8_t number;
	} tSDL_vnc_serverHextileSubrects;

	typedef struct tSDL_vnc_serverHextileColored {
		uint32_t color;
		uint8_t xy;
		uint8_t wh;
	} tSDL_vnc_serverHextileColored;

	typedef struct tSDL_vnc_serverHextileRect {
		uint8_t xy;
		uint8_t wh;
	} tSDL_vnc_serverHextileRect;

	/* ---- client messages ---- */

	typedef struct tSDL_vnc_clientKeyevent {
		uint8_t messagetype;
		uint8_t downflag;
		uint8_t padding[2];
		uint32_t  key;
	} tSDL_vnc_clientKeyevent;

	typedef struct tSDL_vnc_clientPointerevent {
		uint8_t messagetype;
		uint8_t buttonmask;
		uint16_t x;
		uint16_t y;
	} tSDL_vnc_clientPointerevent;
	
	/* ---- main SDL_vnc structure ---- */

	typedef struct tSDL_vnc {
		int socket;				// socket to server
		int versionMajor;				// current VNC version
		int versionMinor;				// current VNC version
		unsigned int security_type;		// current security type
		tSDL_vnc_serverFormat serverFormat;	// current server format
		tSDL_vnc_updateRequest updateRequest;	// standard update request for full screen 
		
		int reading;				// flag indicating we are reading
		int framerate;				// current framerate for update requests
		int delay;					// Throttle down main thread (power saving)
		
		Uint32 rmask, gmask, bmask, amask;	// current RGBA mask
		
		
		SDL_Thread *thread;			// VNC client thread
		SDL_mutex *mutex;			// thread mutex
		
		// Variables below are accessed by the Thread
		// and need to be mutex locked if accessed externally
		
		unsigned char *buffer;				// general IO buffer
		
		char *clientbuffer;			// buffer for client-to-server data
		int clientbufferpos;			// current position in buffer
		
		int fbupdated;				// flag indicating that the framebuffer was updated
		SDL_Rect updatedRect;			// rectangle that was updated
		
		SDL_Surface *framebuffer;		// RGB surface of framebuffer
		SDL_Surface *scratchbuffer;		// workbuffer for encodings
		SDL_Surface *tilebuffer;		// workbuffer for encodings
		
		int gotcursor;				// flag indicating that the cursor was updated
		SDL_Surface *cursorbuffer;		// RGBA surface of cursor (fixed at 32x32)
		SDL_Rect cursorhotspot;			// hotspot location of cursor (only .x and .y are used)
		
	} tSDL_vnc;


	/* ---- Prototypes */

	/* ---- Function Prototypes */

#if defined(WIN32) || defined(WIN64)
#  if defined(BUILD_DLL) && !defined(LIBSDL_VNC_DLL_IMPORT)
#    define SDL_VNC_SCOPE __declspec(dllexport)
#  else
#    ifdef LIBSDL_VNC_DLL_IMPORT
#      define SDL_VNC_SCOPE __declspec(dllimport)
#    endif
#  endif
#endif
#ifndef SDL_VNC_SCOPE
#  define SDL_VNC_SCOPE extern
#endif



/* 
	Connect to VNC server 

	vnc  = pointer to tSDL_vnc structure
	host = hostname or hostip
	port = port
	mode = submode,submode,...
	submode =	raw | 
	copyrect | 
	rre | 
	hextile | 
	zrle(unimplemented) | 
	cursor(ignored) | 
	desktop(ignored)
	password = text
	framerate = 1 to 100

	*/

	SDL_VNC_SCOPE int vncConnect(tSDL_vnc *vnc, char *host, int port, char *mode, char *password, int framerate);



	/* 
	Blit current framebuffer to target

	Only blits if framebuffer exists and was updated. 
	Updated region is stored in urec (if not NULL).

	Returns 1 if the blit occured, 0 otherwise.
	*/

	SDL_VNC_SCOPE int vncBlitFramebuffer(tSDL_vnc *vnc, SDL_Surface *target, SDL_Rect *urec);
	SDL_VNC_SCOPE int vncBlitFramebufferAdvanced(tSDL_vnc *vnc, SDL_Surface *target, SDL_Rect *urec, int outx, int outy, float outScale, int fullRefresh);

	/*
	Blit current cursor to target
	
	Blitting is at the actual the cursor position.
	Returns 1 if blit occured, 0 otherwise 
	*/

	SDL_VNC_SCOPE int vncBlitCursor(tSDL_vnc *vnc, SDL_Surface *target, SDL_Rect *trec);



	/*
	Return cursor hotspot

	(Note: Only returned .x and .y are used.)
	*/

	SDL_VNC_SCOPE SDL_Rect vncCursorHotspot(tSDL_vnc *vnc);


	/*
	Send keyboard and pointer events to server
	*/
	SDL_VNC_SCOPE int vncClientKeyevent(tSDL_vnc *vnc, unsigned char downflag, unsigned int key);
	SDL_VNC_SCOPE int vncClientPointerevent(tSDL_vnc *vnc, unsigned char buttonmask, unsigned short x, unsigned short y);


	/* Disconnect from vnc server */

	SDL_VNC_SCOPE void vncDisconnect(tSDL_vnc *vnc);


	/* Ends C function definitions when using C++ */
#ifdef __cplusplus
};
#endif

#endif				/* _SDL_vnc_h */
