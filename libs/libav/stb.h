/*
 *  $Id$
 *
 *  Copyright (C) 2004, BtB, Jon Gettler
 *  http://mvpmc.sourceforge.net/
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef STB_H
#define STB_H

typedef struct {
	int y;
	int x;
	int w;
	int h;
} vid_rect_t;

typedef struct {
	vid_rect_t src;
	vid_rect_t dest;
} vid_pos_regs_t;

typedef enum {
	VID_DISPMODE_NORM,
	VID_DISPMODE_LETTERBOX,
	VID_DISPMODE_1_2 = 3,
	VID_DISPMODE_1_4 = 4,
	VID_DISPMODE_2x = 5,
	VID_DISPMODE_SCALE = 6,
	VID_DISPMODE_DISEXP
} vid_disp_mode_t;

typedef struct {
	unsigned int reserved           :30;
	unsigned int freeze             :1;
	unsigned int picture_start      :1;
} vid_status_t;

typedef struct {
	unsigned long dsp_status;
	unsigned long stream_decode_type;
	unsigned long sample_rate;
	unsigned long bit_rate;
	unsigned long raw[64/sizeof(unsigned long)];
} aud_status_t;

typedef enum {
	VID_SYNC_OFF,
	VID_SYNC_VID,
	VID_SYNC_AUD,
} vid_sync_t;

typedef enum {
	AUD_PCM_STEREO = 0,
	AUD_PCM_MONO,
} aud_pcm_channels_t;

typedef enum {
	AUD_PCM_20BIT = 0,
	AUD_PCM_18BIT,
	AUD_PCM_16BIT,
	AUD_PCM_8BIT,
} aud_pcm_bits_t;

typedef enum {
	AUD_PCM_SIGNED = 0,
	AUD_PCM_UNSIGNED,
} aud_pcm_signed_t;

typedef enum {
	AUD_PCM_BIG_ENDIAN = 0,
	AUD_PCM_LITTLE_ENDIAN,
} aud_pcm_endian_t;

typedef struct {
	aud_pcm_channels_t      channels;
	aud_pcm_bits_t          bits;
	int                     freq;
	aud_pcm_signed_t        sign;
	aud_pcm_endian_t        endian;
} aud_pcm_t;

/*
 * /dev/adec_mpeg
 */
#define AV_SET_AUD_STOP		_IOW('a',1,int)
#define AV_SET_AUD_PLAY		_IOW('a',2,int)
#define AV_SET_AUD_PAUSE	_IOW('a',3,int)
#define AV_SET_AUD_UNPAUSE	_IOW('a',4,int)
#define AV_SET_AUD_SRC		_IOW('a',5,int)
#define AV_SET_AUD_MUTE		_IOW('a',6,int)
#define AV_SET_AUD_BYPASS	_IOW('a',8,int)
#define AV_SET_AUD_CHANNEL	_IOW('a',9,int)
#define AV_GET_AUD_STATUS	_IOR('a',10,aud_status_t)
#define AV_SET_AUD_VOLUME	_IOW('a',13,int)
#define AV_GET_AUD_VOLUME	_IOR('a',14,int)
#define AV_SET_AUD_STREAMTYPE	_IOW('a',15,int)
#define AV_SET_AUD_FORMAT	_IOW('a',16,int)
#define AV_GET_AUD_SYNC		_IOR('a',21,pts_sync_data_t*)
#define AV_SET_AUD_STC		_IOW('a',22,uint64_t *)
#define AV_SET_AUD_SYNC		_IOW('a',23,int)
#define AV_SET_AUD_DISABLE_SYNC	_IOW('a',24,aud_sync_parms_t*)
#define AV_SET_AUD_END_STREAM	_IOW('a',25,int)
#define AV_SET_AUD_RESET	_IOW('a',26,int)
#define AV_SET_AUD_DAC_CLK	_IOW('a',27,int)
#define AV_GET_AUD_REGS		_IOW('a',28,aud_ctl_regs_t*)

/*
 * /dev/vdec_dev
 */
#define AV_SET_VID_STOP		_IOW('v',21,int)
#define AV_SET_VID_PLAY		_IOW('v',22,int)
#define AV_SET_VID_FREEZE	_IOW('v',23,int)
#define AV_SET_VID_RESUME	_IOW('v',24,int)
#define AV_SET_VID_SRC		_IOW('v',25,int)
#define AV_SET_VID_FB		_IOW('v',26,int) 
#define AV_GET_VID_STATE	_IOR('v',27,vid_state_regs_t*) 
#define AV_SET_VID_PAUSE	_IOW('v',28,int)
#define AV_SET_VID_FFWD		_IOW('v',29,int)
#define AV_SET_VID_SLOMO	_IOW('v',30,int)
#define AV_SET_VID_BLANK	_IOW('v',32,int)
#define AV_SET_VID_POSITION	_IOW('v',36,vid_pos_regs_t*)
#define AV_SET_VID_SCALE_ON	_IOW('v',37,int)
#define AV_SET_VID_SCALE_OFF	_IOW('v',38,int)
#define AV_GET_VID_SYNC		_IOR('v',39,vid_sync_t)
#define AV_SET_VID_STC		_IOW('v',40,uint64_t *)
#define AV_SET_VID_RATIO	_IOW('v',41,int)
#define AV_SET_VID_SYNC		_IOW('v',42,int)
#define AV_SET_VID_DISABLE_SYNC	_IOW('v',43,int)
#define AV_SET_VID_DISP_FMT	_IOW('v',45,int)
#define AV_SET_VID_RESET	_IOW('v',51,int)
#define AV_SET_VID_OUTPUT	_IOW('v',57,int)
#define AV_SET_VID_MODE		_IOW('v',58,int)
#define AV_GET_VID_STATUS	_IOR('v',61,vid_status_t*)
#define AV_SET_VID_COLORBAR	_IOW('v',62,int)
#define AV_SET_VID_DENC		_IOW('v',63,int)
#define AV_CHK_SCART		_IOW('v',64,int) 

/*
 * /dev/rawir
 */
#define IR_SET_LED		_IOW('i',21,int)

#endif /* STB_H */
