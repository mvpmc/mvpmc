/*
 * (c) Copyright 2005 David Banks, Brisol, UK.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *    1. Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *    2. Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *
 *    3. The name of the author may not be used to endorse or promote
 *       products derived from this software without specific prior
 *       written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef TS_DEMUX_H
#define TS_DEMUX_H

#include "transform.h"                   /* defined the ipack structure */

#include <dvbpsi/dvbpsi.h>               /* DVB PSI Library */
#include <dvbpsi/psi.h>                  /* DVB PSI Generic Structures */
#include <dvbpsi/descriptor.h>           /* DVB PSI Descriptors */
#include <dvbpsi/pat.h>                  /* DVB PSI PAT Table */
#include <dvbpsi/pmt.h>                  /* DVB PSI PMT Table */

#define TS_MAXPIDS 8192                  /* max value of a PID */
#define TS_SIZE 188                      /* length of a TS packet */
#define TS_MAXIPACKS 8                   /* max number of active PIDs that can be demuxed */
#define PES_SIZE 8192                    /* max size of PES packets generated */
#define TS_SYNC_BYTE 0x47                /* synch byte at the start of each TS packet */
#define TS_DETECT_THRESHOLD 10           /* Number of consecutive sync bytes needed to trigget TS detection */

#define TS_MODE_UNKNOWN -1
#define TS_MODE_NO       0
#define TS_MODE_YES      1


typedef struct  {
  int numpids;

  unsigned char ipack_index[TS_MAXPIDS]; /* per PID, index into ipack array */
  unsigned char cc[TS_MAXPIDS];          /* per PID, continuity counter */

  ipack p[TS_MAXIPACKS];                 /* ipack for each active PID */
  int frag_buf_pos;                      /* index into frag_buf */
  char frag_buf[TS_SIZE];                /* holds remaining TS bytes between calls */

  char *pes_buf;                         /* A pointer to the current PES buffer that has been passed in */
  int pes_buf_len;                       /* Size of this buffer */
  int pes_buf_pos;                       /* Current position for writing into this buffer */
  
  int resyncs;                           /* Number of TS stream resyncs during this transform */

  dvbpsi_handle pat_decoder;             /* PAT Table Decoder */
  dvbpsi_handle pmt_decoder;             /* PMT Table Decoder */

  unsigned short pmtpid;                 /* The PID of the PMT for the selected program */

} ts_demux_handle_t;

/* Initializes a  TS Demultiplexor */
/* Returns a ptr to the ts_demux_handle_t structure */
ts_demux_handle_t * ts_demux_init();

/* Tranforms a chunk of TS packets into a chunk of PES packets */
/* Returns the number of PES bytes generated, or -ve if a fatal error occurred */
int ts_demux_transform(ts_demux_handle_t *tshandle, char *ts_buf, int ts_buf_len, char *pes_buf, int pes_buf_len);

/* Resets the state of a TS Demuultiplexor */
void ts_demux_reset(ts_demux_handle_t *tshandle);

/* Releases a TS Demultiplexor */
void ts_demux_free(ts_demux_handle_t *tshandle);

/* Returns the last resync count */
int ts_demux_resync_count(ts_demux_handle_t *tshandle);

/* Determines whether the buffer contains MPEG2 TS Data */
int ts_demux_is_ts(ts_demux_handle_t *tshandle, char *ts_buf, int ts_buf_len);

#endif /* TS_DEMUX_H */
