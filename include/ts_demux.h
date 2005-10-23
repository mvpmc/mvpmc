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

#ifndef DEMUX_TS_H
#define DEMUX_TS_H

/* Transport Stream Decoder Additions */

typedef struct ts_demux_handle_s ts_demux_handle_t;

/* Initializes a  TS Demultiplexor */
/* Returns a ptr to the ts_demux_handle_t structure */
ts_demux_handle_t * ts_demux_init();

/* Tranforms a chunk of TS packets into a chunk of PES packets */
/* Returns the number of PES bytes generated, or -ve if a fatal error occurred */
int ts_demux_transform(ts_demux_handle_t *tshandle, void *buf, int ts_buf_len, char *pes_buf, int pes_buf_len);

/* Resets the state of a TS Demuultiplexor */
void ts_demux_reset(ts_demux_handle_t *tshandle);

/* Releases a TS Demultiplexor */
void ts_demux_free(ts_demux_handle_t *tshandle);

/* Returns the last resync count */
int ts_demux_resync_count(ts_demux_handle_t *tshandle);

#define TS_MODE_UNKNOWN -1
#define TS_MODE_NO       0
#define TS_MODE_YES      1

/* Determines whether the buffer contains MPEG2 TS Data */
int ts_demux_is_ts(ts_demux_handle_t *tshandle, char *ts_buf, int ts_buf_len);

#endif /* DEMUX_TS_H */
