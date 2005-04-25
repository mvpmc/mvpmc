/*
 *  $Id$
 *
 *  Copyright (C) 2004, Eric Lund
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

#ifndef __CMYTH_H
#define __CMYTH_H

/*
 * -----------------------------------------------------------------
 * Types
 * -----------------------------------------------------------------
 */
struct cmyth_conn;
typedef struct cmyth_conn *cmyth_conn_t;

struct cmyth_recorder;
typedef struct cmyth_recorder *cmyth_recorder_t;

struct cmyth_ringbuf;
typedef struct cmyth_ringbuf *cmyth_ringbuf_t;

struct cmyth_rec_num;
typedef struct cmyth_rec_num *cmyth_rec_num_t;

struct cmyth_posmap;
typedef struct cmyth_posmap *cmyth_posmap_t;

struct cmyth_proginfo;
typedef struct cmyth_proginfo *cmyth_proginfo_t;

typedef enum {
	CHANNEL_DIRECTION_UP = 0,
	CHANNEL_DIRECTION_DOWN = 1,
	CHANNEL_DIRECTION_FAVORITE = 2,
	CHANNEL_DIRECTION_SAME = 4,
} cmyth_channeldir_t;

typedef enum {
	ADJ_DIRECTION_UP = 1,
	ADJ_DIRECTION_DOWN = 0,
} cmyth_adjdir_t;

typedef enum {
	BROWSE_DIRECTION_SAME = 0,
	BROWSE_DIRECTION_UP = 1,
	BROWSE_DIRECTION_DOWN = 2,
	BROWSE_DIRECTION_LEFT = 3,
	BROWSE_DIRECTION_RIGHT = 4,
	BROWSE_DIRECTION_FAVORITE = 5,
} cmyth_browsedir_t;

typedef enum {
	WHENCE_SET = 0,
	WHENCE_CUR = 1,
	WHENCE_END = 2,
} cmyth_whence_t;

struct cmyth_timestamp;
typedef struct cmyth_timestamp *cmyth_timestamp_t;

struct cmyth_keyframe;
typedef struct cmyth_keyframe *cmyth_keyframe_t;

struct cmyth_freespace;
typedef struct cmyth_freespace *cmyth_freespace_t;

struct cmyth_proglist;
typedef struct cmyth_proglist *cmyth_proglist_t;

struct cmyth_file;
typedef struct cmyth_file *cmyth_file_t;

/*
 * -----------------------------------------------------------------
 * Debug Output Control
 * -----------------------------------------------------------------
 */

/*
 * Debug level constants used to determine the level of debug tracing
 * to be done and the debug level of any given message.
 */

#define CMYTH_DBG_NONE  -1
#define CMYTH_DBG_ERROR  0
#define CMYTH_DBG_WARN   1
#define CMYTH_DBG_INFO   2
#define CMYTH_DBG_DETAIL 3
#define CMYTH_DBG_DEBUG  4
#define CMYTH_DBG_PROTO  5
#define CMYTH_DBG_ALL    6

extern void cmyth_dbg_level(int l);  /* Set a specific debug level */
extern void cmyth_dbg_all(void);     /* turn on all debuging */
extern void cmyth_dbg_none(void);    /* turn off all debugging */
extern void cmyth_dbg(int level, char *fmt, ...); /* print a debug msg */

/*
 * -----------------------------------------------------------------
 * Connection Operations
 * -----------------------------------------------------------------
 */

extern cmyth_conn_t cmyth_conn_hold(cmyth_conn_t p);

extern void cmyth_conn_release(cmyth_conn_t p);

extern cmyth_conn_t cmyth_conn_connect_ctrl(char *server,
											unsigned short port,
											unsigned buflen);

extern cmyth_file_t cmyth_conn_connect_file(cmyth_proginfo_t prog,
											unsigned buflen);
extern cmyth_conn_t cmyth_conn_connect_ring(cmyth_recorder_t rec,
					    unsigned buflen);

extern int cmyth_conn_check_block(cmyth_conn_t conn, unsigned long size);

extern cmyth_recorder_t cmyth_conn_get_recorder_from_num(cmyth_conn_t conn,
														 cmyth_rec_num_t num);

extern cmyth_recorder_t cmyth_conn_get_free_recorder(cmyth_conn_t conn);

extern int cmyth_conn_get_freespace(cmyth_conn_t control,
				    unsigned int *total, unsigned int *used);

extern int cmyth_conn_hung(cmyth_conn_t control);

/*
 * -----------------------------------------------------------------
 * Recorder Operations
 * -----------------------------------------------------------------
 */
extern cmyth_recorder_t cmyth_recorder_create(void);

extern void cmyth_recorder_release(cmyth_recorder_t p);

extern int cmyth_recorder_request_block(cmyth_conn_t control,
										cmyth_recorder_t rec,
										unsigned len);

extern int cmyth_recorder_is_recording(cmyth_conn_t control,
									   cmyth_recorder_t rec);

extern int cmyth_recorder_get_framerate(cmyth_conn_t control,
										cmyth_recorder_t rec,
										double *rate);

extern long long cmyth_recorder_get_frames_written(cmyth_conn_t control,
												   cmyth_recorder_t rec);

extern long long cmyth_recorder_get_free_space(cmyth_conn_t control,
											   cmyth_recorder_t rec);

extern long long cmyth_recorder_get_keyframe_pos(cmyth_conn_t control,
												 cmyth_recorder_t rec,
												 unsigned long keynum);
extern int cmyth_recorder_fill_position_map(cmyth_conn_t control,
											cmyth_recorder_t rec,
											unsigned long start,
											unsigned long end);
extern int cmyth_recorder_get_recording(cmyth_conn_t control,
										cmyth_recorder_t rec,
										cmyth_proginfo_t proginfo);

extern int cmyth_recorder_stop_playing(cmyth_conn_t control,
									   cmyth_recorder_t rec);

extern int cmyth_recorder_frontend_ready(cmyth_conn_t control,
										 cmyth_recorder_t rec);

extern int cmyth_recorder_cancel_next_recording(cmyth_conn_t control,
												cmyth_recorder_t rec);

extern int cmyth_recorder_pause(cmyth_conn_t control,
								cmyth_recorder_t rec);

extern int cmyth_recorder_finish_recording(cmyth_conn_t control,
										   cmyth_recorder_t rec);

extern int cmyth_recorder_toggle_channel_favorite(cmyth_conn_t control,
												  cmyth_recorder_t rec);

extern int cmyth_recorder_toggle_channel_favorite(cmyth_conn_t control,
												  cmyth_recorder_t rec);

extern int cmyth_recorder_change_channel(cmyth_conn_t control,
										 cmyth_recorder_t rec,
										 cmyth_channeldir_t direction);

extern int cmyth_recorder_set_channel(cmyth_conn_t control,
									  cmyth_recorder_t rec,
									  char *channame);

extern int cmyth_recorder_change_color(cmyth_conn_t control,
									   cmyth_recorder_t rec,
									   cmyth_adjdir_t direction);

extern int cmyth_recorder_change_brightness(cmyth_conn_t control,
											cmyth_recorder_t rec,
											cmyth_adjdir_t direction);

extern int cmyth_recorder_change_contrast(cmyth_conn_t control,
										  cmyth_recorder_t rec,
										  cmyth_adjdir_t direction);

extern int cmyth_recorder_change_hue(cmyth_conn_t control,
									 cmyth_recorder_t rec,
									 cmyth_adjdir_t direction);

extern int cmyth_recorder_check_channel(cmyth_conn_t control,
										cmyth_recorder_t rec,
										char *channame);

extern int cmyth_recorder_check_channel_prefix(cmyth_conn_t control,
											   cmyth_recorder_t rec,
											   char *channame);

extern int cmyth_recorder_get_program_info(cmyth_conn_t control,
										   cmyth_recorder_t rec,
										   cmyth_proginfo_t proginfo);

extern int cmyth_recorder_get_next_program_info(cmyth_conn_t control,
												cmyth_recorder_t rec,
												cmyth_proginfo_t proginfo,
												cmyth_browsedir_t direction);

extern int cmyth_recorder_get_input_name(cmyth_conn_t control,
										 cmyth_recorder_t rec,
										 char *name,
										 unsigned len);

extern long long cmyth_recorder_seek(cmyth_conn_t control,
									 cmyth_recorder_t rec,
									 long long pos,
									 cmyth_whence_t whence,
									 long long curpos);

extern int cmyth_recorder_spawn_livetv(cmyth_conn_t control,
									   cmyth_recorder_t rec);

extern int cmyth_recorder_start_stream(cmyth_conn_t control,
									   cmyth_recorder_t rec);

extern int cmyth_recorder_end_stream(cmyth_conn_t control,
									 cmyth_recorder_t rec);

/*
 * -----------------------------------------------------------------
 * Ring Buffer Operations
 * -----------------------------------------------------------------
 */
extern cmyth_ringbuf_t cmyth_ringbuf_create(void);

extern cmyth_ringbuf_t cmyth_ringbuf_hold(cmyth_ringbuf_t p);

extern void cmyth_ringbuf_release(cmyth_ringbuf_t p);

/*
 * -----------------------------------------------------------------
 * Recorder Number Operations
 * -----------------------------------------------------------------
 */
extern cmyth_rec_num_t cmyth_rec_num_create(void);

extern cmyth_rec_num_t cmyth_rec_num_hold(cmyth_rec_num_t p);

extern void cmyth_rec_num_release(cmyth_rec_num_t p);

extern void cmyth_rec_num_fill(cmyth_rec_num_t rn,
							   char *host,
							   unsigned short port,
							   unsigned id);

extern char *cmyth_rec_num_string(cmyth_rec_num_t rn);

/*
 * -----------------------------------------------------------------
 * Timestamp Operations
 * -----------------------------------------------------------------
 */
extern cmyth_timestamp_t cmyth_timestamp_create(void);

extern cmyth_timestamp_t cmyth_timestamp_hold(cmyth_timestamp_t p);

extern void cmyth_timestamp_release(cmyth_timestamp_t p);

extern int cmyth_timestamp_from_string(cmyth_timestamp_t ts, char *str);

extern int cmyth_timestamp_from_longlong(cmyth_timestamp_t ts, long long l);

extern long long cmyth_timestamp_to_longlong(cmyth_timestamp_t ts);

extern int cmyth_timestamp_to_string(char *str, cmyth_timestamp_t ts);

extern int cmyth_datetime_to_string(char *str, cmyth_timestamp_t ts);

extern int cmyth_datetime_from_string(cmyth_timestamp_t ts, char *str);

extern int cmyth_timestamp_compare(cmyth_timestamp_t ts1,
								   cmyth_timestamp_t ts2);
/*
 * -----------------------------------------------------------------
 * Key Frame Operations
 * -----------------------------------------------------------------
 */
extern cmyth_keyframe_t cmyth_keyframe_create(void);

extern cmyth_keyframe_t cmyth_keyframe_hold(cmyth_keyframe_t p);

extern void cmyth_keyframe_release(cmyth_keyframe_t p);

extern void cmyth_keyframe_fill(cmyth_keyframe_t kf,
								unsigned long keynum,
								unsigned long long pos);

extern char *cmyth_keyframe_string(cmyth_keyframe_t kf);

/*
 * -----------------------------------------------------------------
 * Position Map Operations
 * -----------------------------------------------------------------
 */
extern cmyth_posmap_t cmyth_posmap_create(void);

extern cmyth_posmap_t cmyth_posmap_hold(cmyth_posmap_t p);

extern void cmyth_posmap_release(cmyth_posmap_t p);

/*
 * -----------------------------------------------------------------
 * Program Info Operations
 * -----------------------------------------------------------------
 */

typedef enum {
	RS_DELETED = -5,
	RS_STOPPED = -4,
	RS_RECORDED = -3,
	RS_RECORDING = -2,
	RS_WILL_RECORD = -1,
	RS_DONT_RECORD = 1,
	RS_PREVIOUS_RECORDING = 2,
	RS_CURRENT_RECORDING = 3,
	RS_EARLIER_RECORDING = 4,
	RS_TOO_MANY_RECORDINGS = 5,
	RS_CANCELLED = 6,
	RS_CONFLICT = 7,
	RS_LATER_SHOWING = 8,
	RS_REPEAT = 9,
	RS_LOW_DISKSPACE = 11,
	RS_TUNER_BUSY = 12,
} cmyth_proginfo_rec_status_t;

extern cmyth_proginfo_t cmyth_proginfo_create(void);

extern cmyth_proginfo_t cmyth_proginfo_hold(cmyth_proginfo_t p);

extern void cmyth_proginfo_release(cmyth_proginfo_t p);

extern int cmyth_progrino_stop_recording(cmyth_conn_t control,
										 cmyth_proginfo_t prog);

extern int cmyth_proginfo_check_recording(cmyth_conn_t control,
										  cmyth_proginfo_t prog);

extern int cmyth_proginfo_delete_recording(cmyth_conn_t control,
										   cmyth_proginfo_t prog);

extern int cmyth_proginfo_forget_recording(cmyth_conn_t control,
										   cmyth_proginfo_t prog);

extern int cmyth_proginfo_get_recorder_num(cmyth_conn_t control,
										   cmyth_rec_num_t rnum,
										   cmyth_proginfo_t prog);

extern char *cmyth_proginfo_string(cmyth_proginfo_t prog);

extern char *cmyth_chaninfo_string(cmyth_proginfo_t prog);

extern const char *cmyth_proginfo_title(cmyth_proginfo_t prog);

extern const char *cmyth_proginfo_subtitle(cmyth_proginfo_t prog);

extern const char *cmyth_proginfo_description(cmyth_proginfo_t prog);

extern const char *cmyth_proginfo_category(cmyth_proginfo_t prog);

extern const char *cmyth_proginfo_chanstr(cmyth_proginfo_t prog);

extern const char *cmyth_proginfo_chansign(cmyth_proginfo_t prog);

extern const char *cmyth_proginfo_channame(cmyth_proginfo_t prog);

extern long cmyth_proginfo_chan_id(cmyth_proginfo_t prog);

extern const char *cmyth_proginfo_pathname(cmyth_proginfo_t prog);

extern const char *cmyth_proginfo_seriesid(cmyth_proginfo_t prog);

extern const char *cmyth_proginfo_programid(cmyth_proginfo_t prog);

extern const char *cmyth_proginfo_stars(cmyth_proginfo_t prog);

extern cmyth_timestamp_t cmyth_proginfo_rec_start(cmyth_proginfo_t prog);

extern cmyth_timestamp_t cmyth_proginfo_rec_end(cmyth_proginfo_t prog);

extern cmyth_timestamp_t cmyth_proginfo_originalairdate(cmyth_proginfo_t prog);

extern int cmyth_proginfo_rec_status(cmyth_proginfo_t prog);

extern long long cmyth_proginfo_length(cmyth_proginfo_t prog);

extern char *cmyth_proginfo_host(cmyth_proginfo_t prog);

extern int cmyth_proginfo_compare(cmyth_proginfo_t a, cmyth_proginfo_t b);
/*
 * -----------------------------------------------------------------
 * Program List Operations
 * -----------------------------------------------------------------
 */

extern cmyth_proglist_t cmyth_proglist_create(void);

extern cmyth_proglist_t cmyth_proglist_hold(cmyth_proglist_t p);

extern void cmyth_proglist_release(cmyth_proglist_t p);

extern int cmyth_proglist_get_all_recorded(cmyth_conn_t control,
										   cmyth_proglist_t proglist);

extern int cmyth_proglist_get_all_pending(cmyth_conn_t control,
										  cmyth_proglist_t proglist);

extern int cmyth_proglist_get_all_scheduled(cmyth_conn_t control,
											cmyth_proglist_t proglist);

extern int cmyth_proglist_get_conflicting(cmyth_conn_t control,
										  cmyth_proglist_t proglist);

extern cmyth_proginfo_t cmyth_proglist_get_item(cmyth_proglist_t pl,
												int index);

extern int cmyth_proglist_get_count(cmyth_proglist_t pl);

/*
 * -----------------------------------------------------------------
 * File Transfer Operations
 * -----------------------------------------------------------------
 */
extern cmyth_file_t cmyth_file_hold(cmyth_file_t p);

extern int cmyth_file_release(cmyth_conn_t control, cmyth_file_t file);

extern cmyth_conn_t cmyth_file_data(cmyth_file_t file);

extern unsigned long long cmyth_file_start(cmyth_file_t file);

extern unsigned long long cmyth_file_length(cmyth_file_t file);

extern int cmyth_file_get_block(cmyth_file_t file, char *buf,
				unsigned long len);

extern int cmyth_file_request_block(cmyth_conn_t control, cmyth_file_t file,
				    unsigned long len);

extern long long cmyth_file_seek(cmyth_conn_t control, cmyth_file_t file,
				 long long offset, int whence);

/*
 * -----------------------------------------------------------------
 * Free Space Operations
 * -----------------------------------------------------------------
 */
extern cmyth_freespace_t cmyth_freespace_create(void);

extern cmyth_freespace_t cmyth_freespace_hold(cmyth_freespace_t p);

extern void cmyth_freespace_release(cmyth_freespace_t p);

#endif /* __CMYTH_H */
