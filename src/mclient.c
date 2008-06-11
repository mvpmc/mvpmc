/*
 *  Copyright (C) 2005-2008, Paul Warren <pdw@ex-parrot.com>
 *  http://www.mvpmc.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundtion; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

#include <mvp_widget.h>
#include <mvp_av.h>
#include <mvp_demux.h>
#include <mvp_osd.h>

#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "mvpmc.h"

#include <sys/un.h>

#include <unistd.h>
#include <sys/utsname.h>

#include <fcntl.h>
#include <errno.h>

#include "mclient.h"

/******************************
 * Adapted from slimp3slave
 ******************************/

#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <locale.h>
#include <ctype.h>
#include <stdarg.h>
#include <time.h>

#include "display.h"

/*
 * Added to obtain MAC address.
 */
#include <net/if.h>
#include <sys/ioctl.h>

/* lookup table to convert the VFD charset to Latin-1 */
/* (Sorry, other character sets not supported) */
char vfd2latin1[] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,     /* 00 - 07 */
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,     /* 08 - 0f */
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,     /* 10 - 17 */
    0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,     /* 18 - 1f */
    0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,     /* 20 - 27 */
    0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,     /* 28 - 2f */
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,     /* 30 - 37 */
    0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,     /* 38 - 3f */
    0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,     /* 40 - 47 */
    0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f,     /* 48 - 4f */
    0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57,     /* 50 - 57 */
    0x58, 0x59, 0x5a, 0x5b, 0xa5, 0x5d, 0x5e, 0x5f,     /* 58 - 5f */
    0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,     /* 60 - 67 */
    0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f,     /* 68 - 6f */
    0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77,     /* 70 - 77 */
    0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0xbb, 0xab,     /* 78 - 7f */
    0xc4, 0xc3, 0xc5, 0xe1, 0xe5, 0x85, 0xd6, 0xf6,     /* 80 - 87 */
    0xd8, 0xf8, 0xdc, 0x8b, 0x5c, 0x8d, 0x7e, 0xa7,     /* 88 - 8f */
    0xc6, 0xe6, 0xa3, 0x93, 0xb7, 0x6f, 0x96, 0x97,     /* 90 - 97 */
    0xa6, 0xc7, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f,     /* 98 - 9f */
    0xa0, 0xa1, 0xa2, 0xac, 0xa4, 0xb7, 0xa6, 0xa7,     /* a0 - a7 */
    0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf,     /* a8 - af */
    0x2d, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb1,     /* b0 - b7 */
    0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf,     /* b8 - bf */
    0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7,     /* c0 - c7 */
    0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf,     /* c8 - cf */
    0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7,     /* d0 - d7 */
    0xd8, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xa8, 0xb0,     /* d8 - df */
    0xe0, 0xe4, 0xdf, 0xe3, 0xb5, 0xe5, 0x70, 0x67,     /* e0 - e7 */
    0xe8, 0xe9, 0x6a, 0xa4, 0xa2, 0xed, 0xf1, 0xf6,     /* e8 - ef */
    0x70, 0x71, 0xf2, 0xf3, 0xf4, 0xfc, 0xf6, 0xf7,     /* f0 - f7 */
    0xf8, 0x79, 0xfa, 0xfb, 0xfc, 0xf7, 0xfe, 0xff      /* f8 - ff */
};

/*
 * Global pointer to structure containning alloc'd memory for
 * local buffer and other buffer related info.
 */
ring_buf *outbuf;
/*
 * Global pointer to alloc'd memory for data received from 
 * the client.
 */
void *recvbuf;

static int debug = 0;

/*
 * Default is to enable the display.
 */
int display = 1;

struct in_addr *server_addr_mclient = NULL;

char slimp3_display[DISPLAY_SIZE + 1];

struct timeval uptime;          /* time we started */

/*
 * Track remote control button activity.
 */
remote_buttons_type remote_buttons;

ring_buf *
ring_buf_create(int size)
{
    ring_buf *b;
    b = malloc(sizeof(ring_buf));
    b->buf = (void *)calloc(size, 1);
    b->size = size;
    b->head = b->tail = 0;
    return b;
}

/*
 * Need the MAC to uniquely identify this mvpmc box to
 * mclient server.
 */
struct ifreq ifr;

void
send_packet(int s, char *b, int l)
{
    struct sockaddr_in ina;

    ina.sin_family = AF_INET;
    ina.sin_port = htons(SERVER_PORT);
    ina.sin_addr = *server_addr_mclient;

    /* Protect from multiple sends */
    pthread_mutex_lock(&mclient_mutex);

    if (sendto(s, b, l, 0, (const struct sockaddr *)&ina, sizeof(ina)) == -1)
    {
        if (debug)
            printf("mclient:Could not send packet\n");
    };
    pthread_mutex_unlock(&mclient_mutex);
}

void
send_discovery(int s)
{
    char pkt[18];

    memset(pkt, 0, sizeof(pkt));
    pkt[0] = 'd';
    pkt[2] = 1;
    pkt[3] = 0x11;

    memcpy(&pkt[12], mac_address_ptr, 6);

    if (debug)
        printf("mclient: Sending discovery request.\n");

    send_packet(s, pkt, sizeof(pkt));
}

void
send_ack(int s, unsigned short seq)
{
    packet_ack pkt;

    memset(&pkt, 0, sizeof(pkt));

    pkt.type = 'a';

    pkt.wptr = htons(outbuf->head >> 1);
    pkt.rptr = htons(outbuf->tail >> 1);

    pkt.seq = htons(seq);

    memcpy(pkt.mac_addr, mac_address_ptr, 6);

    if (debug)
    {
        printf("\nmclient:pkt.wptr:%8.8d pkt.rptr:%8.8d handle:%d\n", pkt.wptr,
               pkt.rptr, s);
        printf("=> sending ack for %d\n", seq);
    }

    send_packet(s, (void *)&pkt, sizeof(request_data_struct));
}

void
say_hello(int s)
{
    char pkt[18];

    memset(pkt, 0, sizeof(pkt));

    // For SLIMP3 the hello message format is:
    pkt[0] = 'h';               // Hello
    pkt[1] = 1;                 // Device ID (1=SLIMP3)
    pkt[2] = 0x11;              // Firmware revision (0x11 for version 1.1)
    // Bits 3..11 are reserved
    // For SLIMP3 bits 12..17 are for MAC address, bits 3..11 are reserved
    memcpy(&pkt[12], mac_address_ptr, 6);

#if 0
    // For SqueezeBox the format is different:
    // For bits 0..3 are "HELO"
    pkt[0] = 'H';               // Hello
    pkt[1] = 'E';               // Hello
    pkt[2] = 'L';               // Hello
    pkt[3] = 'O';               // Hello
    // For bit 4 Device ID (2=SqueezeBox)
    pkt[4] = 2;                 // Device ID (1=SLIMP3)
    // For bit 5 Firmware revision (0x11 for version 1.1)
    pkt[5] = 0x11;              // Firmware revision (0x11 for version 1.1)
    // For bits 6..11 player's MAC address
    memcpy(&pkt[6], mac_address_ptr, 6);
    // for bits 12..13 list of 802.11 channels enabled (0x7ff means chan 0
    // through 11 are enabled).
    pkt[12] = 0x07;
    pkt[13] = 0xff;
#endif

    send_packet(s, pkt, sizeof(pkt));
}

void
send_ir(int s, char codeset, unsigned long code, int bits)
{
    send_ir_struct pkt;
    struct timeval now;
    struct timezone tz;
    struct timeval diff;
    unsigned long usecs;
    unsigned long ticks;

    gettimeofday(&now, &tz);
    now.tv_sec -= 60 * tz.tz_minuteswest;       /* canonicalize to GMT/UTC */
    if (now.tv_usec < uptime.tv_usec)
    {
        /* borrowing */
        now.tv_usec += 1000;
        now.tv_sec -= 1;
    }
    diff.tv_usec = now.tv_usec - uptime.tv_usec;
    diff.tv_sec = now.tv_sec - uptime.tv_sec;
    usecs = diff.tv_sec * 1000000L + diff.tv_usec;
    ticks = (unsigned int)(0.625 * (double)usecs);

    memset(&pkt, 0, sizeof(send_ir_struct));
    pkt.type = 'i';
    pkt.zero = 0;
    pkt.time = htonl(ticks);
    pkt.codeset = codeset;
    pkt.bits = (char)bits;
    pkt.code = htonl(code);
    memcpy(pkt.mac_addr, mac_address_ptr, 6);

    if (debug)
        printf("=> sending IR code %lu at tick %lu (%lu usec)\n", code, ticks,
               usecs);

    send_packet(s, (void *)&pkt, sizeof(pkt));
}

unsigned long
curses2ir(int key)
{
    unsigned long ir = 0;
    /*
     * Reset small widget hiding timeout for another
     * short interval (several seconds).
     */
    cli_small_widget_timeout = time(NULL) + 10;
    switch (key)
    {
    case MVPW_KEY_ZERO:
        // Only if we are in local menu mode and we are browsing albums.
        if (remote_buttons.local_menu)
        {
            cli_data.album_start_index_for_cover_art = 0;
            // Only need to adjust the widget (user focus) unless we fall off edge.
            cli_data.trigger_proc_for_cover_art = TRUE;
        }
        else
        {
            ir = 0x76899867;
        }
        break;
    case MVPW_KEY_ONE:
        // Only if we are in local menu mode and we are browsing albums.
        if (remote_buttons.local_menu)
        {
            cli_data.album_start_index_for_cover_art =
                (cli_data.album_max_index_for_cover_art * .10);
            // Only need to adjust the widget (user focus) unless we fall off edge.
            cli_data.trigger_proc_for_cover_art = TRUE;
        }
        else
        {
            ir = 0x7689f00f;
        }
        break;
    case MVPW_KEY_TWO:
        // Only if we are in local menu mode and we are browsing albums.
        if (remote_buttons.local_menu)
        {
            cli_data.album_start_index_for_cover_art =
                (cli_data.album_max_index_for_cover_art * .20);
            // Only need to adjust the widget (user focus) unless we fall off edge.
            cli_data.trigger_proc_for_cover_art = TRUE;
        }
        else
        {
            ir = 0x768908f7;
        }
        break;
    case MVPW_KEY_THREE:
        // Only if we are in local menu mode and we are browsing albums.
        if (remote_buttons.local_menu)
        {
            cli_data.album_start_index_for_cover_art =
                (cli_data.album_max_index_for_cover_art * .30);
            // Only need to adjust the widget (user focus) unless we fall off edge.
            cli_data.trigger_proc_for_cover_art = TRUE;
        }
        else
        {
            ir = 0x76898877;
        }
        break;
    case MVPW_KEY_FOUR:
        // Only if we are in local menu mode and we are browsing albums.
        if (remote_buttons.local_menu)
        {
            cli_data.album_start_index_for_cover_art =
                (cli_data.album_max_index_for_cover_art * .40);
            // Only need to adjust the widget (user focus) unless we fall off edge.
            cli_data.trigger_proc_for_cover_art = TRUE;
        }
        else
        {
            ir = 0x768948b7;
        }
        break;
    case MVPW_KEY_FIVE:
        // Only if we are in local menu mode and we are browsing albums.
        if (remote_buttons.local_menu)
        {
            cli_data.album_start_index_for_cover_art =
                (cli_data.album_max_index_for_cover_art * .50);
            // Only need to adjust the widget (user focus) unless we fall off edge.
            cli_data.trigger_proc_for_cover_art = TRUE;
        }
        else
        {
            ir = 0x7689c837;
        }
        break;
    case MVPW_KEY_SIX:
        // Only if we are in local menu mode and we are browsing albums.
        if (remote_buttons.local_menu)
        {
            cli_data.album_start_index_for_cover_art =
                (cli_data.album_max_index_for_cover_art * .60);
            // Only need to adjust the widget (user focus) unless we fall off edge.
            cli_data.trigger_proc_for_cover_art = TRUE;
        }
        else
        {
            ir = 0x768928d7;
        }
        break;
    case MVPW_KEY_SEVEN:
        // Only if we are in local menu mode and we are browsing albums.
        if (remote_buttons.local_menu)
        {
            cli_data.album_start_index_for_cover_art =
                (cli_data.album_max_index_for_cover_art * .70);
            // Only need to adjust the widget (user focus) unless we fall off edge.
            cli_data.trigger_proc_for_cover_art = TRUE;
        }
        else
        {
            ir = 0x7689a857;
        }
        break;
    case MVPW_KEY_EIGHT:
        // Only if we are in local menu mode and we are browsing albums.
        if (remote_buttons.local_menu)
        {
            cli_data.album_start_index_for_cover_art =
                (cli_data.album_max_index_for_cover_art * .80);
            // Only need to adjust the widget (user focus) unless we fall off edge.
            cli_data.trigger_proc_for_cover_art = TRUE;
        }
        else
        {
            ir = 0x76896897;
        }
        break;
    case MVPW_KEY_NINE:
        // Only if we are in local menu mode and we are browsing albums.
        if (remote_buttons.local_menu)
        {
            cli_data.album_start_index_for_cover_art =
                (cli_data.album_max_index_for_cover_art * .90);
            // Only need to adjust the widget (user focus) unless we fall off edge.
            cli_data.trigger_proc_for_cover_art = TRUE;
        }
        else
        {
            ir = 0x7689e817;
        }
        break;
    case MVPW_KEY_DOWN:
        if (remote_buttons.local_menu)
        {
            cli_data.row_for_cover_art += 1;
            // if (cli_data.row_for_cover_art >= 3)
            /// Swap above and below lines to skip/not-skip bar.
            if (cli_data.row_for_cover_art >= 2)
            {
                cli_data.row_for_cover_art = 0;
            }

            if (cli_data.state_for_cover_art_widget == IDLE_COVERART_WIDGET)
            {
                cli_data.state_for_cover_art_widget = ADJ_COVERART_WIDGET;
                cli_data.pending_proc_for_cover_art_widget = TRUE;
            }
        }
        else
        {
            ir = 0x7689b04f;
        }
        break;                  /* arrow_down */
    case MVPW_KEY_LEFT:
        if (remote_buttons.local_menu)
        {
            if (cli_data.row_for_cover_art == 2)
            {
                // We are focused on the browsing bar.
                // We are skipping backwards a screen worth of album covers.
                if (cli_data.album_start_index_for_cover_art >= 6)
                {
                    cli_data.album_start_index_for_cover_art -= 6;
                }
                else
                {
                    cli_data.
                        album_start_index_for_cover_art =
                        cli_data.
                        album_max_index_for_cover_art - (6 -
                                                         cli_data.
                                                         album_start_index_for_cover_art);
                }
                cli_data.trigger_proc_for_cover_art = TRUE;
            }
            else
            {
                // We are focused on an album cover.
                // Only need to adjust the widget (user focus) unless we fall off edge.
                cli_data.col_for_cover_art -= 1;
                if (cli_data.col_for_cover_art >= 3)
                {
                    // We fell off the edge, refresh images.
                    cli_data.trigger_proc_for_cover_art = TRUE;

                    // cli_data.col_for_cover_art = 2;
                    /// Swap above and below lines to switch stay/not-stay.
                    cli_data.col_for_cover_art = 0;

                    // Advance to the next set of 6 album covers.
                    // We are skipping backwards a screen worth of album covers.
                    if (cli_data.album_start_index_for_cover_art >= 6)
                    {
                        cli_data.album_start_index_for_cover_art -= 6;
                    }
                    else
                    {
                        cli_data.
                            album_start_index_for_cover_art =
                            cli_data.
                            album_max_index_for_cover_art - (6 -
                                                             cli_data.
                                                             album_start_index_for_cover_art);
                    }
                }
                else
                {
                    // Only need to adjust the widget (user focus).
                    cli_data.state_for_cover_art_widget = ADJ_COVERART_WIDGET;
                    cli_data.pending_proc_for_cover_art_widget = TRUE;
                }
            }
        }
        else
        {
            ir = 0x7689906f;
        }
        break;                  /* arrow_left */
    case MVPW_KEY_RIGHT:
        if (remote_buttons.local_menu)
        {
            if (cli_data.row_for_cover_art == 2)
            {
                // We are focused on the browsing bar.
                // We are skipping forward a screen worth of album covers.
                cli_data.album_start_index_for_cover_art += 6;
                if (cli_data.album_start_index_for_cover_art >=
                    cli_data.album_max_index_for_cover_art)
                {
                    cli_data.album_start_index_for_cover_art %=
                        cli_data.album_max_index_for_cover_art;
                }
                cli_data.trigger_proc_for_cover_art = TRUE;
            }
            else
            {
                // We are focused on an album cover.
                // Only need to adjust the widget (user focus) unless we fall off edge.
                cli_data.col_for_cover_art += 1;
                if (cli_data.col_for_cover_art >= 3)
                {
                    // We fell off the edge, refresh images.
                    cli_data.trigger_proc_for_cover_art = TRUE;

                    // cli_data.col_for_cover_art = 0;
                    /// Swap above and below lines to switch stay/not-stay.
                    cli_data.col_for_cover_art = 2;

                    // Advance to the next set of 6 album covers.
                    // We are skipping forward a screen worth of album covers.
                    cli_data.album_start_index_for_cover_art += 6;
                    if (cli_data.album_start_index_for_cover_art >=
                        cli_data.album_max_index_for_cover_art)
                    {
                        cli_data.album_start_index_for_cover_art %=
                            cli_data.album_max_index_for_cover_art;
                    }
                }
                else
                {
                    // Only need to adjust the widget (user focus).
                    cli_data.state_for_cover_art_widget = ADJ_COVERART_WIDGET;
                    cli_data.pending_proc_for_cover_art_widget = TRUE;
                }
            }
        }
        else
        {
            ir = 0x7689d02f;
        }
        break;                  /* arrow_right */
    case MVPW_KEY_UP:
        // Only if we are in local menu mode and we are browsing albums.
        if (remote_buttons.local_menu)
        {
            if (cli_data.row_for_cover_art <= 0)
            {
                cli_data.row_for_cover_art = 1;
            }
            else
            {
                cli_data.row_for_cover_art -= 1;
            }

            if (cli_data.state_for_cover_art_widget == IDLE_COVERART_WIDGET)
            {
                cli_data.state_for_cover_art_widget = ADJ_COVERART_WIDGET;
                cli_data.pending_proc_for_cover_art_widget = TRUE;
            }
        }
        else
        {
            ir = 0x7689e01f;
        }
        break;                  /* arrow_up */
    case MVPW_KEY_VOL_DOWN:
        ir = 0x768900ff;
        break;                  /* voldown */
    case MVPW_KEY_VOL_UP:
        ir = 0x7689807f;
        break;                  /* volup */
///    case MVPW_KEY_REWIND:
///        ir = 0x7689c03f;
///        break;                  /* rew */
    case MVPW_KEY_PAUSE:
        ir = 0x768920df;
        break;                  /* pause */
    case MVPW_KEY_SKIP:
        remote_buttons.last_pressed = MVPW_KEY_SKIP;
        if (remote_buttons.shift == TRUE)
        {
            ir = 0x7689d827;    /// This cmd is for cycling through different shuffle modes, not skip.
        }
        else
        {
            ir = 0x7689a05f;    /// This cmd is for skipping forward.
        }
        break;                  /* fwd */
///    case MVPW_KEY_FFWD:
///        ir = 0x7689a05f;
///        break;                  /* fwd */
///    case MVPW_KEY_OK:
///        ir = 0x768910ef;
///        break;                  /* play */
///    case MVPW_KEY_PLAY:
///        ir = 0x768910ef;
///        break;                  /* play */
    case MVPW_KEY_MENU:
        remote_buttons.last_pressed = MVPW_KEY_SKIP;
        if (remote_buttons.shift == TRUE)
        {
            ir = 0x76897887;    /// This cmd for jumping to slimserver's "Now Playing" menu.
        }
        else
        {
            /*
             * Enter local menu.
             */
            remote_buttons.local_menu = TRUE;
            mvpw_show(mclient_sub_localmenu);
            mvpw_raise(mclient_sub_localmenu);
            mvpw_focus(mclient_sub_localmenu);
            mvpw_set_key(mclient_sub_localmenu, mclient_localmenu_callback);
        }
        break;                  /* jump to now playing menu */
    case MVPW_KEY_REPLAY:
        remote_buttons.last_pressed = MVPW_KEY_SKIP;
        if (remote_buttons.shift == TRUE)
        {
            ir = 0x768938c7;    /// This cmd is for different repeat modes, not replay.
        }
        else
        {
            ir = 0x7689c03f;    /// This cmd is for REPLAY.
        }
        break;                  /* rew */
    case MVPW_KEY_GO:
        ir = 0x7689609f;
        break;                  /* add, NOTE: if held = zap */
    case MVPW_KEY_FULL:
        ir = 0x768958a7;
        break;                  /* jump to search menu */
    case MVPW_KEY_BLANK:
        ir = 0x7689b847;
        break;                  /* sleep */

    case MVPW_KEY_RED:
        ir = 0x768940bf;
        break;                  /* power */
    case MVPW_KEY_GREEN:
        /*
         * The Green button is reserved by mvpmc to capture the screen.
         * The command line option to specify a jpg screen dump file:
         * "-C <file_name>"
         */
        break;
    case MVPW_KEY_BLUE:
        ir = 0x7689807f;
        break;                  /* volup */
    case MVPW_KEY_YELLOW:
        ir = 0x768900ff;
        break;                  /* voldown */

        /*
         * JVC remote control codes.
         */
    case MVPW_KEY_STOP:
        ir = 0x0000f7c2;
        break;                  /* stop */

        /*
         * Special keys we can process here.
         */
    case MVPW_KEY_MUTE:
        if (av_mute() == 1)
            mvpw_show(mute_widget);
        else
            mvpw_hide(mute_widget);
        break;

    case MVPW_KEY_FFWD:
        if (remote_buttons.local_menu)
        {
            // We are skipping forward a screen worth of album covers.
            cli_data.album_start_index_for_cover_art += 6;
            if (cli_data.album_start_index_for_cover_art >=
                cli_data.album_max_index_for_cover_art)
            {
                cli_data.album_start_index_for_cover_art %=
                    cli_data.album_max_index_for_cover_art;
            }
            cli_data.trigger_proc_for_cover_art = TRUE;
        }
        else
        {
            /*
             * Special case for FF key: 
             * 
             * If not already in FF mode: If FF is pressed, pass 2x command to CLI.  If
             * FF is held, pass 3x command to CLI after 1 to 2 seconds and 4x command to
             * CLI after another 1 second.
             * If in FF mode: If PLAY is pressed, pass 1x command to CLI.
             */
            remote_buttons.last_pressed = MVPW_KEY_FFWD;
            remote_buttons.number_of_scans++;
        }
        break;

    case MVPW_KEY_REWIND:
        if (remote_buttons.local_menu)
        {
            // We are skipping backwards a screen worth of album covers.
            if (cli_data.album_start_index_for_cover_art >= 6)
            {
                cli_data.album_start_index_for_cover_art -= 6;
            }
            else
            {
                cli_data.album_start_index_for_cover_art =
                    cli_data.album_max_index_for_cover_art - (6 -
                                                              cli_data.
                                                              album_start_index_for_cover_art);
            }
            cli_data.trigger_proc_for_cover_art = TRUE;
        }
        else
        {
            /*
             * Special case for REWIND key: 
             * 
             * If not already in REWIND mode: If REWIND is pressed, pass -2x command to CLI.  If
             * REWIND is held, pass -3x command to CLI after 1 to 2 seconds and -4x command to
             * CLI after another 1 second.
             * If in REWIND mode: If PLAY is pressed, pass 1x command to CLI.
             */
            remote_buttons.last_pressed = MVPW_KEY_REWIND;
            remote_buttons.number_of_scans++;
        }
        break;

        /*
         * Special case for OK key: 
         * 
         * If in FF or FR mode revert back to 1x speed.  Otherwise,
         * treat like ordinary PLAY cmd.
         */
    case MVPW_KEY_OK:
        if (remote_buttons.local_menu == TRUE)
        {
            // Press OK button while in LOCAL MENU mode.
            // ...this is handled else where...
        }
        else
        {
            if ((remote_buttons.last_pressed == MVPW_KEY_FFWD) ||
                (remote_buttons.last_pressed == MVPW_KEY_REWIND))
            {
                remote_buttons.last_pressed = MVPW_KEY_OK;
                /*
                 * Send CLI to set FF or FR to 1x.
                 */
                sprintf(pending_cli_string, "%s rate 1\n", decoded_player_id);
            }
            else
            {
                /*
                 * Send real PLAY command to slimserver.
                 */
                ir = 0x768910ef;
            }
        }
        break;                  /* play */

        /*
         * Special case for PLAY key: 
         * 
         * If in FF or FR mode revert back to 1x speed.  Otherwise,
         * treat like ordinary PLAY cmd.
         */
    case MVPW_KEY_PLAY:
        if ((remote_buttons.last_pressed == MVPW_KEY_FFWD) ||
            (remote_buttons.last_pressed == MVPW_KEY_REWIND))
        {
            remote_buttons.last_pressed = MVPW_KEY_OK;

            /*
             * Send CLI to set FF or FR to 1x.
             */
            sprintf(pending_cli_string, "%s rate 1\n", decoded_player_id);
        }
        else
        {
            /*
             * Send real PLAY command to slimserver.
             */
            ir = 0x768910ef;
        }
        break;                  /* play */

        /*
         * Old way to toggle debug printfs. 
         *
         * Now using record button as shift key
         * for more options:
         *
         * Uncomment if want to toggle mclient debug printfs (lots of them!).
         */
        ///    case MVPW_KEY_RECORD: debug = !debug; break; /* toggle debug mode */

        /*
         * Now using record button as shift key for more options:
         */
    case MVPW_KEY_RECORD:
        remote_buttons.last_pressed = MVPW_KEY_RECORD;
        break;

        /*
         * Keys that may not make sense for mvpmc.
         */
        ///    case '+': ir = 0x7689f807; break; /* size */
        ///    case '*': ir = 0x768904fb; break; /* brightness */
    }
    if (ir != 0)
        send_ir(mclient_socket, 0xff, ir, 16);

    return ir;
}

void
receive_display_data(char *ddram, unsigned short *data, int bytes_read)
{
    unsigned short *display_data;
    int n;
    static int addr = 0;        /* counter */

    pthread_mutex_lock(&mclient_mutex);
    if (bytes_read % 2)
        bytes_read--;           /* even number of bytes */
    display_data = &(data[9]);  /* display data starts at byte 18 */

    for (n = 0; n < (bytes_read / 2); n++)
    {
        unsigned short d;       /* data element */
        unsigned char t, c;

        d = ntohs(display_data[n]);
        t = (d & 0x00ff00) >> 8;        /* type of display data */
        c = (d & 0x0000ff);     /* character/command */

        switch (t)
        {
        case 0x03:             /* character */
            /*
             * Traslate server's chars into printable
             * ASCII.
             */
            c = vfd2latin1[c];
            /*
             * Filter out non printable chars.
             */
            if ((c == 0xaf)     /* "~" */
                || (c == 0xbb)  /* ">>" */
                /*
                 * We can probably open up the filter
                 * to pass either the IBM or Windoze
                 * extened ASCII char set here.
                 */
                )
            {
                /*
                 * Allow these characters to pass through.
                 */
            }
            else
            {
                /*
                 * If non printable, replace with a space.
                 */
                if (!isprint(c))
                {
                    c = ' ';
                }
            }
            if (addr < DISPLAY_SIZE)
            {
                ddram[addr++] = c;
            }
            break;
        case 0x02:             /* command */
            switch (c)
            {
            case 0x01:         /* display clear */
                memset(ddram, ' ', DISPLAY_SIZE);
                addr = 0;
                break;
            case 0x02:         /* cursor home */
            case 0x03:         /* cursor home */
                addr = 0;
                break;
            case 0x10:         /* cursor left */
            case 0x11:         /* cursor left */
            case 0x12:         /* cursor left */
            case 0x13:         /* cursor left */
                addr--;
                if (addr < 0)
                    addr = 0;
                break;
            case 0x0e:         /* cursor right *//* NEW */
            case 0x14:         /* cursor right */
            case 0x15:         /* cursor right */
            case 0x16:         /* cursor right */
            case 0x17:         /* cursor right */
                addr++;
                if (addr >= DISPLAY_SIZE)
                    addr = DISPLAY_SIZE - 1;
                break;
            default:
                if ((c & 0x80) == 0x80)
                {
                    addr = (c & 0x7f);
                }
                else
                {
                    if (debug)
                        printf
                            ("mclinet:UNDEFINED CONTROL CHARACTER!!! c:%-2.2x n:%-2.2d\n",
                             c, n);
                }
                break;
            }
        case 0x00:             /* delay */
        default:
            break;
        }
    }
    pthread_mutex_unlock(&mclient_mutex);
}

void
read_packet(int s)
{
    struct sockaddr ina;
    struct sockaddr_in *ina_in = NULL;
    socklen_t slen = sizeof(struct sockaddr);
    int bytes_read;

    bytes_read = recvfrom(s, recvbuf, RECV_BUF_SIZE, 0, &ina, &slen);
    if (ina.sa_family == AF_INET)
    {
        ina_in = (struct sockaddr_in *)(&ina);
    }
    if (bytes_read < 1)
    {
        if (bytes_read < 0)
        {
            if (errno != EINTR)
                if (debug)
                    printf("mclient:recvfrom\n");
        }
        else
        {
            printf("Peer closed connection\n");
        }
    }
    else if (bytes_read < 18)
    {
        printf("<= short packet\n");
    }
    else
    {
        switch (((char *)recvbuf)[0])
        {
        case 'D':
            if (debug)
                printf("<= discovery response\n");
            say_hello(s);
            break;
        case 'h':
            if (debug)
                printf("<= hello\n");
            say_hello(s);
            break;
        case 'l':
            if (debug)
                printf("<= LCD data\n");
            if (display)
            {
                receive_display_data(slimp3_display, recvbuf, bytes_read);
            }
            break;
        case 's':
            if (debug)
                printf("<= stream control\n");
            break;
        case 'm':
            if (debug)
                printf("<= mpeg data\n");
            receive_mpeg_data(s, recvbuf, bytes_read);
            break;
        case '2':
            if (debug)
                printf("<= i2c data\n");
            receive_volume_data(recvbuf, bytes_read);
            break;
        }
    }

}

int
mclient_server_connect(void)
{
    int s;
    struct sockaddr_in my_addr;
    char eth[16];

    snprintf(eth, sizeof(eth), "eth%d", wireless);

    s = socket(AF_INET, SOCK_DGRAM, 0);

    /*
     * Get the MAC address for the first ethernet port.
     */
    strcpy(ifr.ifr_name, eth);
    ioctl(s, SIOCGIFHWADDR, &ifr);
    mac_address_ptr = (unsigned char *)ifr.ifr_hwaddr.sa_data;

    if (s == -1)
    {
        printf("mclient:Could not get descriptor\n");
        return s;
    }
    else
    {
        if (debug)
            printf("mclient:Was able get descriptor:%d\n", s);
    }

    my_addr.sin_family = AF_INET;
    my_addr.sin_port = htons(CLIENT_PORT);
    my_addr.sin_addr.s_addr = INADDR_ANY;
    memset(&(my_addr.sin_zero), '\0', 8);
    if (bind(s, (struct sockaddr *)&my_addr, sizeof(struct sockaddr)))
    {
        printf("mclient:Unable to connect to descriptor endpoint:%d\n", s);
    }
    else
    {
        if (debug)
            printf("mclient:Was able to connect to descriptor endpoint:%d\n",
                   s);
    }

    return s;
}
