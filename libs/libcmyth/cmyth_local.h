/*
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
#ident "$Id$"

#ifndef __CMYTH_LOCAL_H
#define __CMYTH_LOCAL_H

#include <unistd.h>
#include <cmyth.h>

/*
 * Some useful constants
 */
#define CMYTH_LONGLONG_LEN (sizeof("-18446744073709551616") - 1)
#define CMYTH_LONG_LEN (sizeof("-4294967296") - 1)
#define CMYTH_SHORT_LEN (sizeof("-65536") - 1)
#define CMYTH_BYTE_LEN (sizeof("-256") - 1)
#define CMYTH_TIMESTAMP_LEN (sizeof("YYYY-MM-DDTHH:MM:SS") - 1)

/*
 * Atomic operations for reference counts.  For now, these are not
 * atomic, since I need to put together the assembly code to do that
 * so the reference counts are not really thread safe, but this will
 * lay the groundwork.  These should be good enough for most uses, but
 * I do want to make them really atomic, at least on PPC and X86.
 */
typedef	unsigned cmyth_atomic_t;
static inline unsigned
__cmyth_atomic_increment(cmyth_atomic_t *ref)
{
    cmyth_atomic_t __val;
#if defined __i486__ || defined __i586__ || defined __i686__
	asm volatile (".byte 0xf0, 0x0f, 0xc1, 0x02" /*lock; xaddl %eax, (%edx) */
	     : "=a" (__val)
	     : "0" (1), "m" (*ref), "d" (ref)
	     : "memory");
	/* on the x86 __val is the pre-increment value, so normalize it. */
	++__val;
#elif defined __powerpc__
	asm volatile ("1:	lwarx   %0,0,%1\n"
		"	addic.   %0,%0,1\n"
		"	stwcx.  %0,0,%1\n"
		"	bne-    1b\n"
		"	isync\n"
		: "=&r" (__val)
		: "r" (ref)
		: "cc", "memory");
#else
	/*
	 * Don't know how to atomic increment for a generic architecture
	 * so punt and just increment the value.
	 */
#warning unknown architecture, atomic increment is not...
    __val = ++(*ref);
#endif
	return __val;
}

static inline unsigned
__cmyth_atomic_decrement(cmyth_atomic_t *ref)
{
    cmyth_atomic_t __val;
#if defined __i486__ || defined __i586__ || defined __i686__
	/*
     * This opcode exists as a .byte instead of as a mnemonic for the
     * benefit of SCO OpenServer 5.  The system assembler (which is
     * essentially required on this target) can't assemble xaddl in
     * COFF mode.
	 */
     asm volatile (".byte 0xf0, 0x0f, 0xc1, 0x02" /*lock; xaddl %eax, (%edx) */
	    : "=a" (__val)
	    : "0" (-1), "m" (*ref), "d" (ref)
	    : "memory");
	/* __val is the pre-decrement value, so normalize it */
	--__val;
#elif defined __powerpc__
	asm volatile ("1:	lwarx   %0,0,%1\n"
		"	addic.   %0,%0,-1\n"
		"	stwcx.  %0,0,%1\n"
		"	bne-    1b\n"
		"	isync\n"
		: "=&r" (__val)
		: "r" (ref)
		: "cc", "memory");
#elif defined __sparcv9__
	cmyth_atomic_t __newval, __oldval = (*ref);
	do
	  {
	    __newval = __oldval - 1;
	    __asm__ ("cas	[%4], %2, %0"
		     : "=r" (__oldval), "=m" (*ref)
		     : "r" (__oldval), "m" (*ref), "r"((ref)), "0" (__newval));
	  }
	while (__newval != __oldval);
	/*  The value for __val is in '__oldval' */
	__val = __oldval;
#else
	/*
	 * Don't know how to atomic decrement for a generic architecture
	 * so punt and just decrement the value.
	 */
#warning unknown architecture, atomic deccrement is not...
    __val = --(*ref);
#endif
	return __val;
}
#define cmyth_atomic_inc __cmyth_atomic_inc
static inline void cmyth_atomic_inc(cmyth_atomic_t *a) {
	__cmyth_atomic_increment(a);
};

#define cmyth_atomic_dec_and_test __cmyth_atomic_dec_and_test
static inline int cmyth_atomic_dec_and_test(cmyth_atomic_t *a) {
	return (__cmyth_atomic_decrement(a) == 0);
};

#define cmyth_atomic_set __cmyth_atomic_set
static inline void cmyth_atomic_set(cmyth_atomic_t *a, unsigned val) {
	*a = val;
};

/*
 * Data structures
 */
struct cmyth_conn {
	int	conn_fd;
	unsigned char *conn_buf;
	int conn_buflen;
	int conn_len;
	int conn_pos;
	cmyth_atomic_t refcount;
    unsigned long conn_version;
};

struct cmyth_recorder {
	unsigned rec_have_stream;
	unsigned rec_id;
	char *rec_server;
	int rec_port;
	cmyth_ringbuf_t rec_ring;
	cmyth_conn_t rec_data;
	double rec_framerate;
	cmyth_atomic_t refcount;
};

struct cmyth_ringbuf {
	char *ringbuf_url;
	unsigned ringbuf_id;
	unsigned long long ringbuf_size;
	unsigned long long ringbuf_start;
	unsigned long long ringbuf_end;
	cmyth_atomic_t refcount;
};

struct cmyth_rec_num {
	char *recnum_host;
	unsigned short recnum_port;
	unsigned recnum_id;
	cmyth_atomic_t refcount;
};

struct cmyth_keyframe {
	unsigned long keyframe_number;
	unsigned long long keyframe_pos;
	cmyth_atomic_t refcount;
};

struct cmyth_posmap {
	unsigned posmap_count;
	struct cmyth_keyframe **posmap_list;
	cmyth_atomic_t refcount;
};

struct cmyth_freespace {
	unsigned long long freespace_total;
	unsigned long long freespace_used;
	cmyth_atomic_t refcount;
};

struct cmyth_timestamp {
	unsigned long timestamp_year;
	unsigned long timestamp_month;
	unsigned long timestamp_day;
	unsigned long timestamp_hour;
	unsigned long timestamp_minute;
	unsigned long timestamp_second;
	cmyth_atomic_t refcount;
};

struct cmyth_proginfo {
	char *proginfo_title;
	char *proginfo_subtitle;
	char *proginfo_description;
	char *proginfo_category;
	long proginfo_chanId;
	char *proginfo_chanstr;
	char *proginfo_chansign;
	char *proginfo_channame;  /* Deprecated in V8, simulated for compat. */
	char *proginfo_chanicon;  /* New in V8 */
	char *proginfo_url;
	long long proginfo_Start;
	long long proginfo_Length;
	cmyth_timestamp_t proginfo_start_ts;
	cmyth_timestamp_t proginfo_end_ts;
	unsigned long proginfo_conflicting; /* Deprecated in V8, always 0 */
    unsigned char *proginfo_unknown_0;   /* May be new 'conflicting' in V8 */
	unsigned long proginfo_recording;
	unsigned long proginfo_override;
	char *proginfo_hostname;
	long proginfo_source_id; /* ??? in V8 */
	long proginfo_card_id;   /* ??? in V8 */
	long proginfo_input_id;  /* ??? in V8 */
	char *proginfo_rec_priority;  /* ??? in V8 */
	unsigned long proginfo_rec_status; /* ??? in V8 */
	unsigned long proginfo_record_id;  /* ??? in V8 */
	unsigned long proginfo_rec_type;   /* ??? in V8 */
	unsigned long proginfo_rec_dups;   /* ??? in V8 */
	unsigned long proginfo_unknown_1;  /* new in V8 */
	cmyth_timestamp_t proginfo_rec_start_ts;
	cmyth_timestamp_t proginfo_rec_end_ts;
	unsigned long proginfo_repeat;   /* ??? in V8 */
	long proginfo_program_flags;
    char *proginfo_rec_profile;  /* new in V8 */
	char *proginfo_unknown_2;    /* new in V8 */
	char *proginfo_unknown_3;    /* new in V8 */
	char *proginfo_unknown_4;    /* new in V8 */
	char *proginfo_unknown_5;    /* new in V8 */
	char *proginfo_pathname;
	int proginfo_port;
	char *proginfo_host;
    unsigned long proginfo_version;
	cmyth_atomic_t refcount;
};

struct cmyth_proglist {
	cmyth_proginfo_t *proglist_list;
	long proglist_count;
	cmyth_atomic_t refcount;
};

struct cmyth_file {
	cmyth_conn_t file_data;
	long file_id;
	unsigned long long file_start;
	unsigned long long file_length;
	unsigned long long file_pos;
	cmyth_atomic_t refcount;
};

/*
 * Private funtions in debug.c
 */
#define cmyth_dbg __cmyth_dbg
extern void cmyth_dbg(int level, char *fmt, ...);

/*
 * Private funtions in socket.c
 */
#define cmyth_send_message __cmyth_send_message
extern int cmyth_send_message(cmyth_conn_t conn, char *request);

#define cmyth_rcv_length __cmyth_rcv_length
extern int cmyth_rcv_length(cmyth_conn_t conn);

#define cmyth_rcv_string __cmyth_rcv_string
extern int cmyth_rcv_string(cmyth_conn_t conn,
							int *err,
							char *buf, int buflen,
							int count);

#define cmyth_rcv_okay __cmyth_rcv_okay
extern int cmyth_rcv_okay(cmyth_conn_t conn, char *ok);

#define cmyth_rcv_version __cmyth_rcv_version
extern int cmyth_rcv_version(cmyth_conn_t conn, unsigned long *vers);

#define cmyth_rcv_byte __cmyth_rcv_byte
extern int cmyth_rcv_byte(cmyth_conn_t conn, int *err, char *buf, int count);

#define cmyth_rcv_short __cmyth_rcv_short
extern int cmyth_rcv_short(cmyth_conn_t conn, int *err, short *buf, int count);

#define cmyth_rcv_long __cmyth_rcv_long
extern int cmyth_rcv_long(cmyth_conn_t conn, int *err, long *buf, int count);

#define cmyth_rcv_long_long __cmyth_rcv_long_long
extern int cmyth_rcv_long_long(cmyth_conn_t conn, int *err, long long *buf,
							   int count);

#define cmyth_rcv_ubyte __cmyth_rcv_ubyte
extern int cmyth_rcv_ubyte(cmyth_conn_t conn, int *err, unsigned char *buf,
						   int count);

#define cmyth_rcv_ushort __cmyth_rcv_ushort
extern int cmyth_rcv_ushort(cmyth_conn_t conn, int *err, unsigned short *buf,
							int count);

#define cmyth_rcv_ulong __cmyth_rcv_ulong
extern int cmyth_rcv_ulong(cmyth_conn_t conn, int *err, unsigned long *buf,
						   int count);

#define cmyth_rcv_ulong_long __cmyth_rcv_ulong_long
extern int cmyth_rcv_ulong_long(cmyth_conn_t conn,
								int *err,
								unsigned long long *buf,
								int count);

#define cmyth_rcv_data __cmyth_rcv_data
extern int cmyth_rcv_data(cmyth_conn_t conn, int *err, unsigned char *buf,
						  int count);

#define cmyth_rcv_timestamp __cmyth_rcv_timestamp
extern int cmyth_rcv_timestamp(cmyth_conn_t conn, int *err,
							   cmyth_timestamp_t buf,
							   int count);

#define cmyth_rcv_proginfo __cmyth_rcv_proginfo
extern int cmyth_rcv_proginfo(cmyth_conn_t conn, int *err,
							  cmyth_proginfo_t buf,
							  int count);

#define cmyth_rcv_chaninfo __cmyth_rcv_chaninfo
extern int cmyth_rcv_chaninfo(cmyth_conn_t conn, int *err,
							  cmyth_proginfo_t buf,
							  int count);

#define cmyth_rcv_proglist __cmyth_rcv_proglist
extern int cmyth_rcv_proglist(cmyth_conn_t conn, int *err,
							  cmyth_proglist_t buf,
							  int count);

#define cmyth_rcv_keyframe __cmyth_rcv_keyframe
extern int cmyth_rcv_keyframe(cmyth_conn_t conn, int *err,
							  cmyth_keyframe_t buf,
							  int count);

#define cmyth_rcv_freespace __cmyth_rcv_freespace
extern int cmyth_rcv_freespace(cmyth_conn_t conn, int *err,
							   cmyth_freespace_t buf,
							   int count);

#define cmyth_rcv_recorder __cmyth_rcv_recorder
extern int cmyth_rcv_recorder(cmyth_conn_t conn, int *err,
							  cmyth_recorder_t buf,
							  int count);

#define cmyth_rcv_ringbuf __cmyth_rcv_ringbuf
extern int cmyth_rcv_ringbuf(cmyth_conn_t conn, int *err, cmyth_ringbuf_t buf,
							 int count);

/*
 * From proginfo.c
 */
#define cmyth_proginfo_string __cmyth_proginfo_string
extern char *cmyth_proginfo_string(cmyth_proginfo_t prog);

#define cmyth_chaninfo_string __cmyth_chaninfo_string
extern char *cmyth_chaninfo_string(cmyth_proginfo_t prog);

/*
 * From file.c
 */
#define cmyth_file_create __cmyth_file_create
extern cmyth_file_t cmyth_file_create(void);

#endif /* __CMYTH_LOCAL_H */
