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
	int w;
	int h;
	int scale;
	int x1;
	int y;
	int x;
	int y2;
	int x3;
	int y3;
	int x4;
	int y4;
} vid_pos_regs_t;

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
#define AV_GET_AUD_INFO		_IOR('a',10,int)
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
#define AV_GET_VID_SYNC		_IOR('v',39,int)
#define AV_SET_VID_STC		_IOW('v',40,uint64_t *)
#define AV_SET_VID_RATIO	_IOW('v',41,int)
#define AV_SET_VID_SYNC		_IOW('v',42,int)
#define AV_SET_VID_DISABLE_SYNC	_IOW('v',43,int)
#define AV_SET_VID_DISP_FMT	_IOW('v',45,int)
#define AV_SET_VID_RESET	_IOW('v',51,int)
#define AV_SET_VID_OUTPUT	_IOW('v',57,int)
#define AV_SET_VID_MODE		_IOW('v',58,int)
#define AV_GET_VID_REGS		_IOR('v',61,struct vid_regs*)
#define AV_SET_VID_COLORBAR	_IOW('v',62,int)
#define AV_SET_VID_DENC		_IOW('v',63,int)
#define AV_CHK_SCART		_IOW('v',64,int) 

/*
 * /dev/rawir
 */
#define IR_SET_LED		_IOW('i',21,int)

#endif /* STB_H */
