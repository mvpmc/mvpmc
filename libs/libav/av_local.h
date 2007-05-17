/*
 *  Copyright (C) 2004-2006 Jon Gettler
 *  http://www.mvpmc.org/
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

#ifndef AV_LOCAL_H
#define AV_LOCAL_H

#define fd_video		__av_fd_video
#define fd_audio		__av_fd_audio
#define state			__av_state
#define vid_mode		__av_vid_mode
#define tv_aspect		__av_tv_aspect
#define flicker			__av_flicker

extern int fd_video;
extern int fd_audio;
extern av_state_t state;
extern av_mode_t vid_mode;
extern av_tv_aspect_t tv_aspect;

short flicker;

#define init_mtd		__av_init_mtd
#define set_output_method	__av_set_output_method

extern int init_mtd(void);
extern int set_output_method(void);

#define AV_DEBUG(x) printf("%s: %s\n", __FUNCTION__,(x));fflush(stdout);

#endif /* AV_LOCAL_H */
