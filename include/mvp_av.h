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

#ifndef MVP_AV_H
#define MVP_AV_H

#include <stdint.h>

typedef struct {
        uint64_t stc;
        uint64_t pts;
} pts_sync_data_t;

typedef struct {
        int nleft;
        int state;
} vid_state_regs_t;

extern int av_init(int);
extern int av_video_set_nonblock(int);
extern int av_audio_set_nonblock(int);
extern int av_attach_fb(void);
extern int av_play(void);
extern int av_audio_fd(void);
extern int av_video_fd(void);
extern int av_set_audio_type(int audio_mode);
extern int av_stop(void);
extern int av_pause(void);
extern int av_move(int x, int y, int video_mode);
extern int av_ffwd(void);
extern int av_mute(void);
extern int av_reset(void);

#endif /* MVP_AV_H */
