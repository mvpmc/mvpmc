/*
 * surface.c
 *
 * Jon Gettler <gettler@acm.org>
 */

#ident "$Id$"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include "mvp_osd.h"
#include "osd.h"
#include "surface.h"

#if 0
#define PRINTF(x...) printf(x)
#else
#define PRINTF(x...)
#endif

/*
 * osd_create_surface() - create a drawing surface
 */
osd_surface_t*
osd_create_surface(int w, int h)
{
	osd_surface_t *surface;
	int ret, num = 0;

	PRINTF("%s(): stbgfx %d\n", __FUNCTION__, stbgfx);

	if (stbgfx < 0) {
		if ((stbgfx=open("/dev/stbgfx", O_RDWR)) < 0)
			return NULL;
		gfx_init();
	}

	PRINTF("%s(): stbgfx %d\n", __FUNCTION__, stbgfx);

	if ((surface=malloc(sizeof(*surface))) == NULL)
		return NULL;
	memset(surface, 0, sizeof(*surface));

	do {
		ret = ioctl(stbgfx, GFX_FB_SET_OSD, &num);
	} while ((ret != 0) && (num++ < 16));

	if (ret != 0)
		goto err;

	if (ioctl(stbgfx, GFX_FB_OSD_SURFACE, &num) != 0)
		goto err;

	memset(&surface->sfc, 0, sizeof(surface->sfc));
	surface->sfc.width = w;
	surface->sfc.height = h;
	surface->sfc.flags = 0x3f1533;
	surface->sfc.unknown = 1;
	if (ioctl(stbgfx, GFX_FB_SFC_ALLOC, &surface->sfc) != 0)
		goto err;

	memset(&surface->map, 0, sizeof(surface->map));
	surface->map.map[0].unknown = surface->sfc.handle;
	if (ioctl(stbgfx, GFX_FB_MAP, &surface->map) != 0)
		goto err;

	if ((surface->base[0]=mmap(NULL, surface->map.map[0].size,
				   PROT_READ|PROT_WRITE, MAP_SHARED, stbgfx,
				   surface->map.map[0].addr)) == MAP_FAILED)
		goto err;
	if ((surface->base[1]=mmap(NULL, surface->map.map[1].size,
				   PROT_READ|PROT_WRITE, MAP_SHARED, stbgfx,
				   surface->map.map[1].addr)) == MAP_FAILED)
		goto err;
	if ((surface->base[2]=mmap(NULL, surface->map.map[2].size,
				   PROT_READ|PROT_WRITE, MAP_SHARED, stbgfx,
				   surface->map.map[2].addr)) == MAP_FAILED)
		goto err;

	surface->display.num = num;
	if ((ret=ioctl(stbgfx, GFX_FB_MOVE_DISPLAY, &surface->display)) != 0)
		return NULL;
	PRINTF("Display width: %ld  height: %ld\n",
	       surface->display.width, surface->display.height);

	if ((ret=ioctl(stbgfx, GFX_FB_SET_DISPLAY, &surface->display)) != 0)
		return NULL;

	return surface;

 err:
	if (surface)
		free(surface);

	return NULL;
}

int
osd_destroy_surface(osd_surface_t *surface)
{
	int i;

	for (i=0; i<3; i++)
		if (surface->base[i])
			munmap(surface->base[i], surface->map.map[i].size);

	if (ioctl(stbgfx, GFX_FB_SFC_FREE, &surface->sfc) != 0)
		return -1;

	free(surface);

	PRINTF("surface destroyed\n");

	return 0;
}

void
osd_display_surface(osd_surface_t *surface)
{
	unsigned long fb_descriptor[2];

	fb_descriptor[0] = surface->sfc.handle;
	fb_descriptor[1] = 1;
	
	ioctl(stbgfx, GFX_FB_ATTACH, fb_descriptor);
}
