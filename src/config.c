/*
 *  Copyright (C) 2005-2006, Jon Gettler
 *  http://www.mvpmc.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/*
 * The config data will be stored either in a file, or in flash.  It is
 * a series of variable length data structures defining values for each
 * type.  The data will typically be compressed before it is stored.
 *
 * All data stored in the config file needs to be portable across releases.
 * This means that if a storage method is changed, a new item tag will be
 * needed, and old software will not be able to read those config changes.
 *
 * Note that the heirarchy of settings is (higher ones override lower ones):
 *     - command line
 *     - config file
 *     - theme file
 *     - defaults
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <zlib.h>

#include <mvp_widget.h>
#include <mvp_demux.h>
#include <mvp_av.h>

#include "mvpmc.h"
#include "mythtv.h"
#include "mclient.h"
#include "display.h"
#include "config.h"

static int
do_compress(unsigned char *buf, int len, unsigned char *cbuf, int clen)
{
	z_stream c_stream;

	c_stream.zalloc = (alloc_func)0;
	c_stream.zfree = (free_func)0;
	c_stream.opaque = (voidpf)0;

	if (deflateInit(&c_stream, Z_DEFAULT_COMPRESSION) != Z_OK) {
		return -1;
	}

	c_stream.next_in  = buf;
	c_stream.next_out = cbuf;
        c_stream.avail_in = len;
	c_stream.avail_out = clen;

	if (deflate(&c_stream, Z_NO_FLUSH) != Z_OK) {
		return -1;
	}

	if (deflate(&c_stream, Z_FINISH) != Z_STREAM_END) {
		return -1;
	}

	if (deflateEnd(&c_stream) != Z_OK) {
		return -1;
	}

	return c_stream.total_out;
}

static int
do_uncompress(unsigned char *cbuf, int clen, unsigned char *buf, int len)
{
	z_stream d_stream;

	d_stream.zalloc = (alloc_func)0;
	d_stream.zfree = (free_func)0;
	d_stream.opaque = (voidpf)0;

	if (inflateInit(&d_stream) != Z_OK) {
		return -1;
	}

	d_stream.next_in  = cbuf;
	d_stream.next_out = buf;
	d_stream.avail_in = clen;
	d_stream.avail_out = len;

        if (inflate(&d_stream, Z_NO_FLUSH) != Z_STREAM_END) {
		return -1;
	}

	if (inflateEnd(&d_stream) != Z_OK) {
		return -1;
	}

	return d_stream.total_out;
}

int
config_compress(config_list_t *in, config_list_t *out)
{
	int len, clen;

	len = CONFIG_MAX_COMPRESSED_SIZE;
	if ((clen=do_compress(in->buf, in->buflen, out->buf, len)) < 0) {
		goto fail;
	}

	out->magic = in->magic;
	out->crc = in->crc;
	out->version = in->version;
	out->buflen = clen;
	out->flags = CONFIG_FLAGS_COMPRESSED | in->flags;

	printf("compressed %lu bytes to %lu bytes\n",
	       (unsigned long)in->buflen, (unsigned long)out->buflen);

	return 0;

 fail:
	return -1;
}

int
config_uncompress(config_list_t *in, config_list_t *out)
{
	int len;
	config_list_t *config = NULL;

	len = CONFIG_MAX_SIZE + sizeof(*config);
	if ((config=(config_list_t*)malloc(len)) == NULL)
		goto fail;
	memset(config, 0, len);

	if ((len=do_uncompress(in->buf, in->buflen, out->buf, len)) < 0) {
		goto fail;
	}

	out->magic = in->magic;
	out->crc = in->crc;
	out->version = in->version;
	out->buflen = len;
	out->flags = in->flags & ~CONFIG_FLAGS_COMPRESSED;

	return 0;

 fail:
	if (config)
		free(config);

	return -1;
}

static int
save_config_to_file(config_list_t *config, char *file, int compress)
{
	config_list_t *ptr = NULL;
	int fd = -1, ret = -1;
	int len, tot;

	if (((config->flags & CONFIG_FLAGS_COMPRESSED) == 0) && compress) {
		len = CONFIG_MAX_COMPRESSED_SIZE + sizeof(*ptr);
		if ((ptr=(config_list_t*)malloc(len)) == NULL) {
			ret = -__LINE__;
			goto fail;
		}
		memset(ptr, 0, len);
		if (config_compress(config, ptr) < 0) {
			ret = -__LINE__;
			goto fail;
		}
	} else {
		ptr = config;
	}

	if ((fd=open(file, O_CREAT|O_TRUNC|O_WRONLY, 0644)) < 0) {
		ret = -__LINE__;
		goto fail;
	}

	tot = 0;
	len = sizeof(*ptr) + ptr->buflen;

	while (tot < len) {
		int rc = write(fd, ptr, len-tot);
		if (rc <= 0) {
			ret = -__LINE__;
			goto fail;
		}
		tot += rc;
	}

	ret = 0;

 fail:
	if (fd >= 0)
		close(fd);
	if (ptr && (ptr != config))
		free(ptr);
	
	return ret;
}

static int
add_item(config_list_t *list, int type)
{
	config_item_t *item = NULL;
	int len = 0, i;
	void *ptr;
	char *buf = NULL;

#define ITEM_FIXED(x, y) \
	case CONFIG_ITEM_##x:\
		if ((config->bitmask & CONFIG_##x) == 0)\
			return 0;\
		len = sizeof(config->y);\
		ptr = (void*)&config->y;\
		break;

#define ITEM_STRING(x, y) \
	case CONFIG_ITEM_##x:\
		if ((config->bitmask & CONFIG_##x) == 0)\
			return 0;\
		len = strlen(config->y);\
		ptr = (void*)&config->y;\
		break;

	switch (type) {
		ITEM_FIXED(SCREENSAVER, screensaver_timeout);
		ITEM_FIXED(MODE, av_mode);
		ITEM_FIXED(AUDIO_OUTPUT, av_audio_output);
		ITEM_FIXED(VIDEO_OUTPUT, av_video_output);
		ITEM_FIXED(TV_ASPECT, av_tv_aspect);
		ITEM_FIXED(OSD_BITRATE, osd_bitrate);
		ITEM_FIXED(OSD_CLOCK, osd_clock);
		ITEM_FIXED(OSD_DEMUX_INFO, osd_demux_info);
		ITEM_FIXED(OSD_PROGRAM, osd_program);
		ITEM_FIXED(OSD_PROGRESS, osd_progress);
		ITEM_FIXED(OSD_TIMECODE, osd_timecode);
		ITEM_FIXED(BRIGHTNESS, brightness);
		ITEM_FIXED(MYTHTV_CONTROL, mythtv_tcp_control);
		ITEM_FIXED(MYTHTV_PROGRAM, mythtv_tcp_program);
		ITEM_FIXED(VOLUME, volume);
		ITEM_FIXED(VIEWPORT, viewport);
		ITEM_STRING(THEME, theme);
		ITEM_STRING(MYTHTV_IP, mythtv_ip);
		ITEM_STRING(MCLIENT_IP, mclient_ip);
		ITEM_FIXED(PLAYBACK_OSD, playback_osd);
		ITEM_FIXED(PLAYBACK_PAUSE, playback_pause);
		ITEM_FIXED(MYTHTV_SORT, mythtv_sort);
		ITEM_FIXED(MYTHTV_PROGRAMS, mythtv_programs);
		ITEM_FIXED(DISPLAY_TYPE, display_type);
		ITEM_FIXED(MYTHTV_FILTER, mythtv_filter);
	case CONFIG_ITEM_MYTHTV_RG_HIDE:
		if ((config->bitmask & CONFIG_MYTHTV_RECGROUP) == 0)
			return 0;
		if ((buf=(char*)malloc(4096)) == NULL)
			goto err;
		memset(buf, 0, 4096);
		for (i=0; i<MYTHTV_RG_MAX; i++) {
			if (config->mythtv_recgroup[i].label[0] &&
			    config->mythtv_recgroup[i].hide) {
				if ((len + strlen(config->mythtv_recgroup[i].label)) >= 4096)
					break;
				strcat(buf+len, config->mythtv_recgroup[i].label);
				len += strlen(config->mythtv_recgroup[i].label) + 1;
			}
		}
		ptr = (void*)buf;
		break;
	case CONFIG_ITEM_MYTHTV_RG_SHOW:
		if ((config->bitmask & CONFIG_MYTHTV_RECGROUP) == 0)
			return 0;
		if ((buf=(char*)malloc(4096)) == NULL)
			goto err;
		memset(buf, 0, 4096);
		for (i=0; i<MYTHTV_RG_MAX; i++) {
			if (config->mythtv_recgroup[i].label[0] &&
			    !config->mythtv_recgroup[i].hide) {
				if ((len + strlen(config->mythtv_recgroup[i].label)) >= 4096)
					break;
				strcat(buf+len, config->mythtv_recgroup[i].label);
				len += strlen(config->mythtv_recgroup[i].label) + 1;
			}
		}
		ptr = (void*)buf;
		break;
	default:
		goto err;
		break;
	}

#undef ITEM_FIXED
#undef ITEM_STRING

	if (len == 0)
		goto out;

	if ((item=(config_item_t*)malloc(sizeof(*item)+len)) == NULL)
		return -1;
	item->type = type;
	item->buflen = len;
	memcpy(item->buf, ptr, len);

	memcpy(list->buf+list->buflen, item, sizeof(*item)+item->buflen);

	list->buflen += sizeof(*item)+item->buflen;

	free(item);

 out:
	if (buf)
		free(buf);

	return 0;

 err:
	if (item)
		free(item);

	return -1;
}

static int
get_item(config_item_t *item, int override)
{
	int len, i;
	char *ptr;

#define ITEM_FIXED(x, y) \
	case CONFIG_ITEM_##x: \
		len = sizeof(config->y); \
		if (len != item->buflen) \
			return -1; \
		if (!(config->bitmask & CONFIG_##x) || override) { \
			memcpy((void*)&config->y, item->buf, len); \
			config->bitmask |= CONFIG_##x; \
		} \
		break;

#define ITEM_STRING(x, y) \
	case CONFIG_ITEM_##x: \
		len = sizeof(config->y); \
		if (len <= item->buflen) \
			return -1; \
		if (!(config->bitmask & CONFIG_##x) || override) { \
			memcpy((void*)&config->y, item->buf, len); \
			config->y[item->buflen] = '\0'; \
			config->bitmask |= CONFIG_##x; \
		} \
		break;

	switch (item->type) {
		ITEM_FIXED(SCREENSAVER, screensaver_timeout);
		ITEM_FIXED(MODE, av_mode);
		ITEM_FIXED(AUDIO_OUTPUT, av_audio_output);
		ITEM_FIXED(VIDEO_OUTPUT, av_video_output);
		ITEM_FIXED(TV_ASPECT, av_tv_aspect);
		ITEM_FIXED(OSD_BITRATE, osd_bitrate);
		ITEM_FIXED(OSD_CLOCK, osd_clock);
		ITEM_FIXED(OSD_DEMUX_INFO, osd_demux_info);
		ITEM_FIXED(OSD_PROGRAM, osd_program);
		ITEM_FIXED(OSD_PROGRESS, osd_progress);
		ITEM_FIXED(OSD_TIMECODE, osd_timecode);
		ITEM_FIXED(BRIGHTNESS, brightness);
		ITEM_FIXED(MYTHTV_CONTROL, mythtv_tcp_control);
		ITEM_FIXED(MYTHTV_PROGRAM, mythtv_tcp_program);
		ITEM_FIXED(VOLUME, volume);
		ITEM_FIXED(VIEWPORT, viewport);
		ITEM_STRING(THEME, theme);
		ITEM_STRING(MYTHTV_IP, mythtv_ip);
		ITEM_STRING(MCLIENT_IP, mclient_ip);
		ITEM_FIXED(PLAYBACK_OSD, playback_osd);
		ITEM_FIXED(PLAYBACK_PAUSE, playback_pause);
		ITEM_FIXED(MYTHTV_SORT, mythtv_sort);
		ITEM_FIXED(MYTHTV_PROGRAMS, mythtv_programs);
		ITEM_FIXED(DISPLAY_TYPE, display_type);
		ITEM_FIXED(MYTHTV_FILTER, mythtv_filter);
		ITEM_STRING(VLC_IP, vlc_ip);
		ITEM_STRING(VLC_VOPTS, vlc_vopts);
		ITEM_STRING(VLC_AOPTS, vlc_aopts);
		ITEM_FIXED(VLC_VB, vlc_vb);
		ITEM_FIXED(VLC_AB, vlc_ab);
	case CONFIG_ITEM_MYTHTV_RG_HIDE:
	case CONFIG_ITEM_MYTHTV_RG_SHOW:
		config->bitmask |= CONFIG_MYTHTV_RECGROUP;
		len = item->buflen;
		ptr = (char*)item->buf;
		i = 0;
		while ((len > 0) && (i < MYTHTV_RG_MAX)) {
			if (config->mythtv_recgroup[i].label[0]) {
				i++;
				continue;
			}
			if (strlen(ptr) <
			    sizeof(config->mythtv_recgroup[i].label)) {
				printf("HIDE: %d len %d label '%s'\n",
				       i, len, ptr);
				strncpy(config->mythtv_recgroup[i].label,
					ptr, strlen(ptr));
				if (item->type == CONFIG_ITEM_MYTHTV_RG_HIDE)
					config->mythtv_recgroup[i].hide = 1;
				i++;
			}
			len -= strlen(ptr) + 1;
			ptr += strlen(ptr) + 1;
		}
		break;
	default:
		return -1;
		break;
	}

#undef ITEM_FIXED
#undef ITEM_STRING

	return 0;
}

int
save_config_file(char *file)
{
	config_list_t *list;
	int compress = 1, ret;

	if ((list=(config_list_t*)malloc(CONFIG_MAX_SIZE+sizeof(*list))) == NULL)
		return -1;
	memset(list, 0, sizeof(*list));

	list->magic = CONFIG_MAGIC;
	list->buflen = 0;
	list->flags = CONFIG_FLAGS_PLATFORM;

	if (add_item(list, CONFIG_ITEM_SCREENSAVER) < 0)
		goto err;
	if (add_item(list, CONFIG_ITEM_MODE) < 0)
		goto err;
	if (add_item(list, CONFIG_ITEM_AUDIO_OUTPUT) < 0)
		goto err;
	if (add_item(list, CONFIG_ITEM_VIDEO_OUTPUT) < 0)
		goto err;
	if (add_item(list, CONFIG_ITEM_TV_ASPECT) < 0)
		goto err;
	if (add_item(list, CONFIG_ITEM_OSD_BITRATE) < 0)
		goto err;
	if (add_item(list, CONFIG_ITEM_OSD_CLOCK) < 0)
		goto err;
	if (add_item(list, CONFIG_ITEM_OSD_DEMUX_INFO) < 0)
		goto err;
	if (add_item(list, CONFIG_ITEM_OSD_PROGRESS) < 0)
		goto err;
	if (add_item(list, CONFIG_ITEM_OSD_PROGRAM) < 0)
		goto err;
	if (add_item(list, CONFIG_ITEM_OSD_TIMECODE) < 0)
		goto err;
	if (add_item(list, CONFIG_ITEM_BRIGHTNESS) < 0)
		goto err;
	if (add_item(list, CONFIG_ITEM_MYTHTV_CONTROL) < 0)
		goto err;
	if (add_item(list, CONFIG_ITEM_MYTHTV_PROGRAM) < 0)
		goto err;
	if (add_item(list, CONFIG_ITEM_VOLUME) < 0)
		goto err;
	if (add_item(list, CONFIG_ITEM_VIEWPORT) < 0)
		goto err;
	if (add_item(list, CONFIG_ITEM_THEME) < 0)
		goto err;
	if (add_item(list, CONFIG_ITEM_MYTHTV_IP) < 0)
		goto err;
	if (add_item(list, CONFIG_ITEM_MCLIENT_IP) < 0)
		goto err;
	if (add_item(list, CONFIG_ITEM_PLAYBACK_OSD) < 0)
		goto err;
	if (add_item(list, CONFIG_ITEM_PLAYBACK_PAUSE) < 0)
		goto err;
	if (add_item(list, CONFIG_ITEM_MYTHTV_SORT) < 0) 
		goto err;
	if (add_item(list, CONFIG_ITEM_MYTHTV_PROGRAMS) < 0) 
		goto err;
	if (add_item(list, CONFIG_ITEM_MYTHTV_RG_HIDE) < 0) 
		goto err;
	if (add_item(list, CONFIG_ITEM_MYTHTV_RG_SHOW) < 0) 
		goto err;
	if (add_item(list, CONFIG_ITEM_DISPLAY_TYPE) < 0) 
		goto err;
	if (add_item(list, CONFIG_ITEM_MYTHTV_FILTER) < 0) 
		goto err;
	if (add_item(list, CONFIG_ITEM_VLC_IP) < 0) 
		goto err;
	if (add_item(list, CONFIG_ITEM_VLC_VOPTS) < 0) 
		goto err;
	if (add_item(list, CONFIG_ITEM_VLC_AOPTS) < 0) 
		goto err;
	if (add_item(list, CONFIG_ITEM_VLC_VB) < 0) 
		goto err;
	if (add_item(list, CONFIG_ITEM_VLC_AB) < 0) 
		goto err;


	list->crc = 0;
	list->version = CONFIG_VERSION;
	list->crc = crc32(0, (void*)list, sizeof(*list)+list->buflen);

	/*
	 * Don't bother compressing small files
	 */
	if (list->buflen < 1024)
		compress = 0;

	if ((ret=save_config_to_file(list, file, compress)) < 0) {
		fprintf(stderr, "config file failed with error %d\n", ret);
		goto err;
	}

	free(list);

	printf("saved config file\n");

	return 0;

 err:
	fprintf(stderr, "failed to save config file\n");

	if (list)
		free(list);

	return -1;
}

static int
parse_config(config_list_t *config, int override)
{
	config_item_t *item;
	int offset = 0;

	item = (config_item_t*)config->buf;

	while (offset < config->buflen) {
		if (get_item(item, override) < 0) {
			fprintf(stderr, "failed on type %d\n", item->type);
			return -1;
		}
		offset += sizeof(*item) + item->buflen;
		item = (config_item_t*)((unsigned long)config->buf + offset);
	}

	return 0;
}

void
set_config(void)
{
	if (config->bitmask & CONFIG_SCREENSAVER)
		screensaver_timeout = config->screensaver_timeout;
	if (config->bitmask & CONFIG_MODE)
		av_set_mode(config->av_mode);
	if (config->bitmask & CONFIG_AUDIO_OUTPUT)
		audio_output_mode = config->av_audio_output;
	if (config->bitmask & CONFIG_VIDEO_OUTPUT)
		av_set_output(config->av_video_output);
	if (config->bitmask & CONFIG_TV_ASPECT)
		av_set_tv_aspect(config->av_tv_aspect);
	if (config->bitmask & CONFIG_OSD_BITRATE)
		osd_settings.bitrate = config->osd_bitrate;
	if (config->bitmask & CONFIG_OSD_CLOCK)
		osd_settings.clock = config->osd_clock;
	if (config->bitmask & CONFIG_OSD_DEMUX_INFO)
		osd_settings.demux_info = config->osd_demux_info;
	if (config->bitmask & CONFIG_OSD_PROGRESS)
		osd_settings.progress = config->osd_progress;
	if (config->bitmask & CONFIG_OSD_PROGRAM)
		osd_settings.program = config->osd_program;
	if (config->bitmask & CONFIG_OSD_TIMECODE)
		osd_settings.timecode = config->osd_timecode;
	if (config->bitmask & CONFIG_BRIGHTNESS)
		root_bright = config->brightness;
	if (config->bitmask & CONFIG_MYTHTV_CONTROL)
		mythtv_tcp_control = config->mythtv_tcp_control;
	if (config->bitmask & CONFIG_MYTHTV_PROGRAM)
		mythtv_tcp_program = config->mythtv_tcp_program;
	if (config->bitmask & CONFIG_VOLUME)
		volume = config->volume;
	if (config->bitmask & CONFIG_VIEWPORT)
		memcpy(viewport_edges, config->viewport,
		       sizeof(viewport_edges));
	if (config->bitmask & CONFIG_MYTHTV_IP)
		mythtv_server = strdup(config->mythtv_ip);
	if (config->bitmask & CONFIG_MCLIENT_IP)
		mclient_server = strdup(config->mclient_ip);
	if (config->bitmask & CONFIG_VLC_IP)
		mythtv_server = strdup(config->vlc_ip);
	if (config->bitmask & CONFIG_PLAYBACK_OSD)
		seek_osd_timeout = config->playback_osd;
	if (config->bitmask & CONFIG_PLAYBACK_PAUSE)
		pause_osd = config->playback_pause;
	if (config->bitmask & CONFIG_MYTHTV_SORT) 
		mythtv_sort = config->mythtv_sort;
	if (config->bitmask & CONFIG_MYTHTV_PROGRAMS) 
		show_sort = config->mythtv_programs;
	if (config->bitmask & CONFIG_STARTUP_SELECT) 
		startup_selection = config->startup_selection;
	if (config->bitmask & CONFIG_DISPLAY_TYPE) 
		display_type = config->display_type;
	if (config->bitmask & CONFIG_MYTHTV_FILTER) 
		mythtv_filter = config->mythtv_filter;
}

int
load_config_file(char *file, int override)
{
	struct stat sb;
	config_list_t *list, *config, *ptr;
	int fd;
	uint32_t crc;
	int len;

	if ((fd=open(file, O_RDONLY)) < 0)
		return -1;

	if ((list=(config_list_t*)malloc(CONFIG_MAX_SIZE+sizeof(*list))) == NULL)
		goto err;
	memset(list, 0, sizeof(*list));

	fstat(fd, &sb);
	read(fd, list, sb.st_size);

	if (list->magic != CONFIG_MAGIC) {
		fprintf(stderr, "invalid magic number\n");
		goto err;
	}

	if (list->version != CONFIG_VERSION) {
		fprintf(stderr, "invalid config version\n");
		goto err;
	}

	if (list->buflen != sb.st_size-sizeof(*list)) {
		fprintf(stderr, "invalid buflen %lu\n",
			(unsigned long)list->buflen);
		goto err;
	}

	if ((list->flags & CONFIG_FLAGS_PLATFORM) == 0) {
		fprintf(stderr, "data from invalid platform\n");
		goto err;
	}

	if (list->flags & CONFIG_FLAGS_COMPRESSED) {
		len = CONFIG_MAX_SIZE + sizeof(*ptr);
		if ((config=(config_list_t*)malloc(len)) == NULL)
			goto err;
		memset(config, 0, len);
		if (config_uncompress(list, config) < 0) {
			fprintf(stderr, "uncompress failed\n");
			goto err;
		}
		ptr = config;
	} else {
		ptr = list;
	}

	crc = ptr->crc;
	ptr->crc = 0;
	ptr->crc = crc32(0, (void*)ptr, sizeof(*ptr)+ptr->buflen);

	if (crc != ptr->crc) {
		fprintf(stderr, "invalid crc!\n");
		goto err;
	}

	if (parse_config(ptr, override) < 0) {
		fprintf(stderr, "invalid config data\n");
		goto err;
	}

	set_config();

	close(fd);

	printf("loaded config file\n");

	return 0;

 err:
	fprintf(stderr, "failed to load config file\n");

	if (fd >= 0)
		close(fd);

	return -1;
}
