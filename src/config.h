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

#ifndef MVPMC_CONFIG_H
#define MVPMC_CONFIG_H

#include <limits.h>

#define CONFIG_MAGIC		0x434f4e46

typedef struct {
	char label[64];
	int hide;
} config_mythtv_rg_t;

/*
 * The bit definitions for the config_t bitmask
 */
#define CONFIG_SCREENSAVER		0x0000000000000001LL
#define CONFIG_MODE			0x0000000000000002LL
#define CONFIG_AUDIO_OUTPUT		0x0000000000000004LL
#define CONFIG_VIDEO_OUTPUT		0x0000000000000008LL
#define CONFIG_TV_ASPECT		0x0000000000000010LL
#define CONFIG_OSD_BITRATE		0x0000000000000020LL
#define CONFIG_OSD_CLOCK		0x0000000000000040LL
#define CONFIG_OSD_DEMUX_INFO		0x0000000000000080LL
#define CONFIG_OSD_PROGRESS		0x0000000000000100LL
#define CONFIG_OSD_PROGRAM		0x0000000000000200LL
#define CONFIG_OSD_TIMECODE		0x0000000000000400LL
#define CONFIG_BRIGHTNESS		0x0000000000000800LL
#define CONFIG_MYTHTV_CONTROL		0x0000000000001000LL
#define CONFIG_MYTHTV_PROGRAM		0x0000000000002000LL
#define CONFIG_VOLUME			0x0000000000004000LL
#define CONFIG_VIEWPORT			0x0000000000008000LL
#define CONFIG_THEME			0x0000000000010000LL
#define CONFIG_MYTHTV_IP		0x0000000000020000LL
#define CONFIG_MCLIENT_IP		0x0000000000040000LL
#define CONFIG_PLAYBACK_OSD		0x0000000000080000LL
#define CONFIG_PLAYBACK_PAUSE		0x0000000000100000LL
#define CONFIG_MYTHTV_SORT		0x0000000000200000LL
#define CONFIG_MYTHTV_PROGRAMS		0x0000000000400000LL
#define CONFIG_MYTHTV_RECGROUP		0x0000000000800000LL
#define CONFIG_STARTUP_SELECT		0x0000000001000000LL
#define CONFIG_DISPLAY_TYPE		0x0000000002000000LL
#define CONFIG_MYTHTV_FILTER		0x0000000004000000LL
#define CONFIG_VLC_IP			0x0000000008000000LL
#define CONFIG_VLC_VOPTS		0x0000000010000000LL
#define CONFIG_VLC_AOPTS		0x0000000020000000LL
#define CONFIG_VLC_VB			0x0000000040000000LL
#define CONFIG_VLC_AB			0x0000000080000000LL
#define CONFIG_WEATHER_LOCATION 	0x0000000100000000LL
#define CONFIG_MYTHTV_COMMSKIP  	0x0000000200000000LL
#define CONFIG_FRIENDLY_DATE 		0x0000000400000000LL
#define CONFIG_DURATION_MINUTES 	0x0000000800000000LL
#define CONFIG_MYTHTV_AUTOCOMMSKIP  	0x0000001000000000LL
#define CONFIG_MYTHTV_SEEK	  	0x0000002000000000LL
#define CONFIG_MYTHTV_DISABLE_ALL_COMMSKIP  	0x0000004000000000LL
#define CONFIG_MYTHTV_DISABLE_COMMSKIP_OSD  	0x0000008000000000LL
#define CONFIG_MYTHTV_DISABLE_BOOKMARK_OSD	0x0000010000000000LL
#define CONFIG_MYTHTV_CHECK_TUNER_TYPE 	0x0000020000000000LL

#define MYTHTV_RG_MAX	32

/*
 * The config_t structure will hold all the user settings that can survive
 * a crash or restart.  Eventually, this data will be written to flash or
 * a file so that it can survive a reboot.
 */
struct mysql_config_s {
        char host[64];
        char user[32];
        char pass[32];
        char db[24];
};

typedef struct {
	uint32_t		magic;
	uint64_t		bitmask;
	int			screensaver_timeout;
	av_mode_t		av_mode;
	av_passthru_t		av_audio_output;
	av_video_output_t	av_video_output;
	av_tv_aspect_t		av_tv_aspect;
	int			osd_bitrate;
	int			osd_clock;
	int			osd_demux_info;
	int			osd_progress;
	int			osd_program;
	int			osd_timecode;
	int			brightness;
	int			mythtv_tcp_control;
	int			mythtv_tcp_program;
	int			volume;
	unsigned short		viewport[4];
	char			theme[PATH_MAX];
	char			mythtv_ip[64];
	char			mclient_ip[64];
	int			playback_osd;
	int			playback_pause;
	int			mythtv_sort;
#ifndef MVPMC_HOST
	int			firsttime;/*First run since powerup?*/
#endif
	show_sort_t		mythtv_programs;
	config_mythtv_rg_t	mythtv_recgroup[MYTHTV_RG_MAX];
	int			startup_selection;
	int			display_type;
	int			use_12_hour_clock;
	mythtv_filter_t		mythtv_filter;
	mysql_config_t		mysql;

	char			vlc_ip[64];
	char			vlc_vopts[6];
	char			vlc_aopts[6];
	int			vlc_vb;
	int			vlc_ab;
	char			vlc_fps[8];
	char			weather_location[20];
	int			mythtv_commskip;
	int			mythtv_auto_commskip;
	int			mythtv_disable_all_commskip;
        int			mythtv_use_friendly_date;
        int			mythtv_use_duration_minutes;
	int			mythtv_seek;
	int			mythtv_disable_commskip_osd;
	int			mythtv_disable_bookmark_osd;
	int 			mythtv_check_tuner_type;
} config_t;

extern config_t *config;
#define mysqlptr (&(config->mysql))


/*
 * The item definitions for what can be stored in the config file.
 */
#define CONFIG_ITEM_SCREENSAVER		0x0001
#define CONFIG_ITEM_MODE		0x0002
#define CONFIG_ITEM_AUDIO_OUTPUT	0x0003
#define CONFIG_ITEM_VIDEO_OUTPUT	0x0004
#define CONFIG_ITEM_TV_ASPECT		0x0005
#define CONFIG_ITEM_OSD_BITRATE		0x0006
#define CONFIG_ITEM_OSD_CLOCK		0x0007
#define CONFIG_ITEM_OSD_DEMUX_INFO	0x0008
#define CONFIG_ITEM_OSD_PROGRESS	0x0009
#define CONFIG_ITEM_OSD_PROGRAM		0x000a
#define CONFIG_ITEM_OSD_TIMECODE	0x000b
#define CONFIG_ITEM_BRIGHTNESS		0x000c
#define CONFIG_ITEM_MYTHTV_CONTROL	0x000d
#define CONFIG_ITEM_MYTHTV_PROGRAM	0x000e
#define CONFIG_ITEM_VOLUME		0x000f
#define CONFIG_ITEM_VIEWPORT		0x0010
#define CONFIG_ITEM_THEME		0x0011
#define CONFIG_ITEM_MYTHTV_IP		0x0012
#define CONFIG_ITEM_MCLIENT_IP		0x0013
#define CONFIG_ITEM_PLAYBACK_OSD	0x0014
#define CONFIG_ITEM_PLAYBACK_PAUSE	0x0015
#define CONFIG_ITEM_MYTHTV_SORT		0x0016
#define CONFIG_ITEM_MYTHTV_PROGRAMS	0x0017
#define CONFIG_ITEM_MYTHTV_RG_HIDE	0x0018
#define CONFIG_ITEM_MYTHTV_RG_SHOW	0x0019
#define CONFIG_ITEM_DISPLAY_TYPE	0x0020
#define CONFIG_ITEM_MYTHTV_FILTER	0x0021
#define CONFIG_ITEM_VLC_IP		0x0022
#define CONFIG_ITEM_VLC_VOPTS		0x0023
#define CONFIG_ITEM_VLC_AOPTS		0x0024
#define CONFIG_ITEM_VLC_VB		0x0025
#define CONFIG_ITEM_VLC_AB		0x0026
#define CONFIG_ITEM_WEATHER_LOCATION    0x0027
#define CONFIG_ITEM_FRIENDLY_DATE	0x0028
#define CONFIG_ITEM_DURATION_MINUTES	0x0029
#define CONFIG_ITEM_MYTHTV_COMMSKIP	0x0030
#define CONFIG_ITEM_MYTHTV_AUTOCOMMSKIP	0x0031
#define CONFIG_ITEM_MYTHTV_SEEK		0x0032
#define CONFIG_ITEM_MYTHTV_DISABLE_ALL_COMMSKIP		0x0033
#define CONFIG_ITEM_MYTHTV_DISABLE_COMMSKIP_OSD 	0x0034
#define CONFIG_ITEM_MYTHTV_DISABLE_BOOKMARK_OSD 	0x0035
#define CONFIG_ITEM_MYTHTV_CHECK_TUNER_TYPE	 	0x0036


/*
 * The flags in config_list_t
 */
#ifdef MVPMC_HOST
#if __BYTE_ORDER == __LITTLE_ENDIAN
#define CONFIG_FLAGS_PLATFORM		0x0001
#else
#define CONFIG_FLAGS_PLATFORM		0x0008
#endif
#else
#define CONFIG_FLAGS_PLATFORM		0x0002
#endif
#define CONFIG_FLAGS_COMPRESSED		0x0004

#define CONFIG_MAX_SIZE			(1024*128)
#define CONFIG_MAX_COMPRESSED_SIZE	(1024*32)

#define CONFIG_VERSION			1

typedef struct {
	uint16_t	type;
	uint16_t	buflen;
	uint8_t		buf[0];
} config_item_t;

typedef struct {
	int		magic;
	int		crc;		/* uncompressed data with crc = 0 */
	int		version;
	uint32_t	buflen;
	uint32_t	flags;
	uint32_t	unused[3];
	uint8_t		buf[0];
} config_list_t;

extern int config_compress(config_list_t*, config_list_t*);
extern int config_uncompress(config_list_t*, config_list_t*);
extern int save_config_file(char *file);
extern int load_config_file(char *file, int override);
extern void set_config(void);

extern char *config_file;

#endif /* MVPMC_CONFIG_H */
