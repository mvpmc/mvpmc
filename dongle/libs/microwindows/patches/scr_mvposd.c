/*
 * MediaMVP On Screen Display Screen Driver
 *
 * Jon Gettler <gettler@mvpmc.org>
 * http://www.mvpmc.org/
 */

#include <assert.h>
#include <stdio.h>
#include "device.h"
#include "fb.h"
#include "genmem.h"
#include "genfont.h"

#include "mvp_osd.h"

#if 0
#define PRINTF(x...) printf(x)
#else
#define PRINTF(x...)
#endif

/* specific driver entry points*/
static PSD  OSD_open(PSD psd);
static void OSD_close(PSD psd);
static void OSD_getscreeninfo(PSD psd,PMWSCREENINFO psi);
static void OSD_setpalette(PSD psd,int first,int count,MWPALENTRY *pal);
static void OSD_drawpixel(PSD psd,MWCOORD x, MWCOORD y, MWPIXELVAL c);
static MWPIXELVAL OSD_readpixel(PSD psd,MWCOORD x, MWCOORD y);
static void OSD_drawhline(PSD psd,MWCOORD x1, MWCOORD x2, MWCOORD y, MWPIXELVAL c);
static void OSD_drawvline(PSD psd,MWCOORD x, MWCOORD y1, MWCOORD y2, MWPIXELVAL c);
static void OSD_fillrect(PSD psd,MWCOORD x1, MWCOORD y1, MWCOORD x2,
		MWCOORD y2, MWPIXELVAL c);
static void OSD_blit(PSD dstpsd, MWCOORD dstx, MWCOORD dsty, MWCOORD w,
		MWCOORD h, PSD srcpsd, MWCOORD srcx, MWCOORD srcy, long op);
static void OSD_preselect(PSD psd);
static void OSD_drawarea(PSD psd, driver_gc_t * gc, int op);
static MWBOOL OSD_mapmemgc(PSD mempsd, MWCOORD w, MWCOORD h, int planes,
			   int bpp, int linelen, int size,void *addr);
static void OSD_freememgc(PSD mempsd);

SCREENDEVICE	scrdev = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, NULL,
	OSD_open,
	OSD_close,
	OSD_getscreeninfo,
	OSD_setpalette,
	OSD_drawpixel,
	OSD_readpixel,
	OSD_drawhline,
	OSD_drawvline,
	OSD_fillrect,
	gen_fonts,
	OSD_blit,
	OSD_preselect,
	OSD_drawarea,
	NULL,			/* SetIOPermissions*/
	gen_allocatememgc,
	OSD_mapmemgc,
	OSD_freememgc,
	NULL,			/* StretchBlit subdriver*/
	NULL			/* SetPortrait*/
};

extern int gr_mode;	/* temp kluge*/

static int
osd_init(PSD psd, int w, int h)
{
	osd_surface_t *surface;
	int width, height;

	if ((surface=osd_create_surface(w, h, 0, OSD_GFX)) == NULL) {
		return -1;
	}
	if (osd_get_surface_size(surface, &width, &height) < 0)
		return -1;

	psd->xres = psd->xvirtres = width;
	psd->yres = psd->yvirtres = height;
	psd->linelen = psd->xres;

	psd->ncolors = (1 << 24);
	psd->planes = 1;
	psd->bpp = 32;
	psd->pixtype = MWPF_TRUECOLOR8888;
	psd->flags = PSF_SCREEN | PSF_HAVEBLIT | PSF_MEMORY;
	psd->size = 0;
	psd->portrait = MWPORTRAIT_NONE;

	psd->addr = (void*)surface;

	PRINTF("created surface %d,%d at 0x%.8x\n", w, h, surface);

	return 0;
}

static PSD
OSD_open(PSD psd)
{
	osd_surface_t *surface;

	if (osd_init(psd, -1, -1) < 0)
		return NULL;

	surface = (osd_surface_t*)psd->addr;

	osd_display_surface(surface);

	PRINTF("OSD: display created and displayed\n");

	return psd;
}

static void
OSD_close(PSD psd)
{
	osd_surface_t *surface = (osd_surface_t*)psd->addr;

	if (surface) {
		osd_destroy_surface(surface);

		PRINTF("OSD: %p closed\n", surface);
	}

	surface = NULL;
}

static void
OSD_getscreeninfo(PSD psd,PMWSCREENINFO psi)
{
	osd_surface_t *surface = (osd_surface_t*)psd->addr;

	psi->rows = psd->yvirtres;
	psi->cols = psd->xvirtres;
	psi->planes = psd->planes;
	psi->bpp = psd->bpp;
	psi->ncolors = psd->ncolors;
	psi->fonts = NUMBER_FONTS;
	psi->portrait = MWPORTRAIT_NONE;
	psi->fbdriver = FALSE;	/* not running fb driver, no direct map*/
	psi->pixtype = psd->pixtype;
	psi->rmask = 0xff0000;
	psi->gmask = 0x00ff00;
	psi->bmask = 0x0000ff;

	psi->xdpcm = 27;	/* assumes screen width of 24 cm*/
	psi->ydpcm = 27;	/* assumes screen height of 18 cm*/
}

static void
OSD_setpalette(PSD psd,int first,int count,MWPALENTRY *pal)
{
	osd_surface_t *surface = (osd_surface_t*)psd->addr;

	PRINTF("OSD: setpalette, first %d, count %d\n", first, count);
}

static void
OSD_drawpixel(PSD psd,MWCOORD x, MWCOORD y, MWPIXELVAL c)
{
	osd_surface_t *surface = (osd_surface_t*)psd->addr;

	PRINTF("OSD: draw pixel %d,%d color 0x%.8x\n", x, y, c);
	PRINTF("OSD: sfc 0x%.8x draw pixel %d,%d color 0x%.8x\n", surface, x, y, c);

#if 1
	c |= 0xff000000;
#endif

	osd_draw_pixel(surface, x, y, c);
}

static MWPIXELVAL
OSD_readpixel(PSD psd,MWCOORD x, MWCOORD y)
{
	osd_surface_t *surface = (osd_surface_t*)psd->addr;

	PRINTF("OSD: readpixel %d,%d\n", x, y);

	return osd_read_pixel(surface, x, y);
}

static void
OSD_drawhline(PSD psd,MWCOORD x1, MWCOORD x2, MWCOORD y, MWPIXELVAL c)
{
	osd_surface_t *surface = (osd_surface_t*)psd->addr;

	PRINTF("OSD: drawhline %d %d %d color 0x%.8x\n", x1, x2, y, c);

	osd_draw_horz_line(surface, x1, x2, y, c);
}

static void
OSD_drawvline(PSD psd,MWCOORD x, MWCOORD y1, MWCOORD y2, MWPIXELVAL c)
{
	osd_surface_t *surface = (osd_surface_t*)psd->addr;

	PRINTF("OSD: drawvline %d %d %d color 0x%.8x\n", x, y1, y2, c);

	osd_draw_vert_line(surface, x, y1, y2, c);
}

static void
OSD_fillrect(PSD psd,MWCOORD x1, MWCOORD y1, MWCOORD x2, MWCOORD y2, MWPIXELVAL c)
{
	osd_surface_t *surface = (osd_surface_t*)psd->addr;
	int w, h;

	PRINTF("OSD: fillrect %d,%d %d,%d color 0x%.8x\n", x1, y1, x2, y2, c);

	w = x2 - x1 + 1;
	h = y2 - y1 + 1;

	osd_fill_rect(surface, x1, y1, w, h, c);
}

/* only screen-to-screen blit implemented, op ignored*/
/* FIXME*/
static void
OSD_blit(PSD dstpsd, MWCOORD dstx, MWCOORD dsty, MWCOORD w, MWCOORD h,
	 PSD srcpsd, MWCOORD srcx, MWCOORD srcy, long op)
{
	osd_surface_t *dstsfc = (osd_surface_t*)dstpsd->addr;
	osd_surface_t *srcsfc = (osd_surface_t*)srcpsd->addr;

	if (op == MWMODE_NOOP)
		return;
	if(!(srcpsd->flags & PSF_SCREEN) || !(dstpsd->flags & PSF_SCREEN))
		return;

	PRINTF("%s(): line %d\n", __FUNCTION__, __LINE__);

	PRINTF("blit dst 0x%.8x src 0x%.8x  %d,%d to %d,%d  wh %d %d\n",
	       dstsfc, srcsfc, srcx, srcy, dstx, dsty, w, h);

	osd_blit(dstsfc, dstx, dsty, srcsfc, srcx, srcy, w, h);
}

static void
OSD_drawarea(PSD psd, driver_gc_t * gc, int op)
{
	PRINTF("%s(): line %d\n", __FUNCTION__, __LINE__);
}

static void
OSD_preselect(PSD psd)
{
	osd_surface_t *surface = (osd_surface_t*)psd->addr;

	PRINTF("%s(): line %d\n", __FUNCTION__, __LINE__);
}

MWBOOL
OSD_mapmemgc(PSD mempsd,MWCOORD w,MWCOORD h,int planes,int bpp,int linelen,
	     int size,void *addr)
{
	osd_surface_t *surface;

	if (osd_init(mempsd, w, h) < 0)
		return 0;
	surface = (osd_surface_t*)mempsd->addr;

	return 1;
}

static void
OSD_freememgc(PSD mempsd)
{
	assert(mempsd->flags & PSF_MEMORY);

	/* note: mempsd->addr must be freed elsewhere*/
	OSD_close(mempsd);

	free(mempsd);
}
