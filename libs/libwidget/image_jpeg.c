/*
 * Copyright (c) 2000, 2001, 2003 Greg Haerr <greg@censoft.com>
 * Portions Copyright (c) 2000 Martin Jolicoeur <martinj@visuaide.com>
 * Portions Copyright (c) Independant JPEG group (ijg)
 * Portions Copyright (c) 2006 Terence Wells
 *
 * 24 bpp truecolor scaled image decode routine for JPEG files
 *   mainly derived from
 *   - microwindows/src/engine/devimage_stretch.c
 *   - microwindows/src/engine/image_jpeg.c
 *   - mvplib/libwidget/image.c
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include "device.h"
#include "nano-X.h"
#include "mvp_widget.h"
#include "widget.h"
#include "jpeglib.h"

unsigned char *buffer;
long bufsize;
int fd;
unsigned int orient;
int fatal_error;

static void
error_exit (j_common_ptr cinfo)
{
	fatal_error = 1;
	(*cinfo->err->output_message) (cinfo);
}


static void
init_source(j_decompress_ptr cinfo)
{
	cinfo->src->next_input_byte = buffer;
	cinfo->src->bytes_in_buffer = 0;
}

static boolean
fill_input_buffer(j_decompress_ptr cinfo)
{
	ssize_t ret;
	cinfo->src->next_input_byte = buffer;
	ret = read(fd, buffer, bufsize);
	if( ret <= 0 ) {
		buffer[0] = 0xFF;
		buffer[1] = JPEG_EOI;
		ret = 2;
	}
	cinfo->src->bytes_in_buffer = ret;
	return TRUE;
}

static void
skip_input_data(j_decompress_ptr cinfo, long num_bytes)
{
	if (num_bytes <= 0) return;
	if (num_bytes >= cinfo->src->bytes_in_buffer) {
		cinfo->src->next_input_byte = buffer;
		cinfo->src->bytes_in_buffer = 0;
		lseek(fd, num_bytes-cinfo->src->bytes_in_buffer, SEEK_CUR);
	} else {
		cinfo->src->next_input_byte += num_bytes;
		cinfo->src->bytes_in_buffer -= num_bytes;
	}
}

static boolean
resync_to_restart(j_decompress_ptr cinfo, int desired)
{
	return jpeg_resync_to_restart(cinfo, desired);
}

static void
term_source(j_decompress_ptr cinfo)
{
}

#define GET2BYTES(cinfo, V, swap, offset) do { \
		if (cinfo->src->bytes_in_buffer == 0) fill_input_buffer(cinfo); \
		cinfo->src->bytes_in_buffer--; \
		V = (*cinfo->src->next_input_byte++) << (swap?0:8); \
		if (cinfo->src->bytes_in_buffer == 0) fill_input_buffer(cinfo); \
		cinfo->src->bytes_in_buffer--; \
		V += (*cinfo->src->next_input_byte++) << (swap?8:0); \
		offset += 2; } while(0) 

#define GET4BYTES(cinfo, V, swap, offset) do { \
		if (cinfo->src->bytes_in_buffer == 0) fill_input_buffer(cinfo); \
		cinfo->src->bytes_in_buffer--; \
		V = (*cinfo->src->next_input_byte++) << (swap?0:24); \
		if (cinfo->src->bytes_in_buffer == 0) fill_input_buffer(cinfo); \
		cinfo->src->bytes_in_buffer--; \
		V += (*cinfo->src->next_input_byte++) << (swap?8:16); \
		if (cinfo->src->bytes_in_buffer == 0) fill_input_buffer(cinfo); \
		cinfo->src->bytes_in_buffer--; \
		V += (*cinfo->src->next_input_byte++) << (swap?16:8); \
		if (cinfo->src->bytes_in_buffer == 0) fill_input_buffer(cinfo); \
		cinfo->src->bytes_in_buffer--; \
		V += (*cinfo->src->next_input_byte++) << (swap?24:0); \
		offset += 4; } while(0)

static boolean
get_exif_orient (j_decompress_ptr cinfo)
/* Get the Exif orientation info */
{
	unsigned int tmp, offset, length, numtags;
	boolean swap;
	orient = 1;
	offset = 0;

	/* marker length */
	GET2BYTES(cinfo, length, 0, offset);
	if (length<8) goto err;
	/* Exif header */
	GET4BYTES(cinfo, tmp, 0, offset);
	if (tmp != 0x45786966) goto err;
	GET2BYTES(cinfo, tmp, 0, offset);
	if (tmp != 0x0000) goto err;
	/* Byte-order */
	GET2BYTES(cinfo, tmp, 0, offset);
	if (tmp == 0x4949) swap = 1;
	else if (tmp == 0x4d4d) swap = 0;
	else goto err;
	GET2BYTES(cinfo, tmp, swap, offset);
	if (tmp != 0x002A) goto err;
	/* offset to first IFD */
	GET4BYTES(cinfo, tmp, swap, offset);
	offset += tmp-8;
	skip_input_data(cinfo, tmp-8);
	/* number of tags in IFD */
	GET2BYTES(cinfo, numtags, swap, offset);
  	if (numtags == 0) goto err;
	
	/* Search for Orientation Tag in IFD0 */
	for (;;) {
		if (offset > length-12) goto err;
		GET2BYTES(cinfo, tmp, swap, offset);
		if (tmp == 0x0112) break; /* found Orientation Tag */
		if (--numtags == 0) goto err;
		offset += 10;
		skip_input_data(cinfo, 10);
	}
	offset += 6;
	skip_input_data(cinfo, 6);
	GET2BYTES(cinfo, orient, swap, offset);
	if( orient==0 || orient>8 ) orient = 1;

err:
	skip_input_data(cinfo, length-offset);
	return TRUE;
}

int
mvpw_load_image_jpeg(mvp_widget_t *widget, char *file)
{
	int ret, i;
	struct stat s;
	PMWIMAGEHDR pimage;
	struct jpeg_source_mgr smgr;
	struct jpeg_decompress_struct cinfo;
	struct jpeg_error_mgr jerr;
	int xasp, yasp, width, height, pos, inc, pos2, inc2, dst_row;
	MWUCHAR *dstp, *srcp;
	GR_WINDOW_ID pid, wid;
	GR_GC_ID gc;
	GR_WM_PROPERTIES props;
	JSAMPROW rowptr[1];
	unsigned char prefix[2];

	wid = widget->data.image.wid;
	pid = widget->data.image.pid;

	ret = 0;
	fatal_error = 0;

	fd = open(file, O_RDONLY);
	if (fd < 0 || fstat(fd, &s) < 0) {
		EPRINTF("%s: cannot open image: %s\n", __FUNCTION__, file);
		ret = -1;
		goto err1;
	}

	read(fd, prefix, 2);
	if ((prefix[0] != 0xff) && (prefix[1] != 0xd8)) {
		EPRINTF("%s: not a JPEG file: %s\n", __FUNCTION__, file);
		ret = -1;
		goto err1;
	}
	lseek(fd, 0, SEEK_SET);

	bufsize = 65536;
	buffer = malloc(bufsize);
	if (!buffer) {
		EPRINTF("%s: malloc(%li) failure for buffer\n", __FUNCTION__, bufsize);
		ret = -1;
		goto err1;
	}

	pimage = (PMWIMAGEHDR)malloc(sizeof(MWIMAGEHDR));
	if(!pimage) {
		EPRINTF("%s: malloc(%d) failure for pimage\n", __FUNCTION__, sizeof(MWIMAGEHDR));
		ret = -1;
		goto err2;
	}

	pimage->imagebits = NULL;
	pimage->palette = NULL;
	pimage->transcolor = 0;
	rowptr[0] = NULL;

	cinfo.err = jpeg_std_error(&jerr);
	jerr.error_exit = (void *) error_exit;

	jpeg_create_decompress(&cinfo);

	smgr.init_source = (void *) init_source;
	smgr.fill_input_buffer = (void *) fill_input_buffer;
	smgr.skip_input_data = (void *) skip_input_data;
	smgr.resync_to_restart = (void *) resync_to_restart;
	smgr.term_source = (void *) term_source;
	cinfo.src = &smgr;

	jpeg_set_marker_processor(&cinfo, JPEG_APP0+1, get_exif_orient);

	if (jpeg_read_header(&cinfo, FALSE) == JPEG_REACHED_EOI) {
		EPRINTF("%s: truncated file: %s\n", __FUNCTION__, file);
		goto err3;
	}

	cinfo.out_color_space = JCS_RGB;
	cinfo.quantize_colors = FALSE;
	cinfo.dct_method = JDCT_ISLOW;
	cinfo.do_fancy_upsampling = FALSE;
	cinfo.two_pass_quantize = FALSE;

	xasp = 1;
	yasp = 1;
	if (widescreen) {
		xasp *= 16;
		yasp *= 9;
	} else {
		xasp *= 4;
		yasp *= 3;
	}

	switch( orient  ) {
	default:
		orient = 1;
	case 1:
	case 2:
	case 3:
	case 4:
		if(cinfo.image_width*yasp > cinfo.image_height*xasp) {
			width = widget->width;
			height = widget->height*cinfo.image_height*xasp/cinfo.image_width/yasp;
		} else {
			width = widget->width*cinfo.image_width*yasp/cinfo.image_height/xasp;
			height = widget->height;
		}
		pimage->width = width;
		pimage->height = 1;
		break;
	case 5:
	case 6:
	case 7:
	case 8:
		if(cinfo.image_height*yasp > cinfo.image_width*xasp) {
			height = widget->width;
			width = widget->height*cinfo.image_width*xasp/cinfo.image_height/yasp;
		} else {
			height = widget->width*cinfo.image_height*yasp/cinfo.image_width/xasp;
			width = widget->height;
		}
		pimage->width = 1;
		pimage->height = width;
		break;
	}

	cinfo.scale_num = 1;
	cinfo.scale_denom = 8;

	while( (cinfo.image_height*cinfo.scale_num < height*cinfo.scale_denom || cinfo.image_width*cinfo.scale_num < width*cinfo.scale_denom) && cinfo.scale_num<16 )
		cinfo.scale_num++;

	if (jpeg_has_multiple_scans(&cinfo) ) {
 		EPRINTF("%s: progressive JPEG, skipping: %s\n", __FUNCTION__, file);
		goto err3;
 	}

	jpeg_calc_output_dimensions(&cinfo);

	if( cinfo.output_components != 3 ) {
		EPRINTF("%s: JPEG cannot be mapped to 3 components\n", __FUNCTION__);
		ret = -1;
		goto err3;
	}

	rowptr[0] = malloc(3 * cinfo.output_width);
	if(!rowptr[0]) {
		EPRINTF("%s: malloc(%d) failure for rowptr\n", __FUNCTION__, cinfo.output_components * cinfo.output_width);
		ret = -1;
		goto err3;
	}

	pimage->planes = 1;
	pimage->bpp = 24;
	pimage->bytesperpixel = 3;
	pimage->pitch = pimage->bytesperpixel * pimage->width;
	pimage->compression = MWIMAGE_RGB;
	pimage->palsize = 0;
	pimage->imagebits = malloc(pimage->pitch * pimage->height);
	if(!pimage->imagebits) {
		EPRINTF("%s: malloc(%d) failure for pimagebits\n", __FUNCTION__, pimage->pitch * pimage->height);
		ret = -1;
		goto err4;
	}

	jpeg_start_decompress (&cinfo);

	if (pid == 0)
		pid = GrNewPixmap(widget->width, widget->height, NULL);

	gc = GrNewGC();
	GrSetGCForeground(gc, 0xff000000);
	GrFillRect(pid, gc, 0, 0, widget->width, widget->height);

	pos = 0x10000L;
	inc = (cinfo.output_height << 16) / height;
	for( dst_row=0 ; dst_row<height; ++dst_row ) {
		while (pos >= 0x10000L ) {
			if(fatal_error) goto err5;
			if(cinfo.output_scanline < cinfo.output_height)
				jpeg_read_scanlines (&cinfo, rowptr, 1);
			pos -= 0x10000L;
		}
		srcp = (MWUCHAR *)rowptr[0];
		dstp = (MWUCHAR *)pimage->imagebits;
		if( orient == 2 || orient == 3 || orient == 7 || orient == 8 ) dstp += 3*(width+1);
		pos2 = 0x10000L;
		inc2 = (cinfo.output_width << 16) / width;
		for( i=width ; i>0 ; --i ) {
			while( pos2 >= 0x10000L ) {
				srcp += 3;
				pos2 -= 0x10000L;
			}
			srcp -= 3;
			if( orient == 2 || orient == 3 || orient == 7 || orient == 8 ) dstp -= 6;
			*dstp++ = *srcp++;
			*dstp++ = *srcp++;
			*dstp++ = *srcp++;
			pos2 += inc2;
		}
		pos += inc;
		switch( orient ) {
		case 1:
		case 2:
			GrDrawImageBits(pid, gc, (widget->width-width)/2, (widget->height-height)/2+dst_row, pimage);
			break;
		case 3:
		case 4:
			GrDrawImageBits(pid, gc, (widget->width-width)/2, (widget->height+height)/2-dst_row, pimage);
			break;
		case 5:
		case 8:
			GrDrawImageBits(pid, gc, (widget->width-height)/2+dst_row, (widget->height-width)/2, pimage);
			break;
		case 6:
		case 7:
			GrDrawImageBits(pid, gc, (widget->width+height)/2-dst_row, (widget->height-width)/2, pimage);
			break;
		}
	}
	while (cinfo.output_scanline < cinfo.output_height) {
		if(fatal_error) goto err5;
		jpeg_read_scanlines (&cinfo, rowptr, 1);
	}
	if(fatal_error) goto err5;

	jpeg_finish_decompress (&cinfo);

err5:
	GrDestroyGC(gc);

	free(pimage->imagebits);

	if (wid == 0) {
		wid = GrNewWindowEx(
			GR_WM_PROPS_APPWINDOW|GR_WM_PROPS_NOAUTOMOVE, NULL, widget->wid,
			0, 0, widget->width, widget->height, 0xff00ff00);
		props.flags = GR_WM_FLAGS_PROPS;
		props.props = GR_WM_PROPS_NODECORATE;
		GrSetWMProperties(wid, &props);
		GrSetBackgroundPixmap(wid, pid, GR_BACKGROUND_CENTER);
	}

	widget->data.image.wid = wid;
	widget->data.image.pid = pid;

err4:
	free(rowptr[0]);
err3:
	jpeg_destroy_decompress (&cinfo);
	free(pimage);
err2:
	free(buffer);
err1:
	close(fd);
	return ret;
}

int
mvpw_show_image_jpeg(mvp_widget_t *widget)
{
	if (!widget->data.image.wid) return -1;
	GrClearArea(widget->data.image.wid, 0, 0, 0, 0, 0);
	GrMapWindow(widget->data.image.wid);
	return 0;
}
