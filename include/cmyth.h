/*
 *  Copyright (C) 2004-2006, Eric Lund, Jon Gettler
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

/** \file cmyth.h
 * A C library for communicating with a MythTV server.
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

/* Sergio: Added to support the new livetv protocol */
struct cmyth_livetv_chain;
typedef struct cmyth_livetv_chain *cmyth_livetv_chain_t;

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

struct cmyth_database;
typedef struct cmyth_database *cmyth_database_t;


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

typedef enum {
	CMYTH_EVENT_UNKNOWN = 0,
	CMYTH_EVENT_CLOSE = 1,
	CMYTH_EVENT_RECORDING_LIST_CHANGE,
	CMYTH_EVENT_SCHEDULE_CHANGE,
	CMYTH_EVENT_DONE_RECORDING,
	CMYTH_EVENT_QUIT_LIVETV,
	CMYTH_EVENT_LIVETV_CHAIN_UPDATE,
	CMYTH_EVENT_SIGNAL,
} cmyth_event_t;

#define CMYTH_NUM_SORTS 2
typedef enum {
	MYTHTV_SORT_DATE_RECORDED = 0,
	MYTHTV_SORT_ORIGINAL_AIRDATE,
} cmyth_proglist_sort_t;

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


/* Sergio: Added to support the tvguide functionality */

struct cmyth_channel;
typedef struct cmyth_channel *cmyth_channel_t;

struct cmyth_chanlist;
typedef struct cmyth_chanlist *cmyth_chanlist_t;

struct cmyth_tvguide_progs;
typedef struct cmyth_tvguide_progs *cmyth_tvguide_progs_t;

struct cmyth_tvguide_program;
typedef struct cmyth_tvguide_program *cmyth_tvguide_program_t;

/*
 * -----------------------------------------------------------------
 * Allocation Related Operations
 *------------------------------------------------------------------
 */

/**
 * Release a reference to allocated memory.
 * \param p allocated memory
 */
extern void cmyth_release(void *p);

/**
 * Add a reference to allocated memory.
 * \param p allocated memory
 * \return new reference
 */
extern void *cmyth_hold(void *p);

/**
 * Duplicate a string using reference counted memory.
 * \param str string to duplicate
 * \return reference to the duplicated string
 */
extern char *cmyth_strdup(char *str);

/**
 * Allocate reference counted memory.
 * \param len allocation size
 * \param file filename
 * \param func function name
 * \param line line number
 * \return reference counted memory
 */
extern void *cmyth_allocate_data(size_t len, const char *file, const char *func, int line);

/**
 * Reallocate reference counted memory.
 * \param p allocated memory
 * \param len new allocation size
 * \return reference counted memory
 */
extern void *cmyth_reallocate(void *p, size_t len);

typedef void (*destroy_t)(void *p);

/**
 * Add a destroy callback for reference counted memory.
 * \param block allocated memory
 * \param func destroy function
 */
extern void cmyth_set_destroy(void *block, destroy_t func);

/**
 * Print allocation information to stdout.
 */
extern void cmyth_alloc_show(void);

/**
 * Allocate reference counted memory.
 * \param x allocation size
 * \return reference counted memory
 */
#if defined(DEBUG)
#define cmyth_allocate(x) cmyth_allocate_data(x, __FILE__, __FUNCTION__, __LINE__)
#else
#define cmyth_allocate(x) cmyth_allocate_data(x, NULL, NULL, 0)
#endif


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

/**
 * Set the libcmyth debug level.
 * \param l level
 */
extern void cmyth_dbg_level(int l);

/**
 * Turn on all libcmyth debugging.
 */
extern void cmyth_dbg_all(void);

/**
 * Turn off all libcmyth debugging.
 */
extern void cmyth_dbg_none(void);

/**
 * Print a libcmyth debug message.
 * \param level debug level
 * \param fmt printf style format
 */
extern void cmyth_dbg(int level, char *fmt, ...);

/*
 * -----------------------------------------------------------------
 * Connection Operations
 * -----------------------------------------------------------------
 */

/**
 * Create a control connection to a backend.
 * \param server server hostname or ip address
 * \param port port number to connect on
 * \param buflen buffer size for the connection to use
 * \param tcp_rcvbuf if non-zero, the TCP receive buffer size for the socket
 * \return control handle
 */
extern cmyth_conn_t cmyth_conn_connect_ctrl(char *server,
					    unsigned short port,
					    unsigned buflen, int tcp_rcvbuf);

/**
 * Create an event connection to a backend.
 * \param server server hostname or ip address
 * \param port port number to connect on
 * \param buflen buffer size for the connection to use
 * \param tcp_rcvbuf if non-zero, the TCP receive buffer size for the socket
 * \return event handle
 */
extern cmyth_conn_t cmyth_conn_connect_event(char *server,
					     unsigned short port,
					     unsigned buflen, int tcp_rcvbuf);

/**
 * Create a file connection to a backend.
 * \param prog program handle
 * \param control control handle
 * \param buflen buffer size for the connection to use
 * \param tcp_rcvbuf if non-zero, the TCP receive buffer size for the socket
 * \return file handle
 */
extern cmyth_file_t cmyth_conn_connect_file(cmyth_proginfo_t prog,
					    cmyth_conn_t control,
					    unsigned buflen, int tcp_rcvbuf);

/**
 * Create a ring buffer connection to a recorder.
 * \param rec recorder handle
 * \param buflen buffer size for the connection to use
 * \param tcp_rcvbuf if non-zero, the TCP receive buffer size for the socket
 * \retval 0 success
 * \retval -1 error
 */
extern int cmyth_conn_connect_ring(cmyth_recorder_t rec, unsigned buflen,
				   int tcp_rcvbuf);

/**
 * Create a connection to a recorder.
 * \param rec recorder to connect to
 * \param buflen buffer size for the connection to use
 * \param tcp_rcvbuf if non-zero, the TCP receive buffer size for the socket
 * \retval 0 success
 * \retval -1 error
 */
extern int cmyth_conn_connect_recorder(cmyth_recorder_t rec,
				       unsigned buflen, int tcp_rcvbuf);

/**
 * Check whether a block has finished transfering from a backend.
 * \param conn control handle
 * \param size size of block
 * \retval 0 not complete
 * \retval 1 complete
 * \retval <0 error
 */
extern int cmyth_conn_check_block(cmyth_conn_t conn, unsigned long size);

/**
 * Obtain a recorder from a connection by its recorder number.
 * \param conn connection handle
 * \param num recorder number
 * \param recorder handle
 */
extern cmyth_recorder_t cmyth_conn_get_recorder_from_num(cmyth_conn_t conn,
							 int num);

/**
 * Obtain the next available free recorder on a backend.
 * \param conn connection handle
 * \return recorder handle
 */
extern cmyth_recorder_t cmyth_conn_get_free_recorder(cmyth_conn_t conn);

/**
 * Get the amount of free disk space on a backend.
 * \param control control handle
 * \param[out] total total disk space
 * \param[out] used used disk space
 * \retval 0 success
 * \retval <0 error
 */
extern int cmyth_conn_get_freespace(cmyth_conn_t control,
				    long long *total, long long *used);

/**
 * Determine if a control connection is not responding.
 * \param control control handle
 * \retval 0 not hung
 * \retval 1 hung
 * \retval <0 error
 */
extern int cmyth_conn_hung(cmyth_conn_t control);

/**
 * Determine the number of free recorders.
 * \param conn connection handle
 * \return number of free recorders
 */
extern int cmyth_conn_get_free_recorder_count(cmyth_conn_t conn);

/**
 * Determine the MythTV protocol version being used.
 * \param conn connection handle
 * \return protocol version
 */
extern int cmyth_conn_get_protocol_version(cmyth_conn_t conn);

/*
 * -----------------------------------------------------------------
 * Event Operations
 * -----------------------------------------------------------------
 */

/**
 * Retrieve an event from a backend.
 * \param conn connection handle
 * \param[out] data data, if the event returns any
 * \param len size of data buffer
 * \return event type
 */
extern cmyth_event_t cmyth_event_get(cmyth_conn_t conn, char * data, int len);

/*
 * -----------------------------------------------------------------
 * Recorder Operations
 * -----------------------------------------------------------------
 */

/**
 * Create a new recorder.
 * \return recorder handle
 */
extern cmyth_recorder_t cmyth_recorder_create(void);

/**
 * Duplicaate a recorder.
 * \param p recorder handle
 * \return duplicated recorder handle
 */
extern cmyth_recorder_t cmyth_recorder_dup(cmyth_recorder_t p);

/**
 * Determine if a recorder is in use.
 * \param rec recorder handle
 * \retval 0 not recording
 * \retval 1 recording
 * \retval <0 error
 */
extern int cmyth_recorder_is_recording(cmyth_recorder_t rec);

/**
 * Determine the framerate for a recorder.
 * \param rec recorder handle
 * \param[out] rate framerate
 * \retval 0 success
 * \retval <0 error
 */
extern int cmyth_recorder_get_framerate(cmyth_recorder_t rec,
					double *rate);

extern long long cmyth_recorder_get_frames_written(cmyth_recorder_t rec);

extern long long cmyth_recorder_get_free_space(cmyth_recorder_t rec);

extern long long cmyth_recorder_get_keyframe_pos(cmyth_recorder_t rec,
						 unsigned long keynum);

extern cmyth_posmap_t cmyth_recorder_get_position_map(cmyth_recorder_t rec,
						      unsigned long start,
						      unsigned long end);

extern cmyth_proginfo_t cmyth_recorder_get_recording(cmyth_recorder_t rec);

extern int cmyth_recorder_stop_playing(cmyth_recorder_t rec);

extern int cmyth_recorder_frontend_ready(cmyth_recorder_t rec);

extern int cmyth_recorder_cancel_next_recording(cmyth_recorder_t rec);

/**
 * Request that the recorder stop transmitting data.
 * \param rec recorder handle
 * \retval 0 success
 * \retval <0 error
 */
extern int cmyth_recorder_pause(cmyth_recorder_t rec);

extern int cmyth_recorder_finish_recording(cmyth_recorder_t rec);

extern int cmyth_recorder_toggle_channel_favorite(cmyth_recorder_t rec);

/**
 * Request that the recorder change the channel being recorded.
 * \param rec recorder handle
 * \param direction direction in which to change channel
 * \retval 0 success
 * \retval <0 error
 */
extern int cmyth_recorder_change_channel(cmyth_recorder_t rec,
					 cmyth_channeldir_t direction);

/**
 * Set the channel for a recorder.
 * \param rec recorder handle
 * \param channame channel name to change to
 * \retval 0 success
 * \retval <0 error
 */
extern int cmyth_recorder_set_channel(cmyth_recorder_t rec,
				      char *channame);

extern int cmyth_recorder_change_color(cmyth_recorder_t rec,
				       cmyth_adjdir_t direction);

extern int cmyth_recorder_change_brightness(cmyth_recorder_t rec,
					    cmyth_adjdir_t direction);

extern int cmyth_recorder_change_contrast(cmyth_recorder_t rec,
					  cmyth_adjdir_t direction);

extern int cmyth_recorder_change_hue(cmyth_recorder_t rec,
				     cmyth_adjdir_t direction);

extern int cmyth_recorder_check_channel(cmyth_recorder_t rec,
					char *channame);

extern int cmyth_recorder_check_channel_prefix(cmyth_recorder_t rec,
					       char *channame);

/**
 * Request the current program info for a recorder.
 * \param rec recorder handle
 * \return program info handle
 */
extern cmyth_proginfo_t cmyth_recorder_get_cur_proginfo(cmyth_recorder_t rec);

/**
 * Request the next program info for a recorder.
 * \param rec recorder handle
 * \param current current program
 * \param direction direction of next program
 * \retval 0 success
 * \retval <0 error
 */
extern cmyth_proginfo_t cmyth_recorder_get_next_proginfo(
	cmyth_recorder_t rec,
	cmyth_proginfo_t curent,
	cmyth_browsedir_t direction);

extern int cmyth_recorder_get_input_name(cmyth_recorder_t rec,
					 char *name,
					 unsigned len);

extern long long cmyth_recorder_seek(cmyth_recorder_t rec,
				     long long pos,
				     cmyth_whence_t whence,
				     long long curpos);

extern int cmyth_recorder_spawn_chain_livetv(cmyth_recorder_t rec);

extern int cmyth_recorder_spawn_livetv(cmyth_recorder_t rec);

extern int cmyth_recorder_start_stream(cmyth_recorder_t rec);

extern int cmyth_recorder_end_stream(cmyth_recorder_t rec);
extern char*cmyth_recorder_get_filename(cmyth_recorder_t rec);
extern int cmyth_recorder_stop_livetv(cmyth_recorder_t rec);
extern int cmyth_recorder_done_ringbuf(cmyth_recorder_t rec);
extern int cmyth_recorder_get_recorder_id(cmyth_recorder_t rec);

/*
 * -----------------------------------------------------------------
 * Live TV Operations
 * -----------------------------------------------------------------
 */

extern cmyth_livetv_chain_t cmyth_livetv_chain_create(char * chainid);

extern cmyth_file_t cmyth_livetv_get_cur_file(cmyth_recorder_t rec);

extern int cmyth_livetv_chain_switch(cmyth_recorder_t rec, int dir);

extern int cmyth_livetv_chain_switch_last(cmyth_recorder_t rec);

extern int cmyth_livetv_chain_update(cmyth_recorder_t rec, char * chainid,
						int tcp_rcvbuf);

extern cmyth_recorder_t cmyth_livetv_chain_setup(cmyth_recorder_t old_rec,
						 int tcp_rcvbuf,
						 void (*prog_update_callback)(cmyth_proginfo_t));

extern int cmyth_livetv_get_block(cmyth_recorder_t rec, char *buf,
                                  unsigned long len);

extern int cmyth_livetv_select(cmyth_recorder_t rec, struct timeval *timeout);
  
extern int cmyth_livetv_request_block(cmyth_recorder_t rec, unsigned long len);

extern long long cmyth_livetv_seek(cmyth_recorder_t rec,
						long long offset, int whence);

extern int mythtv_new_livetv(void);

/*
 * -----------------------------------------------------------------
 * Database Operations 
 * -----------------------------------------------------------------
 */

extern cmyth_database_t cmyth_database_create(void);
extern cmyth_chanlist_t myth_load_channels2(cmyth_database_t db);
extern int cmyth_database_set_host(cmyth_database_t db, char *host);
extern int cmyth_database_set_user(cmyth_database_t db, char *user);
extern int cmyth_database_set_pass(cmyth_database_t db, char *pass);
extern int cmyth_database_set_name(cmyth_database_t db, char *name);

/*
 * -----------------------------------------------------------------
 * Ring Buffer Operations
 * -----------------------------------------------------------------
 */
extern char * cmyth_ringbuf_pathname(cmyth_recorder_t rec);

extern cmyth_ringbuf_t cmyth_ringbuf_create(void);

extern cmyth_recorder_t cmyth_ringbuf_setup(cmyth_recorder_t old_rec);

extern int cmyth_ringbuf_request_block(cmyth_recorder_t rec,
				       unsigned long len);

extern int cmyth_ringbuf_select(cmyth_recorder_t rec, struct timeval *timeout);

extern int cmyth_ringbuf_get_block(cmyth_recorder_t rec,
				   char *buf,
				   unsigned long len);

extern long long cmyth_ringbuf_seek(cmyth_recorder_t rec,
				    long long offset,
				    int whence);

/*
 * -----------------------------------------------------------------
 * Recorder Number Operations
 * -----------------------------------------------------------------
 */
extern cmyth_rec_num_t cmyth_rec_num_create(void);

extern cmyth_rec_num_t cmyth_rec_num_get(char *host,
					 unsigned short port,
					 unsigned id);

extern char *cmyth_rec_num_string(cmyth_rec_num_t rn);

/*
 * -----------------------------------------------------------------
 * Timestamp Operations
 * -----------------------------------------------------------------
 */
extern cmyth_timestamp_t cmyth_timestamp_create(void);

extern cmyth_timestamp_t cmyth_timestamp_from_string(char *str);

extern cmyth_timestamp_t cmyth_timestamp_from_longlong(long long l);

extern long long cmyth_timestamp_to_longlong(cmyth_timestamp_t ts);

extern int cmyth_timestamp_to_string(char *str, cmyth_timestamp_t ts);

extern int cmyth_datetime_to_string(char *str, cmyth_timestamp_t ts);

extern cmyth_timestamp_t cmyth_datetime_from_string(char *str);

extern int cmyth_timestamp_compare(cmyth_timestamp_t ts1,
				   cmyth_timestamp_t ts2);
/*
 * -----------------------------------------------------------------
 * Key Frame Operations
 * -----------------------------------------------------------------
 */
extern cmyth_keyframe_t cmyth_keyframe_create(void);

extern cmyth_keyframe_t cmyth_keyframe_tcmyth_keyframe_get(
	unsigned long keynum,
	unsigned long long pos);

extern char *cmyth_keyframe_string(cmyth_keyframe_t kf);

/*
 * -----------------------------------------------------------------
 * Position Map Operations
 * -----------------------------------------------------------------
 */
extern cmyth_posmap_t cmyth_posmap_create(void);

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

extern char *cmyth_proginfo_title(cmyth_proginfo_t prog);

extern char *cmyth_proginfo_subtitle(cmyth_proginfo_t prog);

extern char *cmyth_proginfo_description(cmyth_proginfo_t prog);

extern char *cmyth_proginfo_category(cmyth_proginfo_t prog);

extern char *cmyth_proginfo_chanstr(cmyth_proginfo_t prog);

extern char *cmyth_proginfo_chansign(cmyth_proginfo_t prog);

extern char *cmyth_proginfo_channame(cmyth_proginfo_t prog);

extern long cmyth_proginfo_chan_id(cmyth_proginfo_t prog);

extern char *cmyth_proginfo_pathname(cmyth_proginfo_t prog);

extern char *cmyth_proginfo_seriesid(cmyth_proginfo_t prog);

extern char *cmyth_proginfo_programid(cmyth_proginfo_t prog);

extern char *cmyth_proginfo_stars(cmyth_proginfo_t prog);

extern cmyth_timestamp_t cmyth_proginfo_rec_start(cmyth_proginfo_t prog);

extern cmyth_timestamp_t cmyth_proginfo_rec_end(cmyth_proginfo_t prog);

extern cmyth_timestamp_t cmyth_proginfo_originalairdate(cmyth_proginfo_t prog);

extern cmyth_proginfo_rec_status_t cmyth_proginfo_rec_status(
	cmyth_proginfo_t prog);

extern long long cmyth_proginfo_length(cmyth_proginfo_t prog);

extern char *cmyth_proginfo_host(cmyth_proginfo_t prog);

extern int cmyth_proginfo_compare(cmyth_proginfo_t a, cmyth_proginfo_t b);

extern int cmyth_proginfo_length_sec(cmyth_proginfo_t prog);

extern cmyth_proginfo_t cmyth_proginfo_get_detail(cmyth_conn_t control,
						  cmyth_proginfo_t p);

extern cmyth_timestamp_t cmyth_proginfo_start(cmyth_proginfo_t prog);

extern cmyth_timestamp_t cmyth_proginfo_end(cmyth_proginfo_t prog);

extern long cmyth_proginfo_card_id(cmyth_proginfo_t prog);

extern char *cmyth_proginfo_recgroup(cmyth_proginfo_t prog);
/*
 * -----------------------------------------------------------------
 * Program List Operations
 * -----------------------------------------------------------------
 */

extern cmyth_proglist_t cmyth_proglist_create(void);

extern cmyth_proglist_t cmyth_proglist_get_all_recorded(cmyth_conn_t control);

extern cmyth_proglist_t cmyth_proglist_get_all_pending(cmyth_conn_t control);

extern cmyth_proglist_t cmyth_proglist_get_all_scheduled(cmyth_conn_t control);

extern cmyth_proglist_t cmyth_proglist_get_conflicting(cmyth_conn_t control);

extern cmyth_proginfo_t cmyth_proglist_get_item(cmyth_proglist_t pl,
						int index);

extern int cmyth_proglist_delete_item(cmyth_proglist_t pl,
				      cmyth_proginfo_t prog);

extern int cmyth_proglist_get_count(cmyth_proglist_t pl);

extern int cmyth_proglist_sort(cmyth_proglist_t pl, int count,
			       cmyth_proglist_sort_t sort);

/*
 * -----------------------------------------------------------------
 * File Transfer Operations
 * -----------------------------------------------------------------
 */
extern cmyth_conn_t cmyth_file_data(cmyth_file_t file);

extern unsigned long long cmyth_file_start(cmyth_file_t file);

extern unsigned long long cmyth_file_length(cmyth_file_t file);

extern int cmyth_file_get_block(cmyth_file_t file, char *buf,
				unsigned long len);

extern int cmyth_file_request_block(cmyth_file_t file, unsigned long len);

extern long long cmyth_file_seek(cmyth_file_t file,
				 long long offset, int whence);

extern int cmyth_file_select(cmyth_file_t file, struct timeval *timeout);

/*
 * -----------------------------------------------------------------
 * Free Space Operations
 * -----------------------------------------------------------------
 */
extern cmyth_freespace_t cmyth_freespace_create(void);

/*
 * -------
 * Bookmark,Commercial Skip Operations
 * -------
 */
extern long long cmyth_get_bookmark(cmyth_conn_t conn, cmyth_proginfo_t prog);
extern int cmyth_set_bookmark(cmyth_conn_t conn, cmyth_proginfo_t prog,
	long long bookmark);

/*
 * mysql info
 */

#define PROGRAM_ADJUST  3600

struct program {
	unsigned long chanid;
	char callsign[30];
	char name[84];
	unsigned int sourceid;
	char title[150];
	char subtitle[150];
	char description[280];
	char starttime[35];
	char endtime[35];
	char programid[30];
	char seriesid[24];
	char category[84];
	int recording;
	char rec_status[2];
	int channum;
};

struct channel {
	int chanid;
	int channum;
	long sources; /* A bit array of recorders/tuners supporting the channel */
	char callsign[20];
	char name[64];
	char rec_status[4];
};

#endif /* __CMYTH_H */
