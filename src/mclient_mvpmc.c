/*
 *  $Id$
 *
 *  Copyright (C) 2006, Rick Stuart
 *  http://mvpmc.sourceforge.net/
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
 * Music Client
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

int mclient_type;

int mclient_socket;

/*
 * Pthread for main mclient loop.
 */
static pthread_t mclient_loop_thread_handle;
pthread_cond_t mclient_cond = PTHREAD_COND_INITIALIZER;

/* Mutex for sending on socket */
pthread_mutex_t mclient_mutex = PTHREAD_MUTEX_INITIALIZER;

/* 
 * Local flag to keep track of paused state.
 */
int local_paused;

/*
 * Tracks timeout of small mclient widget.
 */
int cli_small_widget_timeout;
int cli_small_widget_state = SHOW;

void mclient_audio_play (mvp_widget_t * widget);

/*
 * Need the MAC to uniquely identify this mvpmc box to
 * mclient server.
 */
unsigned char *mac_address_ptr;

/*
 * Lookup table to adjust the volume contol - make it sound
 * more like a normal volume control.
 *
 * Mvpmc vol range appears to be between 220 to 255.
 */
unsigned char volume_adj[] = {
    225, 225, 226, 226, 227, 227, 228, 228, 229, 229,
    230, 230, 231, 231, 232, 232, 233, 233, 234, 234,
    235, 236, 237, 238, 239, 240, 241, 242, 243, 244,
    245, 246, 247, 248, 249, 250, 251, 252, 253, 254,
    255
};

/*
 * Volume data received from the server.
 */
unsigned int volume_server[] = {
    0x0, 0x147, 0x51e, 0xb85, 0x147a,
    0x2000, 0x2e14, 0x3eb8, 0x51eb, 0x67ae,
    0x8000, 0x9ae1, 0xb851, 0xd851, 0xfae1,
    0x12000, 0x147ae, 0x171eb, 0x19eb8, 0x1ce14,
    0x20000, 0x2347a, 0x26b85, 0x2a51e, 0x2e147,
    0x32000, 0x36147, 0x3a51e, 0x3eb85, 0x4347a,
    0x48000, 0x4ce14, 0x51eb8, 0x571eb, 0x5c7ae,
    0x62000, 0x67ae1, 0x6d851, 0x73851, 0x79ae1,
    0x80000
};

static int debug = 0;


void
receive_mpeg_data (int s, receive_mpeg_header * data, int bytes_read)
{
    static int seq_history = 0;
    int message_length = 0;
    static int rec_seq = 0;     // for use in ack test

    rec_seq = ntohs (data->seq);
    if (seq_history > rec_seq)
    {
        // reset on server or client reboots or when SlimServer ushort resets at 64k
        seq_history = -1;
    }

    if (debug)
    {
        printf ("mclient:     Address:%8.8d Cntr:%8.8d     Seq:%8.8d  BytesIn:%8.8d\n",
                ntohs (data->wptr), data->control, ntohs (data->seq), bytes_read);

        if (seq_history == ntohs (data->seq))
        {
            printf ("mclient:Sequence says - NO  data\n");
        }
        else
        {
            printf ("mclient:Sequence says - NEW data\n");
        }

        if (outbuf->head == (ntohs (data->wptr) << 1))
        {
            printf ("mclient:Server and Client pntrs - AGREE\n");
        }
        else
        {
            printf ("mclient:Server and Client pntrs - DIFFERENT (by:%d)\n",
                    outbuf->head - ((ntohs (data->wptr)) << 1));
        }
    }

    /*
     * Set head pointer to value received from server.
     * (Server needs this control when switching between
     * programs (i.e. when switching between tracks).)
     */
    outbuf->head = ntohs ((data->wptr) << 1); // ### 20051209  Let's try to let the server do this again.

    /*
     * Store play mode into global variable.
     */
    outbuf->playmode = data->control;

    /*
     * Must be some header bytes we need to get rid of.
     */
    message_length = bytes_read - 18;

    /*
     * If this is a new sequence (new data) then
     * add it to the input buffer.
     *
     * Don't procees when 0 (sendEmptyChunk from SlimServer) or if it has 
     * already been buffered.
     */
    if (message_length != 0 && seq_history < ntohs (data->seq))
    {
        /*
         * Keep history of seq so next time
         * we are here we can tell if
         * we are getting new data that should be 
         * processed.
         */
        seq_history = ntohs (data->seq);

        /*
         * Check if there is room at the end of the buffer for the new data.
         */

        if ((outbuf->head + message_length) <= OUT_BUF_SIZE)
        {
            /*
             * Put data into the rec buf.
             */
            memcpy ((outbuf->buf + outbuf->head), (recvbuf + 18), message_length);

            /*
             * Move head by number of bytes read.
             */
            outbuf->head += message_length;

            if (outbuf->head == OUT_BUF_SIZE)
            {
                outbuf->head = 0;
            }
        }
        else
        {
            /*
             * If not, then split data between the end and beginning of
             * the buffer.
             */
            memcpy ((outbuf->buf + outbuf->head), (recvbuf + 18),
                    (OUT_BUF_SIZE - outbuf->head));
            memcpy (outbuf->buf, (recvbuf + 18 + (OUT_BUF_SIZE - outbuf->head)),
                    (message_length - (OUT_BUF_SIZE - outbuf->head)));

            /*
             * Move head by number of bytes written from the beginning of the buffer.
             */
            outbuf->head = (message_length - (OUT_BUF_SIZE - outbuf->head));
        }
    }

    /*
     * Send ack back to server.
     * (If a new bigger rec_seq has been received, don't re-ack 
     * an outdated packet.)
     */
    if (seq_history <= rec_seq)
    {
        send_ack (s, ntohs (data->seq));
    }
}


void
send_mpeg_data (void)
{
    int amount_written = 0;
    int afd;
    static int playmode_history = 0;

    /*
     * Play control (play, pause & stop).
     */
    switch (outbuf->playmode)
    {
    case 0:
        /*
         * PLAY: Decode data.
         */

        /*
         * Un-Pause the mvpmc so it will start playing anything that is or
         * will be copied into the mvpmc buffer.
         */
        if (local_paused == 1)
        {
            local_paused = av_pause ();
            if (debug)
            {
                printf ("mclient:UN-PAUSE returned:%d\n", local_paused);
            }
        }

        /*
         * Get file descriptor of hardware.
         */
        afd = av_audio_fd ();

        /*
         * Problems opening the audio hardware?
         */
        if (afd < 0)
        {
            outbuf->head = 0;
            outbuf->tail = 0;
            if (debug)
                printf ("mclient:Problems opening the audio hardware.\n");
        }
        else
        {
            /*
             * If there is room, wirte data in the input buffer to
             * the audio device.
             *
             * Careful, if the head pointer (in) has wrapped and the tail (out) pointer
             * has not, the data is split between the end and then the beginning of the 
             * ring buffer.
             */
            if (outbuf->head < outbuf->tail)
            {
                /*
                 * Data is split, use the end of the buffer before going back to the 
                 * beginning.
                 */
                if ((amount_written =
                     write (afd, outbuf->buf + outbuf->tail,
                            OUT_BUF_SIZE - outbuf->tail)) == 0)
                {
                    /*
                     * Couldn't use the end of the buffer.  We know the buffer is not
                     * empty from above (outbuf->head < (or !=) outbuf->tail), so the hardware
                     * must be full. Go into an idle mode.
                     */
                    if (debug)
                    {
                        printf
                            ("mclient:The audio output device is full or there is nothing to write (1).\n");
                    }
                }
                else
                {
                    /// Just in case...
                    if (amount_written < 0)
                    {
                        printf
                            ("mclinet:WARNING (1), the write returned a negative value:%d\n",
                             amount_written);
                    }
                    /*
                     * Data is split and we were able to use some or all of
                     * the data at the end of the ring buffer.  Find out which
                     * the case might be.
                     */
                    if (amount_written == (OUT_BUF_SIZE - outbuf->tail))
                    {
                        /*
                         * All the data at the end of the buffer was written.  Now,
                         * adjust the pointers and start writing from the beginning
                         * of the buffer.
                         */
                        outbuf->tail = 0;

                        if ((amount_written =
                             write (afd, outbuf->buf, outbuf->head)) == 0)
                        {
                            /*
                             * Couldn't use the start of the buffer.  The hardware is
                             * full of data.
                             */
                            if (debug)
                            {
                                printf
                                    ("mclient:The audio output device is full or there is nothing to write (2).\n");
                            }
                        }
                        else
                        {
                            /// Just in case...
                            if (amount_written < 0)
                            {
                                printf
                                    ("mclinet:WARNING (2), the write returned a negative value:%d\n",
                                     amount_written);
                            }
                            /*
                             * We were able to use some or all of
                             * the data.  Find out which the case might be.
                             */
                            if (amount_written == outbuf->head)
                            {
                                /*
                                 * We wrote all of it.
                                 */
                                outbuf->tail = outbuf->head;
                            }
                            else
                            {
                                /*
                                 * The amount written was not zero and it was not up to the 
                                 * head pointer.  Adjust the tail to only account for the 
                                 * portion of the buffer consumed.
                                 */
                                outbuf->tail = amount_written;
                            }
                        }
                    }
                    else
                    {
                        /*
                         * The amount written was not zero and it was not up to the 
                         * end of the buffer.  Adjust the tail to only account for the 
                         * portion of the buffer consumed.
                         */
                        outbuf->tail += amount_written;
                    }
                }
            }
            else
            {
                if ((amount_written =
                     write (afd, outbuf->buf + outbuf->tail,
                            outbuf->head - outbuf->tail)) == 0)
                {
                    if (debug)
                    {
                        printf
                            ("mclient:The audio output device is full or there is nothing to write (3).\n");
                    }
                }
                else
                {
                    /// Just in case...
                    if (amount_written < 0)
                    {
                        printf
                            ("mclinet:WARNING (3), the write returned a negative value:%d\n",
                             amount_written);
                    }
                    /*
                     * We wrote something:
                     * Move the tail of the ring buffer up to show the server
                     * we are actually using data.
                     */
                    outbuf->tail += amount_written;
                }
            }
        }
        break;

    case 1:
        /*
         * PAUSE: Do not play, and do not reset buffer.
         */

        /*
         * Pause the mvpmc so as not to continue to play what has already
         * been copied into the mvpmc buffer.
         */
        if (local_paused == 0)
        {
            local_paused = av_pause ();
            if (debug)
            {
                printf ("mclient:UN-PAUSE returned:%d\n", local_paused);
            }
        }
        break;

    case 3:
        /*
         * STOP: Do not play, and reset tail to beginning of buffer.
         * (Server needs this control when switching between programs.)
         */
        if (playmode_history != 3)
        {
            av_reset ();
        }

        outbuf->tail = 0;
        break;

    default:
        break;
    }
    playmode_history = outbuf->playmode;
}


void
mclient_idle_callback (mvp_widget_t * widget)
{
    static int doubletime = 100;
    static char oldstring[140] = "OriginalString";
    static int send_display_data_state = 0;
    char newstring[140];


    pthread_mutex_lock (&mclient_mutex);

    /*
     * Only use the first 40 characters.  Looks like the server
     * leaves old characters laying round past the 40th character.
     */
    sprintf (newstring, "%-40.40s\n%-40.40s\n", slimp3_display, &slimp3_display[64]);

    pthread_mutex_unlock (&mclient_mutex);

    /*
     * Set the call back for the slower 100ms interval.
     */
    doubletime = 100;

    if (strcmp (newstring, oldstring) != 0)
    {
        /*
         * If we are looking a new title data, reduce the 
         * call back interval to a faster 10ms (in anticipation
         * of the need to handle scrolling data).
         *
         * Send text to OSD.
         */
        doubletime = 10;
        mvpw_set_dialog_text (mclient, newstring);
    }

    /*
     * Send text to VFD.
     *
     * Only send data when Line1 has changed.  We don't want to send
     * text when the server is scrolling text on Line2.
     */
    if ((strncmp (newstring, oldstring, 40) != 0) && (send_display_data_state == 0))
    {
        if (debug)
            printf ("mclient:TEST:new&old are diff new:%s old:%s state:%d\n",
                    newstring, oldstring, send_display_data_state);
        /*
         * But, wait until server animation has stopped before deciding
         * to send data.
         */
        send_display_data_state = 1;
    }

    if ((strncmp (newstring, oldstring, 40) == 0) && (send_display_data_state == 1))
    {
        if (debug)
            printf ("mclient:TEST:new&old are same new:%s old:%s state:%d\n",
                    newstring, oldstring, send_display_data_state);
        snprintf (display_message, sizeof (display_message),
                  "Line1:%40.40s\n", &newstring[0]);
        display_send (display_message);

        snprintf (display_message, sizeof (display_message),
                  "Line2:%40.40s\n", &newstring[41]);
        display_send (display_message);

        send_display_data_state = 0;
    }
    /*
     * Make copy of string to compair with next time 
     * through.
     */
    strncpy (oldstring, newstring, 135);

    if (debug)
        if (doubletime == 10)
            printf ("mclient:Test:Double Time Activated.\n");
    mvpw_set_timer (mclient, mclient_idle_callback, doubletime);
}


void
receive_volume_data (unsigned short *data, int bytes_read)
{
    unsigned short vol_2, vol_1, vol_0;
    unsigned short addr_hi, addr_lo;
    uint addr;
    ulong vol_server;
    uint vol_mvpmc;
    int i;

    /*
     * Verify i2c Address for chan 1.
     *
     * Data starts at byte 18 (9), addres is at 32 (16)
     * vol_server at 38 (18).
     */
    addr_hi = data[16] & 0xff00;
    addr_lo = data[17] & 0xff00;
    addr = (addr_hi + (addr_lo >> 8));
    /*
     * Get volume data for chan 1.
     * Assume chan 1 & 2 (left & right) chan are same.
     */
    if (addr == 0x7f8)
    {
        vol_2 = (data[21]) & 0xff00;
        vol_1 = (data[18]) & 0xff00;
        vol_0 = (data[19]) & 0xff00;
        vol_server = ((vol_2 << 8) + (vol_1) + (vol_0 >> 8));
        /*
         * Mvpmc vol range appears to be between 220 to 255.  Find a
         * best fit for our vol_server we receive from the server.
         * (If we could, we might do better if we used the square
         * root of vol_server.))
         */
        vol_mvpmc = 0;
        i = 0;
        while ((volume_server[i] != vol_server) & (i <= 40))
        {
            i++;
        }
        vol_mvpmc = volume_adj[i];

        {
            av_state_t state;

            av_get_state (&state);

            /*
             * If vol_40 is zero, set mute.
             */
            if ((i == 0) & (state.mute == 0))
            {
                if (av_mute () == 1)
                {
                    mvpw_show (mute_widget);
                }
            }

            /*
             * If vol_40 is changed and not 0, clear mute.
             */
            if ((i != 0) & (state.mute == 1))
            {
                if (av_mute () == 0)
                {
                    mvpw_hide (mute_widget);
                }
            }
        }
        if (debug)
            printf ("mclient:vol_raw:%5x vol_data:%d i:%d\n", (uint) vol_server,
                    vol_mvpmc, i);
        if (av_set_volume (vol_mvpmc) < 0)
            printf ("mclient:error:volume could not be set\n");
        volume = vol_mvpmc;
    }
}


void *
mclient_loop_thread (void *arg)
{
    int socket_handle_data;
    int socket_handle_cli;
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

    fd_set read_fds;
    FD_ZERO (&read_fds);
    pthread_mutex_lock (&mutex);

    for (;;)
    {
        /*
         * Check when we get "turned on" (grab the GUI).
         */
        if (gui_state == MVPMC_STATE_MCLIENT)
        {

            /*
             * Let's try initializing mclient here.
             */
            if (debug)
                printf ("mclient:Initializing mclient\n");

            /*
             * Grab the audio hardware.
             */
            switch_hw_state (MVPMC_STATE_MCLIENT);

            if (debug)
                printf ("mclient:Initializing mclient\n");
            mclient_local_init ();

            if (debug)
                printf ("mclient:Initializing mclient_cli\n");
            cli_init ();

            /*
             * Note, had to put the slowest event first.  Looks like faster events (i.e.
             * events so fast there is one every scan) will block slower events from
             * running.
             */
            printf ("mclient:Get socket handle for mclient_cli data\n");
            socket_handle_cli = cli_server_connect ();
            printf ("mclient:Get socket handle for mclient data\n");
            socket_handle_data = mclient_server_connect ();

            mclient_socket = socket_handle_data;
            send_discovery (socket_handle_data);
            cli_send_discovery (socket_handle_cli);

            /*
             * Set up the hardware to pass mp3 data.
             * (Should only do once?...)
             */
            av_set_audio_output (AV_AUDIO_MPEG);
            av_set_audio_type (0);
            av_play ();

            /*
             * Initialize the play mode to "not start playing"
             * and "clear the buffer".
             */
            outbuf->playmode = 3;

            /*
             * Stay in loop processing server's audio data
             * until we give up the audio hardware.
             */
            while (hw_state == MVPMC_STATE_MCLIENT)
            {
                struct timeval mclient_tv;
                int n = 0;

                if ((cli_small_widget_timeout < time (NULL)) &
                    (cli_small_widget_state == SHOW))
                {
                    /*
                     * Hide small widget.
                     */
                    mvpw_raise (mclient_fullscreen);
                    cli_small_widget_state = HIDE;
                    printf ("mclient_mvpmc:Small widget HIDE.\n");
                }

                if ((cli_small_widget_timeout > time (NULL)) &
                    (cli_small_widget_state == HIDE))
                {
                    /*
                     * Show small widget.
                     */
                    mvpw_raise (mclient);
                    cli_small_widget_state = SHOW;
                    printf ("mclient_mvpmc:Small widget SHOW.\n");
                }

                /*
                 * Empty the set we are keeping an eye on and add all the ones we are interested in.
                 * (i.e. We are interested in MP3 & Control data from the music server, and additional
                 * data from the Command Line Interface (CLI) for the full screen display.)
                 */
                FD_ZERO (&read_fds);
                FD_SET (socket_handle_cli, &read_fds);
                FD_SET (socket_handle_data, &read_fds);
                /*
                 * We need to know the largest file descriptor value we need to track.
                 */
                if (socket_handle_data > n)
                    n = socket_handle_data;
                if (socket_handle_cli > n)
                    n = socket_handle_data;

                /*
                 * Wait until we receive data from server or up to 100ms
                 * (1/10 of a second).
                 */
                mclient_tv.tv_usec = 100000;

                if (select (n + 1, &read_fds, NULL, NULL, &mclient_tv) == -1)
                {
                    if (errno != EINTR)
                    {
                        if (debug)
                            printf ("mclient:select error\n");
                        abort ();
                    }
                }

                /*
                 * Check if the "select" event could have been caused because data
                 * has been sent by the server.
                 */
                if (FD_ISSET (socket_handle_data, &read_fds))
                {
                    if (debug)
                        printf
                            ("mclient_mvpmc:We found data to read on the socket_handle_data handle.\n");
                    read_packet (socket_handle_data);
                }



                /*
                 * This is the highest level hack for receiving CLI radio / streaming info.
                 * Once slimserver 6.5 is in wide use, get rid of this stuff.
                 *
                 * Check if we have a delayed request to send to the
                 * servers' CLI.
                 */
                if (cli_identical_state_interval_timer < time (NULL))
                {
                    if (cli_identical_state_interval_timer != 0)
                    {
                        if (debug)
                            printf ("mclient_cli:Detected delayed request.\n");
                        cli_data.state = UPDATE_RADIO_NOWPLAYING;
                        cli_identical_state_interval_timer = 0;
                        cli_update_playlist (socket_handle_cli);
                    }
                }



                /*
                 * Check if the "select" event could have been caused because new cli data
                 * has been sent. 
                 */
                if (FD_ISSET (socket_handle_cli, &read_fds))
                {
                    if (debug)
                        printf
                            ("mclient_mvpmc:We found data to read on the socket_handle_cli handle.\n");
                    cli_read_data (socket_handle_cli);
                }

                /*
                 * Regardless if we got here because of the "select" time out or receiving
                 * a message from the server (again a "select" event), check to see if we
                 * can send more data to the hardware, if there has been a remote control
                 * key press or if we have exited out of the music client.
                 */
                send_mpeg_data ();
            }

            /*
             * Done, we got "turned off", so close the connection.
             */
            close (socket_handle_data);
            close (socket_handle_cli);

            /*
             * Free up the alloc'd memory.
             */
            free (outbuf->buf);
            free (outbuf);
            free (recvbuf);
            free (recvbuf_back);

        }
        else
        {
            pthread_cond_wait (&mclient_cond, &mutex);
        }
    }
}

void
mclient_local_init (void)
{
    struct hostent *h;
    struct timezone tz;
    static struct in_addr hostname_mclient;

    /*
     * Create the buffer and associated info to store data before
     * sending it out to the hardware.
     */
    outbuf = ring_buf_create (OUT_BUF_SIZE);

    /*
     * Create the buffer to store data from the server.
     */
    recvbuf = (void *) calloc (1, RECV_BUF_SIZE);

    h = gethostbyname ((const char *) mclient_server);
    if (h == NULL)
    {
        printf ("mclient:Unable to get address for %s\n", mclient_server);
        exit (1);
    }
    else
    {
        printf ("mclient:Was able to get an address for:%s\n", mclient_server);
    }

    /*
     * Save address from gethostbyname structure to memory for 
     * later use.
     */
    memcpy (&hostname_mclient, h->h_addr_list[0], sizeof (hostname_mclient));
    server_addr_mclient = &hostname_mclient;

    memset (slimp3_display, ' ', DISPLAY_SIZE);
    slimp3_display[DISPLAY_SIZE] = '\0';

    setlocale (LC_ALL, "");     /* so that isprint() works properly */

    /* save start time for sending IR packet */
    gettimeofday (&uptime, &tz);
    uptime.tv_sec -= 60 * tz.tz_minuteswest; /* canonicalize to GMT/UTC */

    /*
     * Initialize pause state
     */
    local_paused = av_pause ();

    /*
     * Initialize radio title history.
     */
    {
        int i;
        for (i = 0; i < CLI_MAX_TRACKS; i++)
        {
            strcpy (cli_data.title_history[i], "-no history-");
        }
    }

    /*
     * Initialize small widget hide time out.
     */
    cli_small_widget_timeout = time (NULL) + 10;
}

/*
 * Main routine for mclient.
 */
int
music_client (void)
{
    printf ("mclient:Starting mclient pthread.\n");
    pthread_create (&mclient_loop_thread_handle, &thread_attr_small,
                    mclient_loop_thread, NULL);

    mvpw_set_dialog_text (mclient,
                          "Update text lines....................... \n line 2................................... \n line 3................................... \n");


    return 0;
}

void
mclient_exit (void)
{
    audio_clear ();
    av_stop ();
}
