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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ts_demux.h"

#if 1 
#define PRINTF(x...) printf(x)
#else
#define PRINTF(x...)
#endif

#if 1
#define PRINTFERR(x...) printf(x)
#else
#define PRINTFERR(x...)
#endif


void ts_demux_pat_callback(void *private, dvbpsi_pat_t *pat);
void ts_demux_pmt_callback(void *private, dvbpsi_pmt_t *pmt);


void ts_demux_free_pat_decoder(ts_demux_handle_t *tshandle) {
  if (tshandle->pat_decoder) {
    PRINTF("Freeing PAT Decoder\n");
    dvbpsi_DetachPAT(tshandle->pat_decoder);
    tshandle->pat_decoder = NULL;
  }
}

void ts_demux_free_pmt_decoder(ts_demux_handle_t *tshandle) {
  if (tshandle->pmt_decoder) {
    PRINTF("Freeing PMT Decoder\n");
    dvbpsi_DetachPMT(tshandle->pmt_decoder);
    tshandle->pmt_decoder = NULL;
  }
}

void ts_demux_init_pat_decoder(ts_demux_handle_t *tshandle) {
  ts_demux_free_pat_decoder(tshandle);
  PRINTF("Initializing PAT Decoder\n");
  tshandle->pat_decoder = dvbpsi_AttachPAT(ts_demux_pat_callback, tshandle);
  tshandle->pmtpid = 0;
}

void ts_demux_init_pmt_decoder(ts_demux_handle_t *tshandle, int prognum, int pmtpid) {
  ts_demux_free_pmt_decoder(tshandle);
  PRINTF("Initializing PMT Decoder\n");
  tshandle->pmt_decoder = dvbpsi_AttachPMT(prognum, ts_demux_pmt_callback, tshandle);
  tshandle->pmtpid = pmtpid;
}

static void write_pes(uint8_t *buf, int count, void  *private) {

  ipack *p = (ipack *)private;

  ts_demux_handle_t *tshandle = p->tshandle;

  if (!tshandle) {
    PRINTFERR("%s called with NULL tshandle\n", __FUNCTION__);
    return;
  }
  if (tshandle->pes_buf == NULL) {
    PRINTFERR("%s called with NULL tshandle->pes_buf>\n", __FUNCTION__);
    return;
  }

  if (tshandle->pes_buf_pos + count > tshandle->pes_buf_len) {
    PRINTFERR("%s called with insufficent space in tshandle->pes_buf\n", __FUNCTION__);
    return;
  }

  /* Copy in the complete PES packet */
  memcpy (tshandle->pes_buf + tshandle->pes_buf_pos, buf, (size_t)count);

  /* Rewrite the Stream ID */
  if (p->streamid > 0) {
    *(tshandle->pes_buf + tshandle->pes_buf_pos + 3) = p->streamid;
  }
  
  /* Update the buffer pointer */
  tshandle->pes_buf_pos += count;
  return;
}


void demux_ts_packet(ts_demux_handle_t *tshandle, char *buf) {

  int pid;

  if (buf[0] != TS_SYNC_BYTE) {
    PRINTFERR("Out Of Sync\n");
    return;
  }

  pid = get_pid(buf + 1);

  if (!(buf[3] & 0x10)) // no payload?
    return;

  if (buf[1] & 0x80) {
    PRINTFERR("Error in TS for PID: %d\n", pid);
  }

  // Check continuity count
  unsigned char *cc = tshandle->cc + pid;
  if (*cc == 255)
    *cc = (buf[3] & 15);
  else {
    *cc = ((*cc) + 1) & 15;
    if (*cc != (buf[3] & 15)) {
      if ((pid == 0) || (pid == tshandle->pmtpid)) {
	/* MythTV generates PAT and PMT TS packets with incorrect continuity counts */
	/* we fix these, so libdvbpsi doesn't reject the packets */
	buf[3] = (buf[3] & 0xf0) | *cc;
      } else {
	/* Otherwise, this is a real corruption */
	PRINTFERR("pid %d cc %d expected cc %d actual\n", pid, *cc, buf[3] & 15);
	*cc = (buf[3] & 15);
      }
    }
  }

  if (pid == 0) {
    dvbpsi_PushPacket(tshandle->pat_decoder, buf);
    return;
  }

  if (pid == tshandle->pmtpid) {
    dvbpsi_PushPacket(tshandle->pmt_decoder, buf);
    return;
  }


#if 0
  if (pid == 0) {
    for (i = 0; i < 188; i++) {
      printf("%02x ", buf[i]);
      if ((i % 16) == 15)
	printf("\n");
    }
    printf("\n");
  }
#endif

  
  // Find ipack number being used for this PID
  unsigned char ipacknum = tshandle->ipack_index[pid];
  if (ipacknum == 255) {
    // PRINTF("Unexpected pid %d\n", pid);
    return;
  }
  ipack *p = &(tshandle->p[(int)ipacknum]);
    
  if (buf[1] & 0x40) {
    if (p->plength == MMAX_PLENGTH-6){
      p->plength = p->found-6;
      p->found = 0;
      send_ipack(p);
      reset_ipack(p);
    }
  }
  
  int off = 0;
  if (buf[3] & 0x20) {  // adaptation field?
    off = buf[4] + 1;
  }
        
  instant_repack(buf+4+off, TS_SIZE-4-off, p);
}


void ts_demux_pat_callback(void *private, dvbpsi_pat_t *pat) {

  ts_demux_handle_t *tshandle = (ts_demux_handle_t *) private;  
  int progcount = 0;
  dvbpsi_pat_program_t *program = pat->p_first_program;

  PRINTF("Decoding Program Association Table\n");

  while (program != NULL) {
    PRINTF("   prog %d pmtpid %d\n", program->i_number, program->i_pid);
    if (progcount == 0) {
      ts_demux_init_pmt_decoder(tshandle, program->i_number, program->i_pid);
    }
    program = program->p_next;
    progcount++;
  }

  if (progcount > 1) {
    PRINTFERR("Warning: decoding a multi program transport stream; only the first program will be used.\n");
  }
}


void ts_demux_allocate_next_ipack (ts_demux_handle_t *tshandle, int pid, int streamid) {
  unsigned char ipacknum;
  if (tshandle->numpids >= TS_MAXIPACKS) {
    PRINTFERR("Too many PIDs\n");
    return;
  }

  /* Allocate the next ipack */
  ipacknum = tshandle->numpids;
  PRINTF("    Allocating ipack %d to pid %d", ipacknum, pid);
  if (streamid)
    PRINTF("; forcing streamid to 0x%02x", streamid);
  PRINTF("\n");
  tshandle->ipack_index[pid] = ipacknum;
  tshandle->numpids++;

  /* Save the Stream ID, used later when re-writing */
  ipack *p = &(tshandle->p[(int)ipacknum]);  
  p->streamid = streamid;

  return;
}

void ts_demux_decode_descriptor(dvbpsi_descriptor_t *desc) {
  int i;
  PRINTF("    Descriptor Tag:%02x Data:", desc->i_tag);
  for (i = 0; i < desc->i_length; i++)
    PRINTF("%02x ", *(desc->p_data + i));
  PRINTF("\n");
}

void ts_demux_pmt_callback(void *private, dvbpsi_pmt_t *pmt) {
  int audio_type;

  ts_demux_handle_t *tshandle = (ts_demux_handle_t *) private;  
  int video_count = 0;
  int audio_count = 0;
  int subtitle_count = 0;
  dvbpsi_descriptor_t *desc;
  dvbpsi_pmt_es_t *es = pmt->p_first_es;

  int pcr_pid_added = 0;
  int es_added;

  PRINTF("Decoding Program Map Table\n");

  PRINTF("  pcr_pid = %d\n", pmt->i_pcr_pid);

  /* Log the PMT descriptors */
  desc = pmt->p_first_descriptor;
  while (desc != NULL) {
    ts_demux_decode_descriptor(desc);
    desc = desc->p_next;
  }
  
  while (es != NULL) {
    PRINTF("  Elementary Stream type %d has pid %d\n", es->i_type, es->i_pid);


    audio_type = -1;

    /* Log the ES descriptors */
    desc = es->p_first_descriptor;
    while (desc != NULL) {
      ts_demux_decode_descriptor(desc);

      /* Look for an ISO 639 Language Descriptor, and record the audio type */
      if (desc->i_tag == 0x0a) {
	audio_type = *(desc->p_data + 3);
      }

      desc = desc->p_next;
    }

    es_added = 0;

    /* Allocate IPacks to known stream types */
    if (es->i_type == 2) {
      ts_demux_allocate_next_ipack(tshandle, es->i_pid, 0xe0 + video_count);
      es_added = 1;
      video_count++;
    } else if ((es->i_type == 3) || (es->i_type == 4)) {

      switch (audio_type) {
      case 0:
	PRINTF("    Audio Type = reserved\n");
	break;
      case 1:
	PRINTF("    Audio Type = clean effects\n");
	break;
      case 2:
	PRINTF("    Audio Type = hearing impaired\n");
	break;
      case 3:
	PRINTF("    Audio Type = visual impaired\n");
	break;
      default:
	PRINTF("    Audio Type = unknown\n");
	break;
      }

      if (audio_type != 3) {
	ts_demux_allocate_next_ipack(tshandle, es->i_pid, 0xC0 + audio_count);
	es_added = 1;
      }
      
      audio_count++;
    } else if (es->i_type == 6) {
      // ts_demux_allocate_next_ipack(tshandle, es->i_pid, 0);
      // es_added = 1;
      PRINTF("    Subtitle decoding disabled\n");
      subtitle_count++;
    }

    if ((es_added) && (es->i_pid == pmt->i_pcr_pid))
      pcr_pid_added = 1;

    es = es->p_next;
  }

  if (!pcr_pid_added)
    ts_demux_allocate_next_ipack(tshandle, pmt->i_pcr_pid, 0);

  PRINTF("found %d video streams; %d audio streams; %d subtitle streams\n", video_count, audio_count, subtitle_count);

}

/* Initializes a  TS Demultiplexor */
/* Returns a ptr to the ts_demux_handle_t structure */
ts_demux_handle_t * ts_demux_init() {
  int i;
  ts_demux_handle_t *tshandle;

  if ((tshandle = malloc(sizeof(*tshandle))) == NULL)
    return NULL;

  for (i = 0; i < TS_MAXIPACKS; i++) {
    init_ipack(&(tshandle->p[i]), PES_SIZE, write_pes, 0, tshandle);
  }

  tshandle->pat_decoder = NULL;
  tshandle->pmt_decoder = NULL;

  ts_demux_reset(tshandle);

  return tshandle;
}

/* Tranforms a chunk of TS packets into a chunk of PES packets */
/* Returns the number of PES bytes generated, or -ve if a fatal error occurred */
int ts_demux_transform(ts_demux_handle_t *tshandle, char *ts_buf, int ts_buf_len, char *pes_buf, int pes_buf_len) {

  int i;
  int offset = 0;
  char *ptr1;
  char *ptr2;
  int fraglen;

  /* Initialize the tshandle structure with details of the PES buffer to be filled */
  tshandle->pes_buf = pes_buf;
  tshandle->pes_buf_len = pes_buf_len;
  tshandle->pes_buf_pos = 0;
  tshandle->resyncs = 0;

  /* Process the end of the last TS packet, if any */
  if (tshandle->frag_buf_pos > 0) {
    fraglen = (TS_SIZE - tshandle->frag_buf_pos);
    if (fraglen > ts_buf_len)
      fraglen = ts_buf_len;
    ptr1 = tshandle->frag_buf + tshandle->frag_buf_pos;
    ptr2 = ts_buf;
    for (i = 0; i < fraglen; i++)
      *ptr1++ = *ptr2++;
    tshandle->frag_buf_pos += fraglen;
    if (tshandle->frag_buf_pos < TS_SIZE)
      return 0;
    demux_ts_packet(tshandle, tshandle->frag_buf);
    offset = fraglen;
  }

  /* Process complete TS packets */

  int count=0;
  while (offset + TS_SIZE <= ts_buf_len) {

    // Check for sync
    if (ts_buf[offset] != TS_SYNC_BYTE) {
      PRINTFERR("Lost Sync\n");
      tshandle->resyncs++;
      while ((ts_buf[offset] != TS_SYNC_BYTE) && (offset < ts_buf_len))
	offset++;
      if (offset + TS_SIZE > ts_buf_len)
	break;
    }
    demux_ts_packet(tshandle, ts_buf + offset);
    offset += TS_SIZE;
    count++;
  }

  /* Store any fragment for next time */
  fraglen = ts_buf_len - offset;
  if (fraglen > 0) {
    ptr1 = tshandle->frag_buf;
    ptr2 = ts_buf + offset;
    for (i = 0; i < fraglen; i++)
      *ptr1++ = *ptr2++;
  }
  tshandle->frag_buf_pos = fraglen;

  //PRINTF("len=%d count=%d frag_buf_pos=%d\n", len, count, tshandle->frag_buf_pos);

  return tshandle->pes_buf_pos;
}

/* Resets the state of a TS Demuultiplexor */
void ts_demux_reset(ts_demux_handle_t *tshandle) {
  int i;

  tshandle->numpids = 0;

  for (i = 0; i < TS_MAXPIDS; i++) {
    tshandle->ipack_index[i] = 255;
    tshandle->cc[i] = 255;
  }

  for (i = 0; i < TS_MAXIPACKS; i++) {
    reset_ipack(&(tshandle->p[i]));
  }

  tshandle->frag_buf_pos = 0;

  tshandle->pes_buf = NULL;
  tshandle->pes_buf_pos = 0;
  tshandle->pes_buf_len = 0;

  ts_demux_init_pat_decoder(tshandle);
}


/* Releases a TS Demultiplexor */
void ts_demux_free(ts_demux_handle_t *tshandle) {
  int i;

  for (i = 0; i < TS_MAXIPACKS; i++) {
    free_ipack(&(tshandle->p[i]));
  }

  ts_demux_free_pat_decoder(tshandle);
  ts_demux_free_pmt_decoder(tshandle);

  free(tshandle);
}

/* Returns the last resync count */
int ts_demux_resync_count(ts_demux_handle_t *tshandle) {
  return tshandle->resyncs;
}

/* Determines whether the buffer contains MPEG2 TS Data */
int ts_demux_is_ts(ts_demux_handle_t *tshandle, char *ts_buf,int ts_buf_len) {

  int count;
  char *ptr1 = ts_buf;
  char *ptr2 = ts_buf;
  char *end = ts_buf + ts_buf_len;

  if (ts_buf_len < TS_SIZE * TS_DETECT_THRESHOLD) {
    PRINTFERR("Transport Mode Detection Failed; buffer to small (%d < minimum of %d)\n", ts_buf_len, TS_SIZE * TS_DETECT_THRESHOLD); 
    return TS_MODE_UNKNOWN;
  }

  while (ptr1 < end) {

    /* Skip to next possible sync byte */
    while ((ptr1 < end) && (*ptr1 != TS_SYNC_BYTE))
      ptr1++;

    ptr2 = ptr1;
    count = 0;
    while ((ptr2 < end) && (*ptr2 == TS_SYNC_BYTE) && (count < TS_DETECT_THRESHOLD)) {
      count++;
      ptr2 += TS_SIZE;
    }

    if (count == TS_DETECT_THRESHOLD)
      return TS_MODE_YES;

    ptr1++;
  }
  
  return TS_MODE_NO;
}
