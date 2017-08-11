#ifndef _SDL_vnc_private_h
#define _SDL_vnc_private_h
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

#pragma pack(push, 1)
typedef struct tSDL_vnc_serverUpdate {
	uint8_t padding;
	uint16_t rectangles;
} tSDL_vnc_serverUpdate;
#pragma pack(pop)

typedef struct tSDL_vnc_serverRectangle {
	tSDL_vnc_rect rect;
	unsigned int encoding;
} tSDL_vnc_serverRectangle;

#pragma pack(push, 1)
typedef struct tSDL_vnc_serverColormap {
	uint8_t padding;
	uint16_t first;
	uint16_t number;
} tSDL_vnc_serverColormap;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct tSDL_vnc_serverText {
	uint8_t padding[3];
	uint32_t length;
} tSDL_vnc_serverText;
#pragma pack(pop)

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

#pragma pack(push, 1)
typedef struct tSDL_vnc_serverHextileColored8bpp {
	uint8_t color;
	uint8_t xy;
	uint8_t wh;
} tSDL_vnc_serverHextileColored8bpp;

typedef struct tSDL_vnc_serverHextileColored16bpp {
	uint16_t color;
	uint8_t xy;
	uint8_t wh;
} tSDL_vnc_serverHextileColored16bpp;

typedef struct tSDL_vnc_serverHextileColored32bpp {
	uint32_t color;
	uint8_t xy;
	uint8_t wh;
} tSDL_vnc_serverHextileColored32bpp;
#pragma pack(pop)

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

void vnc_to_sdl_rect(tSDL_vnc_rect * src, SDL_Rect * dest);
void GrowUpdateRegion(tSDL_vnc *vnc, SDL_Rect *trec);

#endif /* _SDL_vnc_private_h */
