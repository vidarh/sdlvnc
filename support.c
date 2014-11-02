
/* 
 * Copyright 2014, Vidar Hokstad <vidar@hokstad.com>
 *
 * Licensed under the LGPL - see LICENSE
 *
 */


#include "SDL_vnc.h"

#ifdef DEBUG
	#define DBMESSAGE 	printf
#else
	#define DBMESSAGE 	//
#endif

#define CHECKED_READ(vnc, dest, len, message) { \
    int result = Recv(vnc->socket, dest, len, 0); \
    if (result!=len) { \
    printf("Error reading %s. Got %i of %i bytes.\n", message, result, len); \
    return 0; \
    } \
    }

void vnc_to_sdl_rect(tSDL_vnc_rect * src, SDL_Rect * dest);
void GrowUpdateRegion(tSDL_vnc *vnc, SDL_Rect *trec);


int read_raw(tSDL_vnc * vnc, tSDL_vnc_rect rect) {
    SDL_Rect trec;
    vnc_to_sdl_rect(&rect, &trec);
    SDL_LockMutex(vnc->mutex);
    SDL_LockSurface(vnc->framebuffer);
    
    uint32_t pitch = vnc->framebuffer->pitch/4;
    uint32_t * dest = (uint32_t *)vnc->framebuffer->pixels + (rect.y * pitch) + rect.x;
    uint32_t len = rect.width * rect.height * 4;

    DBMESSAGE("Reading %ld bytes straight into buffer", len);
    CHECKED_READ(vnc, dest, len, "framebuffer");

    GrowUpdateRegion(vnc,&trec);
    SDL_UnlockSurface(vnc->framebuffer);
    SDL_UnlockMutex(vnc->mutex);
    return 1;
}


void blit_raw(tSDL_vnc * vnc, tSDL_vnc_rect rect)
{
    SDL_Rect trec;
    vnc_to_sdl_rect(&rect, &trec);
    SDL_LockMutex(vnc->mutex);
    SDL_LockSurface(vnc->framebuffer);

    uint32_t * src  = vnc->rawbuffer;
    uint32_t srcpitch = rect.width;
    uint32_t len = srcpitch * 4;
    uint32_t pitch = vnc->framebuffer->pitch/4;
    uint32_t * dest = (uint32_t *)vnc->framebuffer->pixels + (rect.y * pitch) + rect.x;

    if (srcpitch == pitch) {
        /* full screen, or at least full width */
        memcpy(dest, src, len * rect.height);
    } else {
        uint32_t * end = dest + (pitch*rect.height);
        
        while (dest < end) {
            memcpy(dest, src, len);
            dest += pitch;
            src  += srcpitch;
        }
    }
    GrowUpdateRegion(vnc,&trec);
    SDL_UnlockSurface(vnc->framebuffer);
    SDL_UnlockMutex(vnc->mutex);
}

