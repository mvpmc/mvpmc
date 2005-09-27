/*
 *  dvb-mpegtools for the Siemens Fujitsu DVB PCI card
 *
 * Copyright (C) 2000, 2001 Marcus Metzler 
 *            for convergence integrated media GmbH
 * Copyright (C) 2002  Marcus Metzler 
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 * Or, point your browser to http://www.gnu.org/copyleft/gpl.html
 * 

 * The author can be reached at marcus@convergence.de, 

 * the project's page is at http://linuxtv.org/dvb/
 */


#include "transform.h"
#include <stdlib.h>
#include <string.h>
#include "ctools.h"



unsigned int ac3_bitrates[32] =
    {32,40,48,56,64,80,96,112,128,160,192,224,256,320,384,448,512,576,640,
     0,0,0,0,0,0,0,0,0,0,0,0,0};

uint32_t ac3_freq[4] = {480, 441, 320, 0};
uint32_t ac3_frames[3][32] =
    {{64,80,96,112,128,160,192,224,256,320,384,448,512,640,768,896,1024,
      1152,1280,0,0,0,0,0,0,0,0,0,0,0,0,0},
     {69,87,104,121,139,174,208,243,278,348,417,487,557,696,835,975,1114,
      1253,1393,0,0,0,0,0,0,0,0,0,0,0,0,0},
     {96,120,144,168,192,240,288,336,384,480,576,672,768,960,1152,1344,
      1536,1728,1920,0,0,0,0,0,0,0,0,0,0,0,0,0}}; 

int get_ac3info(uint8_t *mbuf, int count, AudioInfo *ai, int pr)
{
	uint8_t *headr;
	int found = 0;
	int c = 0;
	uint8_t frame;
	int fr = 0;

	while ( !found  && c < count){
		uint8_t *b = mbuf+c;
		if ( b[0] == 0x0b &&  b[1] == 0x77 )
			found = 1;
		else {
			c++;
		}
	}	


	if (!found){
		return -1;
	}
	ai->off = c;

	if (c+5 >= count) return -1;

	ai->layer = 0;  // 0 for AC3
        headr = mbuf+c+2;

	frame = (headr[2]&0x3f);
	ai->bit_rate = ac3_bitrates[frame>>1]*1000;

	if (pr) fprintf (stderr,"  BRate: %d kb/s", ai->bit_rate/1000);

	fr = (headr[2] & 0xc0 ) >> 6;
	ai->frequency = ac3_freq[fr]*100;
	if (pr) fprintf (stderr,"  Freq: %d Hz\n", ai->frequency);

	ai->framesize = ac3_frames[fr][frame >> 1];
	if ((frame & 1) &&  (fr == 1)) ai->framesize++;
	ai->framesize = ai->framesize << 1;
	if (pr) fprintf (stderr,"  Framesize %d\n", ai->framesize);

	return c;
}







uint16_t get_pid(uint8_t *pid)
{
	uint16_t pp = 0;

	pp = (pid[0] & PID_MASK_HI)<<8;
	pp |= pid[1];

	return pp;
}


uint64_t trans_pts_dts(uint8_t *pts)
{
	uint64_t wts;
	
	wts = ((uint64_t)((pts[0] & 0x0E) << 5) | 
	       ((pts[1] & 0xFC) >> 2)) << 24; 
	wts |= (((pts[1] & 0x03) << 6) |
		((pts[2] & 0xFC) >> 2)) << 16; 
	wts |= (((pts[2] & 0x02) << 6) |
		((pts[3] & 0xFE) >> 1)) << 8;
	wts |= (((pts[3] & 0x01) << 7) |
		((pts[4] & 0xFE) >> 1));
	return wts;
}




void reset_ipack(ipack *p)
{
	p->found = 0;
	p->cid = 0;
	p->plength = 0;
	p->flag1 = 0;
	p->flag2 = 0;
	p->hlength = 0;
	p->mpeg = 0;
	p->check = 0;
	p->which = 0;
	p->done = 0;
	p->count = 0;
	p->size = p->size_orig;
}

void init_ipack(ipack *p, int size,
		void (*func)(uint8_t *buf,  int size, void *priv), int ps, void *tshandle)
{
	if ( !(p->buf = malloc(size)) ){
		fprintf(stderr,"Couldn't allocate memory for ipack\n");
		exit(1);
	}
	p->ps = ps;
	p->size_orig = size;
	p->func = func;
	reset_ipack(p);
	p->has_ai = 0;
	p->has_vi = 0;
	p->start = 0;
	p->tshandle = tshandle;
}

void free_ipack(ipack * p)
{
	if (p->buf) free(p->buf);
}


static void write_ipack(ipack *p, uint8_t *data, int count)
{
	AudioInfo ai;
	uint8_t headr[3] = { 0x00, 0x00, 0x01} ;
	int diff =0;

	if (p->count < 6){
		if (trans_pts_dts(p->pts) > trans_pts_dts(p->last_pts))
			memcpy(p->last_pts, p->pts, 5);
		p->count = 0;
		memcpy(p->buf+p->count, headr, 3);
		p->count += 6;
	}
	if ( p->size == p->size_orig && p->plength &&
	     (diff = 6+p->plength - p->found + p->count +count) > p->size &&
	     diff < 3*p->size/2){
		
			p->size = diff/2;
//			fprintf(stderr,"size: %d \n",p->size);
	}

	if (p->cid == PRIVATE_STREAM1 && p->count == p->hlength+9){
		switch ((data[0] & 0xF8)){
		case 0x80:
		case 0x20:
			break;

		default:
		{
			int ac3_off;
			
			ac3_off = get_ac3info(data, count, &ai,0);
			if (ac3_off>=0 && ai.framesize){
				p->buf[p->count] = 0x80;
				p->buf[p->count+1] = (p->size - p->count
						      - 4 - ac3_off)/ 
					ai.framesize + 1;
				p->buf[p->count+2] = (ac3_off >> 8)& 0xFF;
				p->buf[p->count+3] = (ac3_off)& 0xFF;
				p->count+=4;
				
			}
		}
		break;
		}
	}

	if (p->count + count < p->size){
		memcpy(p->buf+p->count, data, count); 
		p->count += count;
	} else {
		int rest = p->size - p->count;
		if (rest < 0) rest = 0;
		memcpy(p->buf+p->count, data, rest);
		p->count += rest;
//		fprintf(stderr,"count: %d \n",p->count);
		send_ipack(p);
		if (rest > 0 && count - rest > 0)
			write_ipack(p, data+rest, count-rest);
	}
}


void send_ipack(ipack *p)
{
	int streamid=0;
	int off;
	int ac3_off = 0;
	AudioInfo ai;
	int nframes= 0;
	int f=0;

	if (p->count < 10) return;
	p->buf[3] = p->cid;
	p->buf[4] = (uint8_t)(((p->count-6) & 0xFF00) >> 8);
	p->buf[5] = (uint8_t)((p->count-6) & 0x00FF);

	
	if (p->cid == PRIVATE_STREAM1){

		off = 9+p->buf[8];
		streamid = p->buf[off];
		switch (streamid & 0xF8){

		case 0x80:
			ai.off = 0;
			ac3_off = ((p->buf[off+2] << 8)| p->buf[off+3]);
			if (ac3_off < p->count)
				f=get_ac3info(p->buf+off+3+ac3_off, 
					      p->count-ac3_off, &ai,0);
			if ( !f ){
				nframes = (p->count-off-3-ac3_off)/ 
					ai.framesize + 1;
				p->buf[off+1] = nframes;
				p->buf[off+2] = (ac3_off >> 8)& 0xFF;
				p->buf[off+3] = (ac3_off)& 0xFF;
				
				ac3_off +=  nframes * ai.framesize - p->count;
			}
			break;

		case 0x20:
		default:
			break;
		}
		
	} 
	
	/* if (p->ps) ps_pes(p);
	   else */ 

	p->func(p->buf, p->count, p);

	switch ( p->mpeg ){
	case 2:		
		
		p->buf[6] = 0x80;
		p->buf[7] = 0x00;
		p->buf[8] = 0x00;
		p->count = 9;

		if (p->cid == PRIVATE_STREAM1){

			switch (streamid & 0xF8){

			case 0x80: 
				p->count += 4;
				p->buf[9] = streamid;
				p->buf[10] = 0;
				p->buf[11] = (ac3_off >> 8)& 0xFF;
				p->buf[12] = (ac3_off)& 0xFF;
				break;
				
			case 0x20:
				p->count += 2;
				p->buf[9] = 0x20;
				p->buf[10] = 0;				
				break;
			}
		}		
		break;

	case 1:
		p->buf[6] = 0x0F;
		p->count = 7;
		break;
	}

}



void instant_repack (uint8_t *buf, int count, ipack *p)
{

	int l;
	unsigned short *pl;
	int c=0;

	while (c < count && (p->mpeg == 0 ||
			     (p->mpeg == 1 && p->found < 7) ||
			     (p->mpeg == 2 && p->found < 9))
	       &&  (p->found < 5 || !p->done)){
		switch ( p->found ){
		case 0:
		case 1:
			if (buf[c] == 0x00) p->found++;
			else p->found = 0;
			c++;
			break;
		case 2:
			if (buf[c] == 0x01) p->found++;
			else if (buf[c] == 0){
				p->found = 2;
			} else p->found = 0;
			c++;
			break;
		case 3:
			p->cid = 0;
			switch (buf[c]){
			case PROG_STREAM_MAP:
			case PRIVATE_STREAM2:
			case PROG_STREAM_DIR:
			case ECM_STREAM     :
			case EMM_STREAM     :
			case PADDING_STREAM :
			case DSM_CC_STREAM  :
			case ISO13522_STREAM:
				p->done = 1;
			case PRIVATE_STREAM1:
			case VIDEO_STREAM_S ... VIDEO_STREAM_E:
			case AUDIO_STREAM_S ... AUDIO_STREAM_E:
				p->found++;
				p->cid = buf[c];
				c++;
				break;
			default:
				p->found = 0;
				break;
			}
			break;
			

		case 4:
			if (count-c > 1){
				pl = (unsigned short *) (buf+c);
				p->plength =  ntohs(*pl);
				p->plen[0] = buf[c];
				c++;
				p->plen[1] = buf[c];
				c++;
				p->found+=2;
			} else {
				p->plen[0] = buf[c];
				p->found++;
				return;
			}
			break;
		case 5:
			p->plen[1] = buf[c];
			c++;
			pl = (unsigned short *) p->plen;
			p->plength = ntohs(*pl);
			p->found++;
			break;


		case 6:
			if (!p->done){
				p->flag1 = buf[c];
				c++;
				p->found++;
				if ( (p->flag1 & 0xC0) == 0x80 ) p->mpeg = 2;
				else {
					p->hlength = 0;
					p->which = 0;
					p->mpeg = 1;
					p->flag2 = 0;
				}
			}
			break;

		case 7:
			if ( !p->done && p->mpeg == 2){
				p->flag2 = buf[c];
				c++;
				p->found++;
			}	
			break;

		case 8:
			if ( !p->done && p->mpeg == 2){
				p->hlength = buf[c];
				c++;
				p->found++;
			}
			break;
			
		default:

			break;
		}
	}


	if (c == count) return;

	if (!p->plength) p->plength = MMAX_PLENGTH-6;


	if ( p->done || ((p->mpeg == 2 && p->found >= 9)  || 
	     (p->mpeg == 1 && p->found >= 7)) ){
		switch (p->cid){
			
		case AUDIO_STREAM_S ... AUDIO_STREAM_E:			
		case VIDEO_STREAM_S ... VIDEO_STREAM_E:
		case PRIVATE_STREAM1:
			
			if (p->mpeg == 2 && p->found == 9){
				write_ipack(p, &p->flag1, 1);
				write_ipack(p, &p->flag2, 1);
				write_ipack(p, &p->hlength, 1);
			}

			if (p->mpeg == 1 && p->found == 7){
				write_ipack(p, &p->flag1, 1);
			}


			if (p->mpeg == 2 && (p->flag2 & PTS_ONLY) &&  
			    p->found < 14){
				while (c < count && p->found < 14){
					p->pts[p->found-9] = buf[c];
					write_ipack(p, buf+c, 1);
					c++;
					p->found++;
				}
				if (c == count) return;
			}
			
			if (p->mpeg == 1 && p->which < 2000){

				if (p->found == 7) {
					p->check = p->flag1;
					p->hlength = 1;
				}

				while (!p->which && c < count && 
				       p->check == 0xFF){
					p->check = buf[c];
					write_ipack(p, buf+c, 1);
					c++;
					p->found++;
					p->hlength++;
				}

				if ( c == count) return;
				
				if ( (p->check & 0xC0) == 0x40 && !p->which){
					p->check = buf[c];
					write_ipack(p, buf+c, 1);
					c++;
					p->found++;
					p->hlength++;

					p->which = 1;
					if ( c == count) return;
					p->check = buf[c];
					write_ipack(p, buf+c, 1);
					c++;
					p->found++;
					p->hlength++;
					p->which = 2;
					if ( c == count) return;
				}

				if (p->which == 1){
					p->check = buf[c];
					write_ipack(p, buf+c, 1);
					c++;
					p->found++;
					p->hlength++;
					p->which = 2;
					if ( c == count) return;
				}
				
				if ( (p->check & 0x30) && p->check != 0xFF){
					p->flag2 = (p->check & 0xF0) << 2;
					p->pts[0] = p->check;
					p->which = 3;
				} 

				if ( c == count) return;
				if (p->which > 2){
					if ((p->flag2 & PTS_DTS_FLAGS)
					    == PTS_ONLY){
						while (c < count && 
						       p->which < 7){
							p->pts[p->which-2] =
								buf[c];
							write_ipack(p,buf+c,1);
							c++;
							p->found++;
							p->which++;
							p->hlength++;
						}
						if ( c == count) return;
					} else if ((p->flag2 & PTS_DTS_FLAGS) 
						   == PTS_DTS){
						while (c < count && 
						       p->which< 12){
							if (p->which< 7)
								p->pts[p->which
								      -2] =
									buf[c];
							write_ipack(p,buf+c,1);
							c++;
							p->found++;
							p->which++;
							p->hlength++;
						}
						if ( c == count) return;
					}
					p->which = 2000;
				}
							
			}

			while (c < count && p->found < p->plength+6){
				l = count -c;
				if (l+p->found > p->plength+6)
					l = p->plength+6-p->found;
				write_ipack(p, buf+c, l);
				p->found += l;
				c += l;
			}	
		
			break;
		}


		if ( p->done ){
			if( p->found + count - c < p->plength+6){
				p->found += count-c;
				c = count;
			} else {
				c += p->plength+6 - p->found;
				p->found = p->plength+6;
			}
		}

		if (p->plength && p->found == p->plength+6) {
			send_ipack(p);
			reset_ipack(p);
			if (c < count)
				instant_repack(buf+c, count-c, p);
		}
	}
	return;
}
