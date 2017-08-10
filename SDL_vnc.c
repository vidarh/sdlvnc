/*

SDL_vnc.c - VNC client implementation

LGPL (c) A. Schiffler, aschiffler at ferzkopp dot net
Additions by B. Slawik, info at bernhardslawik dot de
Butchered by Vidar Hokstad, vidar@hokstad.com

*/

#if defined(WIN32) || defined(WIN64)
 #define _CRT_SECURE_NO_DEPRECATE
 #define _CRT_NONSTDC_NO_DEPRECATE
 #include <windows.h>

 /* Define for strncasecmp */
 #define strncasecmp(s1, s2, n)	strnicmp(s1, s2, n)

 /* Prototype for inet_pton */
 int inet_pton(int af, const char *src, void *dst);
#else
 #include <sys/select.h>
 #include <unistd.h>
 #include <sys/socket.h>
 #include <netinet/in.h>
 #include <arpa/inet.h>

 // For alternative "gethostbyname" and "hostent"
 #include <netdb.h>
#endif

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>

#include "SDL_vnc.h"
#include "SDL_vnc_private.h"
#include "d3des.h"

// FIXME: Currently these need to be larger than maximum used buffer
#define RAWBUFFER_WIDTH 1920
#define RAWBUFFER_HEIGHT 1024

/* From support.c */
void blit_raw(tSDL_vnc * vnc, tSDL_vnc_rect rect);

/* Endian dependent routines/data */

#if SDL_BYTEORDER == SDL_BIG_ENDIAN
#define swap_16(x) (x)
	#define swap_32(x) (x)
	unsigned char bitfield[8]={1,2,4,8,16,32,64,128};
#else
	#define swap_16(x) ((((x) & 0xff) << 8) | (((x) >> 8) & 0xff))
	#define swap_32(x) (((x) >> 24) | (((x) & 0x00ff0000) >> 8)  | (((x) & 0x0000ff00) << 8)  | ((x) << 24))
	unsigned char bitfield[8]={128,64,32,16,8,4,2,1};
#endif

/* Define this to generate lots of info while the library is running. */
//#define DEBUG
#define TRACE_LAST_ERROR

#ifdef DEBUG
	#define DBMESSAGE 	printf
#else
	#define DBMESSAGE(...)	//
#endif

#ifdef TRACE_LAST_ERROR
	#define DBERROR 	traceError
	// Debug functionality
	void traceError(const char * format, ...) {
		va_list args;
		va_start(args, format);
		vsprintf(vncLastError, format, args);
		printf(">>> Error: "); puts(vncLastError);
		va_end(args);
	}
#else
	#define DBERROR 	printf(">>> Error: "); printf
#endif

#define MAX(a,b) (((a) > (b)) ? (a) : (b))
#define MIN(a,b) (((a) < (b)) ? (a) : (b))
#define ARRSIZE(a) (sizeof(a)/sizeof(a[0]))

#define CHECKED_READ(vnc, dest, len, message) { \
    int result = Recv(vnc->socket, dest, len, 0); \
    if (result!=len) { \
    printf("Error reading %s. Got %i of %i bytes.\n", message, result, len); \
    return 0; \
    } \
    }

typedef enum ClientMsgType {
	CMSG_PIXELFORMAT  = 0,
	CMSG_SETENCODINGS = 2,
	CMSG_FBUPDATEREQ  = 3,
	CMSG_KEYEVENT     = 4,
	CMSG_POINTEREVENT = 5,
	CMSG_CLIENTCUTTXT = 6,
} ClientMsgType;

typedef enum VNCEncoding {
	ENC_RAW = 0,
	ENC_COPYRECT = 1,
	ENC_RRE = 2,
	ENC_HEXTILE = 5,
	ENC_TRLE = 15,
	ENC_ZRLE = 16,
	ENC_CURSOR = -239,
	ENC_DESKTOP = -223,
} VNCEncoding;

char *strdup(const char *s);

static int WaitForMessage(tSDL_vnc *vnc, unsigned int usecs)
{
	fd_set fds;
	struct timeval timeout;
	int result;
	
	timeout.tv_sec=0;
	timeout.tv_usec=usecs;
	FD_ZERO(&fds);
	FD_SET(vnc->socket,&fds);
	result=select(vnc->socket+1, &fds, NULL, NULL, &timeout);
#ifdef DEBUG
	if (result<0) {
		DBMESSAGE("Waiting for message failed: %d (%s)\n",errno,strerror(errno));
	}
#endif
	
	return result;
}

int Recv(int s, void *buf, size_t len, int flags)
{
	unsigned char *target=buf;
	size_t to_read=len;
	int result;

	while (to_read>0) {
		result = recv(s,target,to_read,flags);
		if (result<0) return result;
		if (result==0) return (len-to_read);
		to_read -= result;
		target += result;
	}

	return len ;
}

void GrowUpdateRegion(tSDL_vnc *vnc, SDL_Rect *trec)
{
	if (vnc->fbupdated) {
		Sint16 left,top,right,bot;
		SDL_Rect *srec = &vnc->updatedRect;

		/* Adjust bounds for growth */
		top=MIN(srec->y, trec->y);
		left=MIN(srec->x, trec->x);
		bot=MAX(srec->y+srec->h, trec->y + trec->h);
		right=MAX(srec->x+srec->w, trec->x + trec->w);

		srec->y=top;
		srec->x=left;
		srec->h=bot-top;
		srec->w=right-left;
	} else {
		/* Initialize update rectangle */
		vnc->updatedRect=*trec;
		vnc->fbupdated=1;
	}
}


static int handleHextile(tSDL_vnc *vnc) {
    DBMESSAGE("Hextile encoding.\n");
    //
    if (!(vnc->tilebuffer)) {
        // Create new tilebuffer
        vnc->tilebuffer = SDL_CreateRGBSurface(SDL_SWSURFACE,16,16,32,vnc->rmask,vnc->gmask,vnc->bmask,0);
        if (vnc->tilebuffer) {
            SDL_SetAlpha(vnc->tilebuffer,0,0);
            DBMESSAGE("Created new tilebuffer.\n");
        } else {
            DBERROR("Error creating tilebuffer.\n");
            return 0;
        }
    }
    return 1;
}



static int read_security_type(tSDL_vnc *vnc) {
    if (vnc->versionMinor < 7) {
        // Read security type (simple)
        CHECKED_READ(vnc, vnc->buffer, 4, "security type");
        vnc->security_type=vnc->buffer[3];
        DBMESSAGE("Security type (read): %i\n", vnc->security_type);
        return 1;
    }

    // Addition for RFB 003 008

    CHECKED_READ(vnc, vnc->buffer, 1, "security type");

    // Security Type List! Receive number of supported Security Types
    int nSecTypes = vnc->buffer[0];
    if (nSecTypes == 0) {
        DBERROR("Server offered an empty list of security types.\n");
        return 0;
    }
        
    // Receive Security Type List (Buffer overflow possible!)
    int result = Recv(vnc->socket,vnc->buffer,nSecTypes,0);
        
    // Find supported one...
    vnc->security_type = 0;
    int i;
    for (i = 0; i < result; i++) {
        vnc->security_type = vnc->buffer[i];
        // Break if supported type (currently 1 or 2) found
        if ((vnc->security_type == 1) || (vnc->security_type == 2)) break;
    }
    
    // Select it
    DBMESSAGE("Security type (select): %i\n", vnc->security_type);
    vnc->buffer[0] = vnc->security_type;
        
    result = send(vnc->socket,vnc->buffer,1,0);
    if (result != 1) {
        DBERROR("Write error on security type selection.\n");
        return 0;
    }

    return 1;
}


/* FIXME: Is this valid when we never request a non-truecolor display? */
static int HandleServerMessage_colormap(tSDL_vnc * vnc)
{
	tSDL_vnc_serverColormap serverColormap;
    DBMESSAGE("Message: colormap\n");
    // Read data, but ignore it
    CHECKED_READ(vnc, &serverColormap, 5, "server colormap");

    serverColormap.first=swap_16(serverColormap.first);
    serverColormap.number=swap_16(serverColormap.number);

    DBMESSAGE("Server colormap first color: %u\n",serverColormap.first);
    DBMESSAGE("Server colormap number: %u\n",serverColormap.number);

    while (serverColormap.number>0) {
        CHECKED_READ(vnc, &vnc->buffer, 6, "server colormap color");
        DBMESSAGE("Got color %u.\n",serverColormap.first);
        serverColormap.first++;
        serverColormap.number--;
    }
    return 1;
}

void vnc_to_sdl_rect(tSDL_vnc_rect * src, SDL_Rect * dest)
{
    dest->x = src->x;
    dest->y = src->y;
    dest->w = src->width;
    dest->h = src->height;
}

static inline void vnc_hextile_to_sdl_rect(uint8_t xy, uint8_t wh, SDL_Rect * dest)
{
    dest->x=(xy >> 4) & 0x0f;
    dest->y=xy & 0x0f;
    dest->w=((wh >> 4) & 0x0f)+1;
    dest->h=(wh & 0x0f)+1;
}


static inline void vnc_rect_swap(tSDL_vnc_rect * rect)
{
    rect->x = swap_16(rect->x);
    rect->y = swap_16(rect->y);
    rect->width  = swap_16(rect->width);
    rect->height = swap_16(rect->height);
}

static void blit_scratch(tSDL_vnc * vnc, tSDL_vnc_rect rect)
{
    SDL_Rect trec;
    vnc_to_sdl_rect(&rect, &trec);
    SDL_LockMutex(vnc->mutex);
    SDL_BlitSurface(vnc->scratchbuffer, NULL, vnc->framebuffer, &trec);
    GrowUpdateRegion(vnc,&trec);
    SDL_UnlockMutex(vnc->mutex);
}


static int ServerRectangle_Raw(tSDL_vnc * vnc,
                               tSDL_vnc_rect serverRectangle)
{
    DBMESSAGE("RAW encoding.\n");

    if (serverRectangle.width > RAWBUFFER_WIDTH ||
        serverRectangle.height > RAWBUFFER_HEIGHT) {
        DBERROR("Oops. Too large for buffer. Should fall back on old temporary buffer here");
    }

    if (serverRectangle.width == vnc->framebuffer->pitch / 4) {
        if (read_raw(vnc, serverRectangle) == 0) return 0;
    } else {
        int bytes_to_read = serverRectangle.width*serverRectangle.height*4;
        CHECKED_READ(vnc, (unsigned char *)vnc->rawbuffer, bytes_to_read, "pixel data");
        DBMESSAGE("Blitting %i bytes of raw pixel data.\n",bytes_to_read);
        blit_raw(vnc,serverRectangle);
        DBMESSAGE("Blitted raw pixel data.\n");
    }
    return 1;
}


static int ServerRectangle_CopyRect(tSDL_vnc * vnc,
                               tSDL_vnc_rect serverRectangle)
{
    DBMESSAGE("CopyRect encoding.\n");

	tSDL_vnc_serverCopyrect serverCopyrect;
    CHECKED_READ(vnc, &serverCopyrect, 4, "copyrect");

    SDL_Rect trec, srec;
    srec.x=swap_16(serverCopyrect.x);
    srec.y=swap_16(serverCopyrect.y);
    DBMESSAGE("Copyrect from %u,%u\n",srec.x,srec.y);
    srec.w=serverRectangle.width;
    srec.h=serverRectangle.height;
    vnc_to_sdl_rect(&serverRectangle,&trec);
    
    SDL_LockMutex(vnc->mutex);
    SDL_BlitSurface(vnc->framebuffer, &srec, vnc->scratchbuffer, NULL);
    SDL_BlitSurface(vnc->scratchbuffer, NULL, vnc->framebuffer, &trec);
    GrowUpdateRegion(vnc,&trec);
    SDL_UnlockMutex(vnc->mutex);
    DBMESSAGE("Blitted copyrect pixels.\n");
    return 1;
}



static int ServerRectangle_Cursor(tSDL_vnc * vnc,
                                  tSDL_vnc_rect rect)
{
    DBMESSAGE("CURSOR pseudo-encoding.\n");

    vnc->cursorhotspot.x = rect.x;
    vnc->cursorhotspot.y = rect.y;

    int bytes_to_read = rect.width*rect.height*4;
    
    CHECKED_READ(vnc, (unsigned char *)vnc->scratchbuffer->pixels,bytes_to_read, "cursor data");
    DBMESSAGE("Read cursor pixel data %u byte.\n",bytes_to_read);

    // Mask data
    bytes_to_read = (unsigned int)floor((rect.width+7.0)/8.0)*rect.height;
    unsigned char *cursormask=(unsigned char *)malloc(bytes_to_read);
    if (!cursormask) {
        DBERROR("Could not allocate cursor mask.\n");
        return 0;
    }

    CHECKED_READ(vnc, (unsigned char *)cursormask,bytes_to_read, "cursor mask");
    DBMESSAGE("Read cursor mask data %u byte.\n",bytes_to_read);

    // Blit data into cursor image
    SDL_LockMutex(vnc->mutex);
    SDL_BlitSurface(vnc->scratchbuffer,NULL,vnc->cursorbuffer,NULL);
    // Process mask into alpha of cursor image
    unsigned char * target=(unsigned char *)vnc->cursorbuffer->pixels;
    target=target + 3;
    int byteindex=0;
    int cy, cx;
    int bitindex;
    for (cy=0; cy<rect.height; cy++) {
        for (cx=0; cx<rect.width; cx++) {
            bitindex=cx % 8;
            *target = (cursormask[byteindex] & bitfield[bitindex]) ? 255 : 0;
            if (bitindex==7) byteindex++;
            target += 4;
        } // cx loop
        if (bitindex<7) byteindex++;
    } // cy loop
    free(cursormask);
    SDL_UnlockMutex(vnc->mutex);
    return 1;
}



static int ServerRectangle_RRE(tSDL_vnc * vnc,
                               tSDL_vnc_rect rect)
{
	tSDL_vnc_serverRRE serverRRE;
	tSDL_vnc_serverRREdata serverRREdata;
    DBMESSAGE("RRE encoding.\n");
    CHECKED_READ(vnc, &serverRRE, 8, "RRE header");
    serverRRE.number=swap_32(serverRRE.number);

    DBMESSAGE("RRE of %u rectangles. Background color 0x%06x\n",serverRRE.number,serverRRE.background);

    SDL_FillRect(vnc->scratchbuffer, NULL, serverRRE.background);
    /* Draw subrectangles */
    unsigned int num_subrectangles=0;
    SDL_Rect srec;
    while (num_subrectangles<serverRRE.number) {
        num_subrectangles++;
        CHECKED_READ(vnc, &serverRREdata, 12, "RRE data");
        vnc_rect_swap(&serverRREdata.rect);
        vnc_to_sdl_rect(&serverRREdata.rect,&srec);
        serverRREdata.color = serverRREdata.color;
        SDL_FillRect(vnc->scratchbuffer,&srec,serverRREdata.color);
    }
    DBMESSAGE("Drawn %i subrectangles.\n", num_subrectangles);
    blit_scratch(vnc, rect);
    DBMESSAGE("Blitted RRE pixels.\n");
    return 1;
}


static int ServerRectangle_HexTile(tSDL_vnc * vnc,
                               tSDL_vnc_rect serverRectangle)
{
	SDL_Rect trec, srec;
    int bx,by,hx,hy;
    if (handleHextile(vnc) == 0) return 0;
    //
    // Iterate over all tiles
    // row loop
    for (hy=0; hy<serverRectangle.height; hy += 16) {
        // Determine height of tile
        if ((hy+16)>serverRectangle.height) {
            by=serverRectangle.height % 16;
        } else {
            by=16;
        }
        // column loop
        for (hx=0; hx<serverRectangle.width; hx += 16) {
            // Determine width of tile
            if ((hx+16)>serverRectangle.width) {
                bx = serverRectangle.width % 16;
            } else {
                bx = 16;
            }
            tSDL_vnc_serverHextile serverHextile;
            CHECKED_READ(vnc, &serverHextile,1, "hextile header");

            if (serverHextile.mode & 1) {
                // Read raw data for tile in lines
                int bytes_to_read = bx*by*4;
                int result = 0;
                if ((bx == 16) && (by == 16)) {
                    // complete tile
                    result = Recv(vnc->socket,(unsigned char *)vnc->tilebuffer->pixels,bytes_to_read,0);
                } else {
                    // partial tile
                    unsigned char * target =(unsigned char *)vnc->tilebuffer->pixels;
                    int rowindex=by;
                    while (rowindex) {
                        result += Recv(vnc->socket,target,bx*4,0);
                        target += 16*4;
                        rowindex--;
                    }
                }
                if (result !=bytes_to_read) {
                    DBERROR("Error on pixel data. Got %i of %i bytes.\n",result,bytes_to_read);
                    return 0;
                }
                trec.x=hx;
                trec.y=hy;
                trec.w=16;
                trec.h=16;
                SDL_BlitSurface(vnc->tilebuffer, NULL, vnc->scratchbuffer, &trec);
            } else {
                tSDL_vnc_serverHextileBg serverHextileBg;
                tSDL_vnc_serverHextileFg serverHextileFg;
                
                // no raw data
                if (serverHextile.mode & 2) {
                    CHECKED_READ(vnc, &serverHextileBg, 4, "hextile background");
                }
                SDL_FillRect(vnc->tilebuffer,NULL,serverHextileBg.color);
                if (serverHextile.mode & 4) {
                    CHECKED_READ(vnc, &serverHextileFg, 4, "hextile foreground");
                }
                if (serverHextile.mode & 8) {
                    tSDL_vnc_serverHextileSubrects serverHextileSubrects;
                    CHECKED_READ(vnc, &serverHextileSubrects, 1, "hextile subrects");
                    // Read subrects
                    int num_subrectangles=0;
                    while (num_subrectangles<serverHextileSubrects.number) {
                        num_subrectangles++;
                        // Check color mode
                        if (serverHextile.mode & 16) {
                            tSDL_vnc_serverHextileColored serverHextileColored;
                            
                            // Colored subrect
                            CHECKED_READ(vnc, &serverHextileColored,6, "hextile color subrect data");
                            vnc_hextile_to_sdl_rect(serverHextileColored.xy, serverHextileColored.wh, &srec);
                            SDL_FillRect(vnc->tilebuffer,&srec,serverHextileColored.color);
                        } else {
                            // Non-colored Subrect
                            tSDL_vnc_serverHextileRect serverHextileRect;
                            CHECKED_READ(vnc, &serverHextileRect, 2, "hextile subrect data");
                                // Render colored subrect
                            vnc_hextile_to_sdl_rect(serverHextileRect.xy, serverHextileRect.wh, &srec);
                            SDL_FillRect(vnc->tilebuffer,&srec,serverHextileFg.color);
                        } // color mode check
                    } // subrect loop
                    //
                } // have subrects
                // Draw tile
                trec.x=hx;
                trec.y=hy;
                trec.w=16;
                trec.h=16;
                SDL_BlitSurface(vnc->tilebuffer, NULL, vnc->scratchbuffer, &trec);
            } // raw data csheck
        } // hx loop
    } // hy loop
    //
    blit_scratch(vnc,serverRectangle);
    DBMESSAGE("Blitted Hextile pixels.\n");
    return 1;
}


int ReadServerRectangle(tSDL_vnc * vnc,
                        tSDL_vnc_serverRectangle * serverRectangle)
{
    int result = Recv(vnc->socket,serverRectangle,12,0);
    if (result!=12) return 0;

    vnc_rect_swap(&serverRectangle->rect);
    serverRectangle->encoding=swap_32(serverRectangle->encoding);

    DBMESSAGE("    @ %u,%u size %u,%u encoding %u\n",serverRectangle->rect.x,serverRectangle->rect.y,serverRectangle->rect.width,serverRectangle->rect.height,serverRectangle->encoding);
    
    /* Sanity check values */
    if (serverRectangle->rect.x > vnc->serverFormat->width) {
        DBMESSAGE("Bad rectangle: x=%u setting to 0\n",serverRectangle->rect.x);
        serverRectangle->rect.x=0;
    }
    if (serverRectangle->rect.y > vnc->serverFormat->height) {
        DBMESSAGE("Bad rectangle: y=%u setting to 0\n",serverRectangle->rect.y);
        serverRectangle->rect.y=0;
    }
    if ((serverRectangle->rect.width<=0) || (serverRectangle->rect.width>vnc->serverFormat->width)) {
        DBMESSAGE("Bad rectangle: width=%u setting to 1\n",serverRectangle->rect.width);
        serverRectangle->rect.width=1;
    }
    if ((serverRectangle->rect.height<=0) || (serverRectangle->rect.height>vnc->serverFormat->height)) {
        DBMESSAGE("Bad rectangle: height=%u setting to 1\n",serverRectangle->rect.height);
        serverRectangle->rect.height=1;
    }
    return 1;
}


static int PrepScratchBuffer(tSDL_vnc *vnc, 
                             tSDL_vnc_rect serverRectangle)
{
    // Do we have a scratchbuffer
    if (vnc->scratchbuffer) {
        /* Check size */
        // FIXME: This seems very wasteful. width and height of scratchbuffer
        // is used, but should check if any benefits to not continuously redoing this.
        // Or at least keep a few common sizes.
        if ((vnc->scratchbuffer->w != serverRectangle.width) || 
            (vnc->scratchbuffer->h != serverRectangle.height)) {

            DBMESSAGE("Deleting existing scratchbuffer. w=%d,h=%d\n", vnc->scratchbuffer->w, vnc->scratchbuffer->h);
            SDL_FreeSurface(vnc->scratchbuffer);
            vnc->scratchbuffer=NULL;
        }
    }
    if (!(vnc->scratchbuffer)) {
        // Create new scratchbuffer
        vnc->scratchbuffer = SDL_CreateRGBSurface(SDL_SWSURFACE,serverRectangle.width,serverRectangle.height,32,vnc->rmask,vnc->gmask,vnc->bmask,0);
        if (vnc->scratchbuffer) {
            SDL_SetAlpha(vnc->scratchbuffer,0,0);
            DBMESSAGE("Created new scratchbuffer w=%d,h=%d\n", serverRectangle.width, serverRectangle.height);
        } else {
            DBERROR("Error creating scratchbuffer.\n");
            return 0;
        }
    }

    return 1;
}


static int HandleServerMessage_update(tSDL_vnc *vnc)
{
    DBMESSAGE("Message: update\n");
	tSDL_vnc_serverUpdate serverUpdate;
    CHECKED_READ(vnc, &serverUpdate, 3, "server update");

    /* ??? Protocol sais U16, TightVNC server sends U8 */
    serverUpdate.rectangles=serverUpdate.rectangles & 0x00ff;
    DBMESSAGE("Number of rectangles: %u (%04x)\n",serverUpdate.rectangles,serverUpdate.rectangles);
    
    int num_rectangles=0;
    while (num_rectangles<serverUpdate.rectangles) {
        num_rectangles++;
        DBMESSAGE("Rectangle %i of %i:\n",num_rectangles,serverUpdate.rectangles);
        tSDL_vnc_serverRectangle serverRectangle;
        if (ReadServerRectangle(vnc, &serverRectangle) == 0) {
            DBERROR("Read error on server rectangle.\n");
            return 0;
        }

        /* Rectangle Data */
        switch (serverRectangle.encoding) {
        case ENC_RAW:
            if (ServerRectangle_Raw(vnc, serverRectangle.rect) == 0) return 0;
            break;
        case ENC_COPYRECT:
            if (PrepScratchBuffer(vnc, serverRectangle.rect) == 0) return 0;
            if (ServerRectangle_CopyRect(vnc, serverRectangle.rect) == 0) return 0;
            break;
        case ENC_RRE:
            if (PrepScratchBuffer(vnc, serverRectangle.rect) == 0) return 0;
            if (ServerRectangle_RRE(vnc, serverRectangle.rect) == 0) return 0;
            break;
        case ENC_HEXTILE:
            if (PrepScratchBuffer(vnc, serverRectangle.rect) == 0) return 0;
            if (ServerRectangle_HexTile(vnc, serverRectangle.rect) == 0) return 0;
            break;
        case ENC_ZRLE:
            DBERROR("ZRLE encoding - ignored.\n");
            return 0;
            break;
            
        case ENC_CURSOR:
            if (ServerRectangle_Cursor(vnc, serverRectangle.rect) == 0) return 0;
            break;
            
        case ENC_DESKTOP:
            DBMESSAGE("DESKTOP pseudo-encoding (ignored).\n");
            break;
            
        }
    } // while
    return 1;
}




static int HandleServerMessage_text(tSDL_vnc *vnc) 
{
    DBMESSAGE("Message: text\n");
	tSDL_vnc_serverText serverText;

    CHECKED_READ(vnc, &serverText,5, "text");
    serverText.length=swap_32(serverText.length);

    DBMESSAGE("Server text length: %u\n",serverText.length);
    // ??? Protocol sais U16 is length to read
    // TightVNC server sends a byte on empty string
    if (serverText.length==0) {
            serverText.length=1;
    }
    while (serverText.length>0) {
        int result = Recv(vnc->socket,vnc->buffer,serverText.length % VNC_BUFSIZE,0);
        if (result <= 0) {
            serverText.length=0;
        } else {
            DBMESSAGE("Read %i bytes of text.\n",result);
            serverText.length -= result;
        }
    }
    return 0;
}



static int HandleServerMessage(tSDL_vnc *vnc)
{
	tSDL_vnc_serverMessage serverMessage;
	DBMESSAGE("HandleServerMessage\n");
	CHECKED_READ(vnc, &serverMessage, 1, "server message");

    switch (serverMessage.messagetype) {
    case 0:
        if (HandleServerMessage_update(vnc) == 0) return 0;
        break;

    case 1:
        if (HandleServerMessage_colormap(vnc) == 0) return 0;
        break;

    case 2:
        DBMESSAGE("Message: bell - ignored\n");
        // we are done reading
        break;

    case 3:
        if (HandleServerMessage_text(vnc) == 0) return 0;
        break;
        
    default:
        DBERROR("Unknown message error: message=%u\n",serverMessage.messagetype);
        return 0;
        break;
    } // switch messagetype

	return 1;
}


int HandleClientMessage(tSDL_vnc *vnc) {
	SDL_LockMutex(vnc->mutex);
	if (vnc->clientbufferpos>0) {
	}
	SDL_UnlockMutex(vnc->mutex);
	return 0;
}

int vncClientThread (void *data) {
	tSDL_vnc *vnc = (tSDL_vnc *)data;
	unsigned int usvalue;
	int result;

	// Set framerate
	DBMESSAGE("vncClientThread: Started, Polling updates at rate %iHz.\n",vnc->framerate);
	usvalue = (unsigned int)1000000 / vnc->framerate;

	// Processing loop
	vnc->reading = 1;
	while (vnc->reading) {
		//DBMESSAGE("vncClientThread: WaitForMessage...\n");
		
		if (vnc->delay > 0) {
			// Throttle down... Needed for power saving.
			SDL_Delay(vnc->delay);
		}
		
		if ((result = WaitForMessage(vnc,usvalue))<=0) {
			// Client Messages
			SDL_LockMutex(vnc->mutex);
			if (vnc->clientbufferpos>0) {
				result = send(vnc->socket,vnc->clientbuffer,vnc->clientbufferpos,0);
				if (result==vnc->clientbufferpos) {
					DBMESSAGE("vncClientThread: Client-to-Server data: %u bytes send\n",result);
				} else {
					DBERROR("vncClientThread: Write error on client-to-server data.\n");
					vnc->reading=0;
				}
				vnc->clientbufferpos=0;
			}
			SDL_UnlockMutex(vnc->mutex);
			
			// Framebuffer update request
			//DBMESSAGE("vncClientThread: Sending Update Request...\n",result);
			result = send(vnc->socket,(const char *)&vnc->updateRequest,10,0);
			if (result==10) {
				//DBMESSAGE("vncClientThread: Incremental Framebuffer Update Request: send\n");
			} else {
				DBERROR("vncClientThread: Write error on update request.\n");
				vnc->reading=0;
			}
		} else {
			//DBMESSAGE("vncClientThread: HandleServerMessage()...\n");
			vnc->reading = HandleServerMessage(vnc);
		}
	}

	DBMESSAGE("vncClientThread: VNC client thread done.\n");
	return 0;


}


// ================



static int vncReadServerFormat(tSDL_vnc *vnc) {
    // Server Initialiazation
    int result = Recv(vnc->socket,&vnc->serverFormat,24,0);
    if (result==24) {
        // Swap format numbers
        vnc->serverFormat->width      =swap_16(vnc->serverFormat->width);
        vnc->serverFormat->height     =swap_16(vnc->serverFormat->height);
        vnc->serverFormat->pixel_format.redmax     =swap_16(vnc->serverFormat->pixel_format.redmax);
        vnc->serverFormat->pixel_format.greenmax   =swap_16(vnc->serverFormat->pixel_format.greenmax);
        vnc->serverFormat->pixel_format.bluemax    =swap_16(vnc->serverFormat->pixel_format.bluemax);
        vnc->serverFormat->namelength =swap_32(vnc->serverFormat->namelength);
        // Info
        DBMESSAGE("Format Width: %u (0x%04x)\n",vnc->serverFormat->width,vnc->serverFormat->width);
        DBMESSAGE("Format Height: %u (0x%04x)\n",vnc->serverFormat->height,vnc->serverFormat->height);
        DBMESSAGE("Format Pixel bpp: %u\n",vnc->serverFormat->pixel_format.bpp);
        DBMESSAGE("Format Pixel depth: %u\n",vnc->serverFormat->pixel_format.depth);
        DBMESSAGE("Format Pixel big endian: %u\n",vnc->serverFormat->pixel_format.bigendian);
        DBMESSAGE("Format Pixel true color: %u\n",vnc->serverFormat->pixel_format.truecolor);
        DBMESSAGE("Format Pixel R max: %u\n",vnc->serverFormat->pixel_format.redmax);
        DBMESSAGE("Format Pixel G max: %u\n",vnc->serverFormat->pixel_format.greenmax);
        DBMESSAGE("Format Pixel B max: %u\n",vnc->serverFormat->pixel_format.bluemax);
        DBMESSAGE("Format Pixel R shift: %u\n",vnc->serverFormat->pixel_format.redshift);
        DBMESSAGE("Format Pixel G shift: %u\n",vnc->serverFormat->pixel_format.greenshift);
        DBMESSAGE("Format Pixel B shift: %u\n",vnc->serverFormat->pixel_format.blueshift);
        DBMESSAGE("Format Name Length: %u (0x%08x)\n",vnc->serverFormat->namelength,vnc->serverFormat->namelength);
    } else {
        DBERROR("Read error in server info (%i)\n", result);
        return 0;
    }

	
    
    // Desktop Name
    if (vnc->serverFormat->namelength>(VNC_BUFSIZE-1)) {
        DBERROR("Desktop name too long: %i\n",vnc->serverFormat->namelength);
        return 0;
    }
    if (vnc->serverFormat->namelength>1) {
        result = Recv(vnc->socket,vnc->serverFormat->name,vnc->serverFormat->namelength,0);
        if (result==vnc->serverFormat->namelength) {
            vnc->serverFormat->name[vnc->serverFormat->namelength]=0;
            DBMESSAGE("Desktop name: %s\n",vnc->serverFormat->name);
        } else {
            DBERROR("Read error on desktop name.\n");
            return 0;
        }
    } else {
        DBMESSAGE("No desktop name.\n");
    }
    
    return 1;
}

static int initVNC(tSDL_vnc *vnc, int framerate) {
	// Initialize variables
	if (!(vnc->buffer = calloc(VNC_BUFSIZE, 1))) {
		DBERROR("Out of memory allocating workbuffer.\n");
		return 0;
	}

	if (!(vnc->clientbuffer = calloc(VNC_BUFSIZE, 1))) {
		free(vnc->buffer);
		DBERROR("Out of memory allocating clientbuffer.\n");
		return 0;
	}
	vnc->framebuffer=NULL;
	vnc->scratchbuffer=NULL;
	vnc->tilebuffer=NULL;
	vnc->cursorbuffer=NULL;

	DBMESSAGE("Allocating %ld bytes for rawbuffer\n", RAWBUFFER_WIDTH * 4 * RAWBUFFER_HEIGHT);
	if(!(vnc->rawbuffer = malloc(RAWBUFFER_WIDTH * 4 * RAWBUFFER_HEIGHT))) {
		free(vnc->clientbuffer);
		free(vnc->buffer);
		DBERROR("Out of memory allocating rawbuffer.\n");
		return 0;
	}

	if(!(vnc->serverFormat = calloc(sizeof(*vnc->serverFormat), 1))) {
		free(vnc->rawbuffer);
		free(vnc->clientbuffer);
		free(vnc->buffer);
		DBERROR("Out of memory allocating serverFormat.\n");
		return 0;
	}

	if(!(vnc->updateRequest = calloc(sizeof(*vnc->updateRequest), 1))) {
		free(vnc->serverFormat);
		free(vnc->clientbuffer);
		free(vnc->buffer);
		free(vnc->rawbuffer);
		DBERROR("Out of memory allocating serverFormat.\n");
		return 0;
	}

	vnc->fbupdated=0;
	vnc->gotcursor=0;
	vnc->mutex=SDL_CreateMutex();
	vnc->thread=NULL;
	vnc->clientbufferpos=0;
	vnc->delay=0;

	// Set framerate
	if (framerate<1) {
		vnc->framerate=1;
	} else if (framerate>100) {
		vnc->framerate=100;
	} else {
		vnc->framerate=framerate;
	}

	return 1;
}

static int connectSocket(char *host, int port) {
	struct sockaddr_in address;
	struct hostent *he;
	struct in_addr **addr_list;
	int sock;
	// Connect
	DBMESSAGE("Creating socket...");
	if ((sock = socket(AF_INET,SOCK_STREAM,0)) == -1) {
		DBERROR("Could not create socket.\n");
		return -1;
	}
	DBMESSAGE("Converting address...\n");
	address.sin_family = AF_INET;
	address.sin_port = htons(port);
	if (inet_pton(AF_INET,host,&address.sin_addr) != 1) {
		DBMESSAGE("Given IP [%s] could not be parsed. Trying to resolve it as a hostname...\n", host);

		// Resolve
		if ((he = gethostbyname(host)) == NULL) {  // get the host info
			DBERROR("Error: gethostbyname has had a bad day...\n");
			return -1;
		}
		//DBMESSAGE("Official name is: %s\n", he->h_name);
		DBMESSAGE("IP addresses: ");
		addr_list = (struct in_addr **)he->h_addr_list;
		{
			int i;
			for (i = 0; addr_list[i] != NULL; i++) {
				DBMESSAGE("[%s] ", inet_ntoa(*addr_list[i]));
			}
			DBMESSAGE("\n");
			if (i == 0) {
				DBERROR("Error: No applicable IP found.\n");
				return -1;
			} else {
				address.sin_addr = *addr_list[0];
			}
		}
	}

	// Connect to server
	DBMESSAGE("Connecting socket...");
	if (connect(sock,(struct sockaddr *)&address,sizeof(address)) == -1) {
		DBERROR("Could not connect to server %s:%i\n",host,port);
		return -1;
	}
	DBMESSAGE("The connection was accepted with the server %s...\n",inet_ntoa(address.sin_addr));

	return sock;
}

static int handshakeVersion(tSDL_vnc *vnc) {
	int sent, recvd;

	recvd = Recv(vnc->socket,vnc->buffer,12,0);
	if (recvd!=12) {
		DBERROR("Read error on server version.\n");
		return 0;
	}
	vnc->buffer[12]=0;
	DBMESSAGE("Server Version: %s",vnc->buffer);

	// Check major version 3
	if (vnc->buffer[6]!='3') {
		DBERROR("Major version mismatch. Expected 3.\n");
		return 0;
	}
	vnc->versionMajor = 3;
	vnc->versionMinor = vnc->buffer[10]-'0';
	DBMESSAGE("3.x, Minor Version: %i\n",vnc->versionMinor);

	// Send same version back
	sent = send(vnc->socket,vnc->buffer,12,0);
	if (sent!=12) {
		DBERROR("Write error on version echo.\n");
		return 0;
	}

	return 1;
}

static int handshakeSecurity(tSDL_vnc *vnc, const char *password) {
	unsigned int security_result;
	char security_key[8];
	char security_challenge[16];
	char security_response[16];
	int sent, recvd;

	if (read_security_type(vnc) == 0) return 0;

	// Check type
	if ((vnc->security_type < 1) || (vnc->security_type > 2)) {
		DBERROR("Security: Invalid.\n");
		return 0;
	}
	if (vnc->security_type == 1) {
		DBMESSAGE("Security: None.\n");
	}
	if (vnc->security_type == 2) {
		DBMESSAGE("Security: VNC Authentication\n");

		// Security Handshaking
		recvd = Recv(vnc->socket,&security_challenge,16,0);
		if (recvd!=16) {
			DBERROR("Read error on security handshaking.\n");
			return 0;
		}
		DBMESSAGE("Security Challenge: received\n");

		// Calculate response
		strncpy(security_key,password,8);
		deskey((unsigned char*)security_key,EN0);
		des((unsigned char*)security_challenge,(unsigned char*)security_response);
		des((unsigned char*)&security_challenge[8],(unsigned char*)&security_response[8]);

		// Send response
		sent = send(vnc->socket,security_response,16,0);
		if (sent!=16) {
			DBERROR("Write error on security response.\n");
			return 0;
		}
		DBMESSAGE("Security Response: sent\n");

		// Security Result
		recvd = Recv(vnc->socket,vnc->buffer,4,0);
		if (recvd!=4) {
			DBERROR("Read error on security result.\n");
			return 0;
		}
		security_result=vnc->buffer[0];

		DBMESSAGE("Security Result: %i", security_result);

		// Check result
		if (security_result!=0) {
			DBERROR("Could not authenticate\n");
			return 0;
		}

	}
	return 1;
}

static int negotiatePixels(tSDL_vnc *vnc) {
	tSDL_vnc_pixelFormat *pixel_format;
	int sent;

	if (vncReadServerFormat(vnc) == 0) return 0;

	// Set pixel format
	memset(vnc->buffer,0,20);
	vnc->buffer[0]=0;
	pixel_format = (void*) &vnc->buffer[4]; // Map pixel format over buffer
	pixel_format->bpp=32;
	pixel_format->depth=32;
	pixel_format->bigendian=0;
	pixel_format->truecolor=1;
	pixel_format->redmax=swap_16(255);
	pixel_format->greenmax=swap_16(255);
	pixel_format->bluemax=swap_16(255);

	/* FIXME: These depends on endianness; current values below works for
	   little endian */
	pixel_format->redshift=16; // Was 0, which doesn't match vnc->rmask
	pixel_format->greenshift=8;
	pixel_format->blueshift=0; // Was 16, which doesn't match vnc->bmask
	sent = send(vnc->socket,vnc->buffer,20,0);
	if (sent == 20) {
		DBMESSAGE("Pixel format set.\n");
	} else {
		DBERROR("Error setting pixel format.\n");
		return(0);
	}
	return 1;
}

struct EncName {
	char *name;
	VNCEncoding enc;
};

const struct EncName str_enc[] = {
	{     "raw", ENC_RAW},
	{"copyrect", ENC_COPYRECT},
	{     "rre", ENC_RRE},
	{ "hextile", ENC_HEXTILE},
	{    "zrle", ENC_ZRLE},
	{  "cursor", ENC_CURSOR},
	{ "desktop", ENC_DESKTOP},
};

static VNCEncoding nameToEnc(const char *name) {

	for (int i = 0; i < ARRSIZE(str_enc); i++) {
		if (strcasecmp(str_enc[i].name, name)==0) {
			return str_enc[i].enc;
		}
	}
	return -1;
}

struct vnc_set_encodings_t {
	uint8_t  type;
	uint8_t  padding;
	uint16_t count;
	 int32_t encodings[];
};

static void hton_set_encodings(struct vnc_set_encodings_t *e)
{
	int i;
	for (i = 0; i < e->count; i++) {
		e->encodings[i] = htonl((uint32_t) e->encodings[i]);
	}
	e->count = htons(e->count);
}

static int negotiateEncodings(tSDL_vnc *vnc, const char *modes) {
	int sent;
	struct vnc_set_encodings_t *packet = (void*) vnc->buffer;
	size_t psize;

	packet->type = CMSG_SETENCODINGS;
	packet->padding = 0;
	packet->count = 0;

	DBMESSAGE("Requesting modes %s\n", modes);
	for (; modes && *modes; packet->count++) {
		VNCEncoding enc = nameToEnc(modes);
		if (enc == -1) {
			DBERROR("Unknown encoding.\n");
			return 0;
		}

		packet->encodings[packet->count] = enc;

		if ((modes = strchr(modes,',')))
			modes++;
	}
	psize = sizeof(*packet) + packet->count*sizeof(packet->encodings[0]);

	hton_set_encodings(packet);
	sent = send(vnc->socket, packet, psize, 0);
	if (sent != psize) {
		DBERROR("Write error on mode request.\n");
		return 0;
	}

	DBMESSAGE("Mode request sent\n");
	return 1;
}

static int postInit(tSDL_vnc *vnc) {
	// Create framebuffer
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
	DBMESSAGE("Client is: big-endian\n");
	vnc->rmask = 0xff000000;
	vnc->gmask = 0x00ff0000;
	vnc->bmask = 0x0000ff00;
	vnc->amask = 0x000000ff;
#else
	// Pre
	DBMESSAGE("Client is: little-endian\n");
	//@FIXME: Strange... Palm Pre needs reversed R <-> B order! Maybe check "if(SDL_BYTEORDER == SDL_LIL_ENDIAN)"
	vnc->rmask = 0x00ff0000;
	vnc->gmask = 0x0000ff00;
	vnc->bmask = 0x000000ff;
	vnc->amask = 0xff000000;

#endif
	vnc->framebuffer = SDL_CreateRGBSurface(SDL_SWSURFACE,vnc->serverFormat->width,vnc->serverFormat->height,32,vnc->rmask,vnc->gmask,vnc->bmask,0);
	if (!vnc->framebuffer) {
		DBERROR("Could not create framebuffer.\n");
		return 0;
	}
	SDL_SetAlpha(vnc->framebuffer,0,0);
	DBMESSAGE("Framebuffer created.\n");

	// Initial fb update flag is whole screen
	vnc->fbupdated=0;
	vnc->updatedRect.x=0;
	vnc->updatedRect.y=0;
	vnc->updatedRect.w=vnc->serverFormat->width;
	vnc->updatedRect.h=vnc->serverFormat->height;

	// Create 32x32 cursorbuffer (with alpha)
	vnc->cursorbuffer = SDL_CreateRGBSurface(SDL_SWSURFACE,32,32,32,vnc->rmask,vnc->gmask,vnc->bmask,vnc->amask);
	SDL_SetAlpha(vnc->cursorbuffer,SDL_SRCALPHA,0);
	if (vnc->cursorbuffer==NULL) {
		DBERROR("Could not create cursorbuffer.\n");
		return 0;
	} else {
		DBMESSAGE("Cursorbuffer created.\n");
	}

	// Create standard update request
	vnc->updateRequest->messagetype = 3;
	vnc->updateRequest->incremental = 0;
	vnc->updateRequest->rect.x=0;
	vnc->updateRequest->rect.y=0;
	vnc->updateRequest->rect.width=vnc->serverFormat->width;
	vnc->updateRequest->rect.height=vnc->serverFormat->height;
	vnc_rect_swap(&vnc->updateRequest->rect);

	return 1;
}


int vncConnect(tSDL_vnc *vnc, char *host, int port, char *mode, char *password, int framerate) {
	int sent;

	if (!initVNC(vnc, framerate))
		return 0;

	if((vnc->socket = connectSocket(host, port)) == -1)
		return 0;

	if (!handshakeVersion(vnc))
		return 0;

	if (!handshakeSecurity(vnc, password))
		return 0;

	// Send Client Initialization
	vnc->buffer[0]=1;
	sent = send(vnc->socket,vnc->buffer,1,0);
	if (sent==1) {
		DBMESSAGE("Client Initialization: shared\n");
	} else {
		DBERROR("Write error on client initialization.\n");
		return 0;
	}

	if (!negotiatePixels(vnc))
		return 0;

	if (!negotiateEncodings(vnc, mode))
		return 0;


	if (!postInit(vnc))
		return 0;


	// Initial framebuffer update request
	sent = send(vnc->socket,&vnc->updateRequest,10,0);
	if (sent==10) {
		DBMESSAGE("Initial Framebuffer Update Request: send\n");
	} else {
		DBERROR("Write error on initial update request.\n");
		return 0;
	}

	// Modify update request for incremental updates
	vnc->updateRequest->incremental = 1;

	// Start client thread
	DBMESSAGE("Starting Thread...\n");
	vnc->thread =  SDL_CreateThread(vncClientThread, vnc);
	return 1;
}

int vncBlitFramebuffer(tSDL_vnc *vnc, SDL_Surface *target, SDL_Rect *urec) {
	int result;

	if (!vnc) return 0;
	if (!vnc->mutex) return 0;
	if (!vnc->framebuffer) return 0;

	result = 0;
	SDL_LockMutex(vnc->mutex);
	if (vnc->fbupdated) {
		DBMESSAGE("Blitting framebuffer: updated region @ %i,%i size %ix%i\n",vnc->updatedRect.x,vnc->updatedRect.y,vnc->updatedRect.w,vnc->updatedRect.h);
		SDL_BlitSurface(vnc->framebuffer, &vnc->updatedRect, target, &vnc->updatedRect);
		//  SDL_BlitSurface(vnc->framebuffer, NULL, target, trec);
		if (urec) {
			*urec=vnc->updatedRect;
		}
		vnc->fbupdated=0;
		result=1;
	}
	SDL_UnlockMutex(vnc->mutex);
	return result;
}

// Advanced blitting, especially for full-screen and scrolling updates
int vncBlitFramebufferAdvanced(tSDL_vnc *vnc, SDL_Surface *target, SDL_Rect *urec, int outx, int outy, float outScale, int fullRefresh) {
	int result;

	if (!vnc) return 0;
	if (!vnc->mutex) return 0;
	if (!vnc->framebuffer) return 0;

	result = 0;
	SDL_LockMutex(vnc->mutex);
	if ((fullRefresh > 0) || vnc->fbupdated) {
		DBMESSAGE("Blitting framebuffer: updated region @ %i,%i size %ix%i\n",vnc->updatedRect.x,vnc->updatedRect.y,vnc->updatedRect.w,vnc->updatedRect.h);
		
		if (fullRefresh > 0) {
			SDL_Rect srcrec;
			SDL_Rect dstrec;
			int w = target->w;
			int h = target->h;
			
			// Clear
			SDL_FillRect(target,NULL,0);
			
			if (outx > 0) {
				srcrec.x = 0;
				dstrec.x = outx;
				srcrec.w = w - outx;
				dstrec.w = w - outx;
			} else {
				srcrec.x = -outx;
				dstrec.x = 0;
				srcrec.w = w - outx;
				dstrec.w = w - outx;
			}
			
			if (outy > 0) {
				srcrec.y = 0;
				dstrec.y = outy;
				srcrec.h = h - outy;
				dstrec.h = h - outy;
			} else {
				srcrec.y = -outy;
				dstrec.y = 0;
				srcrec.h = h - outy;
				dstrec.h = h - outy;
			}
			SDL_BlitSurface(vnc->framebuffer, &srcrec, target, &dstrec);
			
		} else {
			// Incremental update
			SDL_Rect dstrec;
			dstrec.x = vnc->updatedRect.x + outx;
			dstrec.y = vnc->updatedRect.y + outy;
			dstrec.w = (Uint16)(vnc->updatedRect.w * outScale);
			dstrec.h = (Uint16)(vnc->updatedRect.h * outScale);
			SDL_BlitSurface(vnc->framebuffer, &vnc->updatedRect, target, &dstrec);
		}
		
		if (urec) {
			*urec=vnc->updatedRect;
		}
		vnc->fbupdated=0;
		result=1;
	}
	SDL_UnlockMutex(vnc->mutex);
	return result;
}

int vncBlitCursor(tSDL_vnc *vnc, SDL_Surface *target, SDL_Rect *trec) {
	int result;

	if (!vnc) return 0;
	if (!vnc->mutex) return 0;

	result=0;
	SDL_LockMutex(vnc->mutex);
	if ((vnc->cursorbuffer) && (vnc->gotcursor)) {
		SDL_BlitSurface(vnc->cursorbuffer, NULL, target, trec);
		result=1;
	}
	SDL_UnlockMutex(vnc->mutex);
	return result;
}

SDL_Rect vncCursorHotspot(tSDL_vnc *vnc)
{
	SDL_Rect apos;
	apos.h=0;
	apos.w=0;
	apos.x=0;
	apos.y=0;

	if ((!vnc) || (!vnc->mutex)) 
	{
		return apos;
	}

	SDL_LockMutex(vnc->mutex);
	if (vnc->framebuffer) {
		apos=vnc->cursorhotspot;
	}
	SDL_UnlockMutex(vnc->mutex);
	return apos;
}

int vncClientKeyevent(tSDL_vnc *vnc, unsigned char downflag, unsigned int key)
{
	tSDL_vnc_clientKeyevent clientKeyevent;
	int result=0;

	SDL_LockMutex(vnc->mutex);
	if (vnc->clientbufferpos<(VNC_BUFSIZE-8)) {
		clientKeyevent.messagetype=4;
		clientKeyevent.downflag=downflag;
		clientKeyevent.key=swap_32(key);
		memcpy(&vnc->clientbuffer[vnc->clientbufferpos],&clientKeyevent,8);
		vnc->clientbufferpos += 8;
		result = 1;
	} else {
		DBMESSAGE("CLient buffer full - ignoring keyevent.");
	}
	SDL_UnlockMutex(vnc->mutex);

	return result;
}

int vncClientPointerevent(tSDL_vnc *vnc, unsigned char buttonmask, unsigned short x, unsigned short y)
{
	tSDL_vnc_clientPointerevent clientPointerevent;
	int result=0;

	SDL_LockMutex(vnc->mutex);
	if (vnc->clientbufferpos<(VNC_BUFSIZE-6)) {
		clientPointerevent.messagetype=5;
		clientPointerevent.buttonmask=buttonmask;
		clientPointerevent.x=swap_16(x);
		clientPointerevent.y=swap_16(y);
		memcpy(&vnc->clientbuffer[vnc->clientbufferpos],&clientPointerevent,6);
		vnc->clientbufferpos += 6;
		result = 1;
	} else {
		DBMESSAGE("CLient buffer full - ignoring mouseevent.");
	}
	SDL_UnlockMutex(vnc->mutex);

	return result;
}

void vncDisconnect(tSDL_vnc *vnc)
{
	if (vnc->thread) {
		SDL_KillThread(vnc->thread);
		vnc->thread=NULL;
	}
	if (vnc->mutex) {
		SDL_DestroyMutex(vnc->mutex);
		vnc->mutex=NULL;
	}
	if (vnc->socket) {
#ifdef WIN32
		closesocket(vnc->socket);
#else
		close(vnc->socket);
#endif
		vnc->socket=0;
	}
	if (vnc->buffer) {
		free(vnc->buffer);
		vnc->buffer=NULL;
	}
	if (vnc->clientbuffer) {
		free(vnc->clientbuffer);
		vnc->clientbuffer=NULL;
	}
	if (vnc->framebuffer) {
		SDL_FreeSurface(vnc->framebuffer);
		vnc->framebuffer=NULL;
	}
	if (vnc->scratchbuffer) {
		SDL_FreeSurface(vnc->scratchbuffer);
		vnc->scratchbuffer=NULL;
	}
    if (vnc->rawbuffer) {
        free(vnc->rawbuffer);
        vnc->rawbuffer=NULL;
    }

	if (vnc->tilebuffer) {
		SDL_FreeSurface(vnc->tilebuffer);
		vnc->tilebuffer=NULL;
	}
	if (vnc->cursorbuffer) {
		SDL_FreeSurface(vnc->cursorbuffer);
		vnc->cursorbuffer=NULL;
	}
}
