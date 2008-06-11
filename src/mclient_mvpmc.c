/*
*  Copyright (C) 2006-2008, Rick Stuart
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
#include "http_stream.h"

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
* Tracks timeout of userfocus selection.
*/
int cli_userfocus_timeout;

/*
* Tracks timeout of small mclient widget.
*/
int cli_small_widget_timeout;
int cli_small_widget_state = SHOW;
int cli_small_widget_force_hide = FALSE;
int cli_fullscreen_widget_state = UNINITIALIZED;

void mclient_audio_play(mvp_widget_t * widget);

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

/*
* Passes CLI commands from outside socket_handle_cli space.
*/
char pending_cli_string[MAX_CMD_SIZE];

/*
 * Number of album covers to keep locally cached.
 */
char cached_album_covers_names[CACHED_ALBUM_COVERS_MAX][50];
unsigned int cached_album_covers_head = 0;
unsigned int cached_album_covers_tail = 0;

void
receive_mpeg_data(int s, receive_mpeg_header * data, int bytes_read)
{
    static int seq_history = 0;
    int message_length = 0;
    static int rec_seq = 0;     // for use in ack test

    rec_seq = ntohs(data->seq);
    if (seq_history > rec_seq)
    {
        // reset on server or client reboots or when SlimServer ushort resets at 64k
        seq_history = -1;
    }

    if (debug)
    {
        printf
            ("mclient:     Address:%8.8d Cntr:%8.8d     Seq:%8.8d  BytesIn:%8.8d\n",
             ntohs(data->wptr), data->control, ntohs(data->seq), bytes_read);

        if (seq_history == ntohs(data->seq))
        {
            printf("mclient:Sequence says - NO  data\n");
        }
        else
        {
            printf("mclient:Sequence says - NEW data\n");
        }

        if (outbuf->head == (ntohs(data->wptr) << 1))
        {
            printf("mclient:Server and Client pntrs - AGREE\n");
        }
        else
        {
            printf("mclient:Server and Client pntrs - DIFFERENT (by:%d)\n",
                   outbuf->head - ((ntohs(data->wptr)) << 1));
        }
    }

    /*
     * Set head pointer to value received from server.
     * (Server needs this control when switching between
     * programs (i.e. when switching between tracks).)
     */
    outbuf->head = ntohs((data->wptr) << 1);

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
    if (message_length != 0 && seq_history < ntohs(data->seq))
    {
        /*
         * Keep history of seq so next time
         * we are here we can tell if
         * we are getting new data that should be 
         * processed.
         */
        seq_history = ntohs(data->seq);

        /*
         * Check if there is room at the end of the buffer for the new data.
         */

        if ((outbuf->head + message_length) <= OUT_BUF_SIZE)
        {
            /*
             * Put data into the rec buf.
             */
            memcpy((outbuf->buf + outbuf->head), (recvbuf + 18),
                   message_length);

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
            memcpy((outbuf->buf + outbuf->head), (recvbuf + 18),
                   (OUT_BUF_SIZE - outbuf->head));
            memcpy(outbuf->buf, (recvbuf + 18 + (OUT_BUF_SIZE - outbuf->head)),
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
        send_ack(s, ntohs(data->seq));
    }
}

void
send_mpeg_data(void)
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
            local_paused = av_pause();
            if (debug)
            {
                printf("mclient:UN-PAUSE returned:%d\n", local_paused);
            }
        }

        /*
         * Get file descriptor of hardware.
         */
        afd = av_get_audio_fd();

        /*
         * Problems opening the audio hardware?
         */
        if (afd < 0)
        {
            outbuf->head = 0;
            outbuf->tail = 0;
            if (debug)
                printf("mclient:Problems opening the audio hardware.\n");
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
                     write(afd, outbuf->buf + outbuf->tail,
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
                             write(afd, outbuf->buf, outbuf->head)) == 0)
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
                     write(afd, outbuf->buf + outbuf->tail,
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

        /*
         * If we are in PLAY mode, we probably do not want to reset the hardware
         * audio buffer when we transition to STOP. But, as the server's CLI and 
         * data/control interface are not necessaraly in sync, we need to wait before
         * we can assume we do not need to reset the hardware buffer for the next
         * PLAY to STOP transition.
         *
         * The small widget hide timer is perfect for this.  It starts when a button
         * is pressed on the remote and clears several seconds later.
         */
        if ((reset_mclient_hardware_buffer == 1)
            && (cli_small_widget_timeout < time(NULL)))
        {
            printf
                ("mclient_mvpmc:send_mpeg_data: Clear the HW buffer reset flag as we are still playing a song.\n");
            reset_mclient_hardware_buffer = 0;
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
            local_paused = av_pause();
            if (debug)
            {
                printf("mclient_mvpmc:send_mpeg_data: UN-PAUSE returned:%d\n",
                       local_paused);
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
            /*
             * But only reset the buffer when the user is stopping or skipping around
             * with either the remote or web interface.  Use the CLI from the server to
             * detect these events.  As we don't know which stop indication will come first
             * (the server's data/control or CLI port) have similar code in the CLI processing
             * functions (we are in the data/control processing functions).
             */
            if (reset_mclient_hardware_buffer == 1)
            {
                printf
                    ("mclient_mvpmc:send_mpeg_data: Resetting HW audio buffer from data/control functions.\n");
                av_reset();
                reset_mclient_hardware_buffer = 0;
            }
        }

        outbuf->tail = 0;
        break;

    default:
        break;
    }
    playmode_history = outbuf->playmode;
}

void
mclient_idle_callback(mvp_widget_t * widget)
{
    static int doubletime = 100;
    static char oldstring[140] = "OriginalString";
    static int send_display_data_state = 0;
    char newstring[140];

    pthread_mutex_lock(&mclient_mutex);

    /*
     * Only use the first 40 characters.  Looks like the server
     * leaves old characters laying round past the 40th character.
     */
    sprintf(newstring, "%-40.40s\n%-40.40s\n", slimp3_display,
            &slimp3_display[64]);

    pthread_mutex_unlock(&mclient_mutex);

    /*
     * Set the call back for the slower 100ms interval.
     */
    doubletime = 100;

    if (strcmp(newstring, oldstring) != 0)
    {
        /*
         * If we are looking a new title data, reduce the 
         * call back interval to a faster 10ms (in anticipation
         * of the need to handle scrolling data).
         *
         * Send text to OSD.
         */
        doubletime = 10;
        mvpw_set_dialog_text(mclient, newstring);
        /*
         * Send text to sub fullscreen OSD as well.
         */
        // For some reason the text widget can not handle
        // the ">>" character so we will just over write it
        // with a end of string null character...
        newstring[79] = '\0';
        mvpw_set_text_str(mclient_sub_softsqueeze, newstring);
    }

    /*
     * Send text to VFD.
     *
     * Only send data when Line1 has changed.  We don't want to send
     * text when the server is scrolling text on Line2.
     */
    if ((strncmp(newstring, oldstring, 40) != 0)
        && (send_display_data_state == 0))
    {
        if (debug)
            printf
                ("mclient:TEST:new&old are diff new:%s old:%s state:%d\n",
                 newstring, oldstring, send_display_data_state);
        /*
         * But, wait until server animation has stopped before deciding
         * to send data.
         */
        send_display_data_state = 1;
    }

    if ((strncmp(newstring, oldstring, 40) == 0)
        && (send_display_data_state == 1))
    {
        if (debug)
            printf
                ("mclient:TEST:new&old are same new:%s old:%s state:%d\n",
                 newstring, oldstring, send_display_data_state);
        snprintf(display_message, sizeof(display_message), "Line1:%40.40s\n",
                 &newstring[0]);
        display_send(display_message);

        snprintf(display_message, sizeof(display_message), "Line2:%40.40s\n",
                 &newstring[41]);
        display_send(display_message);

        send_display_data_state = 0;
    }
    /*
     * Make copy of string to compair with next time 
     * through.
     */
    strncpy(oldstring, newstring, 135);

    if (debug)
        if (doubletime == 10)
            printf("mclient:Test:Double Time Activated.\n");
    mvpw_set_timer(mclient, mclient_idle_callback, doubletime);
}

void
receive_volume_data(unsigned short *data, int bytes_read)
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
        while ((volume_server[i] <= vol_server) & (i <= 40))
        {
            i++;
        }

        if (i > 40)
        {
            i = 40;
        }

        vol_mvpmc = volume_adj[i];

        {
            av_state_t state;

            av_get_state(&state);

            /*
             * If vol_40 is zero, set mute.
             */
            if ((i == 0) & (state.mute == 0))
            {
                if (av_mute() == 1)
                {
                    mvpw_show(mute_widget);
                }
            }

            /*
             * If vol_40 is changed and not 0, clear mute.
             */
            if ((i != 0) & (state.mute == 1))
            {
                if (av_mute() == 0)
                {
                    mvpw_hide(mute_widget);
                }
            }
        }
        if (debug)
            printf("mclient:vol_raw:%5x vol_data:%d i:%d\n", (uint) vol_server,
                   vol_mvpmc, i);
        if (av_set_volume(vol_mvpmc) < 0)
        {
            printf("mclient:error:volume could not be set\n");
        }
        volume = vol_mvpmc;
    }
}

void *
mclient_loop_thread(void *arg)
{
    int socket_handle_data;
    int socket_handle_cli;
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

    fd_set read_fds;
    FD_ZERO(&read_fds);
    pthread_mutex_lock(&mutex);

    for (;;)
    {
        /*
         * Check when we get "turned on" (grab the GUI).
         */
        if (gui_state == MVPMC_STATE_MCLIENT)
        {
            /*
             * Let's try initializing mclient here.
             * Grab the audio hardware.
             */
            switch_hw_state(MVPMC_STATE_MCLIENT);

            if (debug)
                printf("mclient:Initializing mclient\n");
            mclient_local_init();

            if (debug)
                printf("mclient:Initializing mclient cli\n");
            cli_init();

            /*
             * Get the socket handles for mclient ports (control-data & CLI).
             */
            printf("mclient:Get socket handle for mclient_cli data\n");
            socket_handle_cli = cli_server_connect();
            printf("mclient:Get socket handle for mclient data\n");
            socket_handle_data = mclient_server_connect();
            if ((socket_handle_cli == -1) || (socket_handle_data == -1))
            {
                /*
                 * Warning, could not open sockets for MClient.
                 * Display Warning box on OSD.
                 */
                {
                    char buf[200];

                    snprintf(buf, sizeof(buf), "%s%s%s",
                             "Could not open either the normal or CLI socket for MClient. ",
                             "We were trying to connect to Slimserver at:",
                             mclient_server ? mclient_server : "127.0.0.1");
                    gui_error(buf);
                }
            }
            else
            {

                mclient_socket = socket_handle_data;
                send_discovery(socket_handle_data);
                cli_send_discovery(socket_handle_cli);

                /*
                 * Set up the hardware to pass mp3 data.
                 * (Should only do once?...)
                 */
                av_set_audio_output(AV_AUDIO_MPEG);
                av_set_audio_type(0);
                av_play();

                /*
                 * Stay in loop processing server's audio data
                 * until we give up the audio hardware.
                 */
                while (hw_state == MVPMC_STATE_MCLIENT)
                {
                    struct timeval mclient_tv;
                    int n = 0;

                    /*
                     * Check Slimserver's version number for compatability.
                     * Only need to do this once but must wait (back off) if this 
                     * is the first time this client has been seen by the server.
                     */
                    if ((cli_data.check_server_version) &&
                        (cli_data.check_server_version_timer < time(NULL)))
                    {
                        cli_data.check_server_version = false;
                        char cmd[MAX_CMD_SIZE];
                        sprintf(cmd, "%s version ?\n", decoded_player_id);
                        cli_send_packet(socket_handle_cli, cmd);
                        sprintf(cmd, "%s mixer volume %d\n", decoded_player_id,
                                cli_data.volume);
                        cli_send_packet(socket_handle_cli, cmd);

                    }

                    /*
                     * Check if short (about 1 second) display update
                     * (for widgets like the progressbar) is needed.
                     */
                    if (cli_data.short_update_timer < time(NULL))
                    {
                        cli_data.short_update_timer = time(NULL) + 1;
                        cli_data.short_update_timer_expired = TRUE;
                        mvpw_set_graph_current(mclient_sub_progressbar,
                                               cli_data.percent);
                        mvpw_expose(mclient_sub_progressbar);
                        mvpw_set_graph_current(mclient_sub_volumebar,
                                               cli_data.volume);
                        mvpw_expose(mclient_sub_volumebar);
                        /*
                         * Has there been an outstanding CLI message fo
                         * a long period of time.  If so clear the flag
                         * as we expect we lost the transaction.
                         */
                        if (cli_data.outstanding_cli_message == TRUE)
                        {
                            cli_data.outstanding_cli_message_timer++;
                            // 2 seconds should be long enough to
                            // process a CLI message.
                            if (cli_data.outstanding_cli_message_timer > 2)
                            {
                                printf
                                    ("mclient: WARNING, Outstanding CLI message TIME OUT!\n");
                                cli_data.outstanding_cli_message = FALSE;
                                cli_data.outstanding_cli_message_timer = 0;
                            }
                        }
                    }

                    /*
                     * Switch to "Now Playing" from "Up Next" after several seconds to
                     * accomodate for the lag presented by the audio HW buffer.
                     */
                    if ((now_playing_timeout < time(NULL))
                        && (now_playing_timeout != 0))
                    {
                        cli_data.state = UPDATE_PLAYLIST_MINMINUS1 + 1;
                        now_playing_timeout = 0;
                        cli_update_playlist(socket_handle_cli);
                    }

                    /*
                     * Switch from userfocus to nowplaying on full screen mclient dispaly.
                     */
                    if ((cli_userfocus_timeout < time(NULL))
                        && (cli_userfocus_timeout != 0))
                    {
                        cli_userfocus_timeout = 0;
                        /*
                         * Need to trigger a pull of track info from server when switching from
                         * user to player focus.
                         * Set to fist state (always MINMUNIS1 plus 1).
                         */
                        cli_data.state = UPDATE_PLAYLIST_MINMINUS1 + 1;
                        cli_update_playlist(socket_handle_cli);

                        printf("mclient_mvpmc:User focus hilite off.\n");
                    }

                    /*
                     * Empty the set we are keeping an eye on and add all the ones we are 
                     * interested in.
                     * (i.e. We are interested in MP3 & Control data from the music server, 
                     * and additional data from the Command Line Interface (CLI) for the full 
                     * screen display.)
                     */
                    FD_ZERO(&read_fds);
                    FD_SET(socket_handle_cli, &read_fds);
                    FD_SET(socket_handle_data, &read_fds);
                    /*
                     * We need to know the largest file descriptor value we need to track.
                     */
                    if (socket_handle_data > n)
                        n = socket_handle_data;
                    if (socket_handle_cli > n)
                        n = socket_handle_cli;

                    /*
                     * Wait until we receive data from server or up to 100ms
                     * (1/10 of a second).
                     */
                    mclient_tv.tv_usec = 100000;

                    if (select(n + 1, &read_fds, NULL, NULL, &mclient_tv) == -1)
                    {
                        if (errno != EINTR)
                        {
                            if (debug)
                                printf("mclient:select error\n");
                            abort();
                        }
                    }

                    /*
                     * Check if the "select" event could have been caused because data
                     * has been sent by the server.
                     */
                    if (FD_ISSET(socket_handle_data, &read_fds))
                    {
                        if (debug)
                            printf
                                ("mclient_mvpmc:We found data to read on the socket_handle_data handle.\n");
                        read_packet(socket_handle_data);
                    }

                    /*
                     * This is the highest level hack for receiving CLI radio / streaming info.
                     * Once slimserver 6.5 is in wide use, get rid of this stuff.
                     *
                     * Check if we have a delayed request to send to the
                     * servers' CLI.
                     */
                    if (cli_identical_state_interval_timer < time(NULL))
                    {
                        if (cli_identical_state_interval_timer != 0)
                        {
                            if (debug)
                                printf
                                    ("mclient_cli:Detected delayed request.\n");
                            cli_data.state = UPDATE_RADIO_NOWPLAYING;
                            cli_identical_state_interval_timer = 0;
                            cli_update_playlist(socket_handle_cli);
                        }
                    }

                    /*
                     * Check if the "select" event could have been caused because new cli data
                     * has been sent. 
                     */
                    if (FD_ISSET(socket_handle_cli, &read_fds))
                    {
                        if (debug)
                            printf
                                ("mclient_mvpmc:We found data to read on the socket_handle_cli handle.\n");
                        cli_read_data(socket_handle_cli);
                    }

                    /*
                     * Need to check for a shift key event.
                     * 
                     * Has shift time expired?
                     */
                    if (remote_buttons.shift_time <= time(NULL))
                    {
                        remote_buttons.shift = FALSE;
                    }
                    if (remote_buttons.last_pressed == MVPW_KEY_RECORD)
                    {
                        remote_buttons.last_pressed = MVPW_KEY_NONE;
                        remote_buttons.shift = TRUE;
                        // Renew shift timer.
                        remote_buttons.shift_time = time(NULL) + 2;
                    }
                    else
                    {
                        switch (remote_buttons.last_pressed)
                        {
                        case MVPW_KEY_SKIP:
                            remote_buttons.last_pressed = MVPW_KEY_NONE;
                            if (remote_buttons.shift == TRUE)
                            {
                                // Renew shift timer.
                                remote_buttons.shift_time = time(NULL) + 2;
                            }
                            break;
                        case MVPW_KEY_REPLAY:
                            remote_buttons.last_pressed = MVPW_KEY_NONE;
                            if (remote_buttons.shift == TRUE)
                            {
                                // Renew shift timer.
                                remote_buttons.shift_time = time(NULL) + 2;
                            }
                        }
                    }

                    /*
                     * If time expired, need to check on buttons monitored for
                     * a "held down" event.
                     */
                    if (remote_buttons.elapsed_time <= time(NULL))
                    {
                        remote_buttons.elapsed_time = time(NULL) + 1;
                        if (remote_buttons.number_of_scans > 1)
                        {
                            /*
                             * Passed threshold, assume button held down.
                             */
                            if (remote_buttons.state != RELEASED_BUTTON)
                            {
                                /*
                                 * Same pressed button as last time.
                                 */
                                switch (remote_buttons.last_pressed)
                                {
                                case MVPW_KEY_FFWD:
                                    remote_buttons.number_of_pushes++;
                                    sprintf
                                        (pending_cli_string,
                                         "%s rate %d\n", decoded_player_id,
                                         remote_buttons.number_of_pushes);
                                    break;
                                case MVPW_KEY_REWIND:
                                    remote_buttons.number_of_pushes--;
                                    sprintf
                                        (pending_cli_string,
                                         "%s rate %d\n", decoded_player_id,
                                         remote_buttons.number_of_pushes);
                                    break;
                                }
                                remote_buttons.state = PUSHING_BUTTON;
                                remote_buttons.number_of_scans = 0;     /// Snyc
                                remote_buttons.elapsed_time = time(NULL) + 1;
                            }
                            else
                            {
                                /*
                                 * Newly pressed button since last time.
                                 */
                                switch (remote_buttons.last_pressed)
                                {
                                case MVPW_KEY_FFWD:
                                    remote_buttons.number_of_pushes = 2;
                                    sprintf
                                        (pending_cli_string,
                                         "%s rate %d\n", decoded_player_id,
                                         remote_buttons.number_of_pushes);
                                    break;
                                case MVPW_KEY_REWIND:
                                    remote_buttons.number_of_pushes = -2;
                                    sprintf
                                        (pending_cli_string,
                                         "%s rate %d\n", decoded_player_id,
                                         remote_buttons.number_of_pushes);
                                    break;
                                }
                                remote_buttons.state = PUSHING_BUTTON;
                                remote_buttons.number_of_scans = 0;     /// Snyc
                                remote_buttons.elapsed_time = time(NULL) + 1;

                            }
                        }
                        else
                        {
                            /*
                             * Assumed button not held down.
                             */
                            switch (remote_buttons.last_pressed)
                            {
                            case MVPW_KEY_FFWD:
                                break;
                            case MVPW_KEY_REWIND:
                                break;
                            }
                            remote_buttons.state = RELEASED_BUTTON;
                            remote_buttons.number_of_scans = 0; /// Snyc
                            remote_buttons.elapsed_time = time(NULL) + 1;
                        }
                    }

                    /*
                     * Any timed CLI messages to be sent out to slimserver?
                     */
                    if (cli_data.short_update_timer_expired == TRUE)
                    {
                        char cmd[MAX_CMD_SIZE];
                        cli_data.short_update_timer_expired = FALSE;
                        sprintf(cmd, "%s time ?\n", decoded_player_id);
                        cli_send_packet(socket_handle_cli, cmd);

                        sprintf(cmd, "%s mixer volume ?\n", decoded_player_id);
                        cli_send_packet(socket_handle_cli, cmd);

                        sprintf(cmd, "%s mode ?\n", decoded_player_id);
                        cli_send_packet(socket_handle_cli, cmd);
                    }

                    /*
                     * Any pending CLI messages to be sent out to slimserver?
                     */
                    if (pending_cli_string[0] != '\0')
                    {
                        /*
                         * If there is a CLI message out, don't send another.
                         */
                        if (cli_data.outstanding_cli_message != TRUE)
                        {
                            cli_send_packet(socket_handle_cli,
                                            pending_cli_string);
                            pending_cli_string[0] = '\0';
                        }
                    }

                    /*
                     * Do we need to get the cover art?
                     */
                    if ((cli_data.get_cover_art_later == TRUE)
                        && (gui_state == MVPMC_STATE_MCLIENT))
                    {
                        /*
                         * Only get new cover art if the local menu is 
                         * not active.
                         */
                        if (remote_buttons.local_menu == FALSE)
                        {
                            if (cli_data.get_cover_art_holdoff_timer <
                                time(NULL))
                            {
                                cli_data.get_cover_art_holdoff_timer =
                                    time(NULL) + 5;
                                cli_data.get_cover_art_later = FALSE;
                                cli_get_cover_art();
                            }
                        }
                    }

                    /*
                     * Do we need to get the cover art for album browser?
                     */
                    if ((cli_data.pending_proc_for_cover_art == TRUE)
                        && (gui_state == MVPMC_STATE_MCLIENT))
                    {
                        /*
                         * If there is a CLI message out, don't send another.
                         */
                        if (cli_data.outstanding_cli_message != TRUE)
                        {
                            cli_data.pending_proc_for_cover_art = FALSE;
                            /*
                             * Check if we have been triggered to restart the
                             * state machine.
                             */
                            if (cli_data.trigger_proc_for_cover_art == TRUE)
                            {
                                cli_data.trigger_proc_for_cover_art = FALSE;
                                cli_data.state_for_cover_art =
                                    GET_1ST_ALBUM_COVERART;
                            }
                            mclient_browse_by_cover();
                        }
                    }
                    else
                    {
                        /*
                         * If there are no outstanding requests, then we
                         * can start over.
                         */
                        if (cli_data.outstanding_cli_message == FALSE)
                        {
                            /*
                             * If we are not getting cover art, check, we may
                             * have been triggered to start over, if so, set 
                             * it up the next go around.
                             */
                            if (cli_data.trigger_proc_for_cover_art == TRUE)
                            {
                                cli_data.pending_proc_for_cover_art = TRUE;
                            }
                        }
                    }

                    /*
                     * Do we need to get the update cover art widgets for 
                     * album browser?
                     */
                    if ((cli_data.pending_proc_for_cover_art_widget == TRUE) &&
                        (gui_state == MVPMC_STATE_MCLIENT)
                        && (remote_buttons.local_menu_browse == TRUE))
                    {
                        cli_data.pending_proc_for_cover_art_widget = FALSE;
                        mclient_browse_by_cover_widget();
                    }

                    /*
                     * If this is the first time through, send a command to 
                     * the CLI that will trigger a CLI / fullscreen update.
                     */
                    if (cli_fullscreen_widget_state == UNINITIALIZED)
                    {
                        char cmd[MAX_CMD_SIZE];

                        /*
                         * Initialize the CLI state machine to the beginning 
                         * of getting playlist information.
                         */
                        cli_data.state = UPDATE_PLAYLIST_MINMINUS1 + 1;

                        sprintf(cmd, "%s listen 1\n", decoded_player_id);
                        cli_send_packet(socket_handle_cli, cmd);
                        cli_fullscreen_widget_state = INITIALIZED;

                        /*
                         * Grab new duration for current track.
                         */
                        {
                            char cmd[MAX_CMD_SIZE];
                            sprintf(cmd, "%s duration ?\n", decoded_player_id);
                            cli_send_packet(socket_handle_cli, cmd);
                        }

                        /*
                         * Set up a call to initialize the total number of 
                         * albums.
                         */
                        sprintf(cmd, "%s info total albums ?\n",
                                decoded_player_id);
                        cli_send_packet(socket_handle_cli, cmd);
                    }

                    /*
                     * Regardless if we got here because of the "select" time 
                     * out or receiving a message from the server (again a 
                     * "select" event), check to see if we can send more 
                     * data to the hardware, if there has been a remote control
                     * key press or if we have exited out of the music client.
                     */
                    send_mpeg_data();
                }

                /*
                 * The hardware state has changed and is no longer reserved 
                 * for mclient (i.e. we got "turned off") so start shutting 
                 * down.
                 */
                audio_clear();
                sleep(1);       /// ### Adding delays, need to test if necessary for stability.

                av_stop();
                sleep(1);       /// ### Adding delays, need to test if necessary for stability.

            }                   // Could not open sockets, so jump here.

            /*
             * Close the connection.
             */
            close(socket_handle_data);
            close(socket_handle_cli);
            sleep(1);           /// ### Adding delays, need to test if necessary for stability.

            /*
             * Free up the alloc'd memory.
             */
            free(outbuf->buf);
            free(outbuf);
            free(recvbuf);
            free(recvbuf_back);

            /*
             * Free up virtual disk space (delet any cached
             * album cover images.
             */
            printf("TEST>>> We have this many cached album covers: head:%d tail:%d\n", cached_album_covers_head, cached_album_covers_tail);     ///###
            // Let's be safe (no infinit loops).
            cached_album_covers_head %= CACHED_ALBUM_COVERS_MAX;
            while (cached_album_covers_head != cached_album_covers_tail)
            {
                // Delet album cover pointed at by tail.
                unlink(cached_album_covers_names[cached_album_covers_tail]);
                printf("TEST>>> Deleting this cover:%s\n", cached_album_covers_names[cached_album_covers_tail]);        ///###
                cached_album_covers_tail =
                    (cached_album_covers_tail + 1) % CACHED_ALBUM_COVERS_MAX;
            }
            // Don't forget the newest one.
            unlink(cached_album_covers_names[cached_album_covers_tail]);
            printf("TEST>>> Deleting this last cover:%s\n", cached_album_covers_names[cached_album_covers_tail]);       ///###
            unlink("/tmp/cover_current");
/// ### Not yet      unlink("/tmp/cover_*"); 

            /*
             * TEST: See if we need a hold off state to sync
             * activity between threads.
             */
            hw_state = MVPMC_STATE_NONE;
        }
        else
        {
            pthread_cond_wait(&mclient_cond, &mutex);
        }
    }
}

void
mclient_local_init(void)
{
    struct hostent *h;
    struct timezone tz;
    static struct in_addr hostname_mclient;
    uint vol_mvpmc = 0;

    /*
     * Create the buffer and associated info to store data before
     * sending it out to the hardware.
     */
    outbuf = ring_buf_create(OUT_BUF_SIZE);

    /*
     * Create the buffer to store data from the server.
     */
    recvbuf = (void *)calloc(1, RECV_BUF_SIZE);

    h = gethostbyname((const char *)mclient_server);
    if (h == NULL)
    {
        printf("mclient:Unable to get address for %s\n", mclient_server);
        exit(1);
    }
    else
    {
        printf("mclient:Was able to get an address for:%s\n", mclient_server);
    }

    /*
     * Save address from gethostbyname structure to memory for 
     * later use.
     */
    memcpy(&hostname_mclient, h->h_addr_list[0], sizeof(hostname_mclient));
    server_addr_mclient = &hostname_mclient;

    memset(slimp3_display, ' ', DISPLAY_SIZE);
    slimp3_display[DISPLAY_SIZE] = '\0';

    setlocale(LC_ALL, "");      /* so that isprint() works properly */

    // Save start time for sending IR packet
    gettimeofday(&uptime, &tz);
    // Canonicalize to GMT/UTC 
    uptime.tv_sec -= 60 * tz.tz_minuteswest;

    // Initialize pause state
    local_paused = av_pause();

    // Initialize radio title history.
    {
        int i;
        for (i = 0; i < CLI_MAX_TRACKS; i++)
        {
            strcpy(cli_data.title_history[i], "-no history-");
        }
    }

    // Initialize small widget hide time out.
    cli_small_widget_timeout = time(NULL) + 10;

    // Force to draw boxes around cover art browser windows.
    cli_data.pending_proc_for_cover_art_widget = TRUE;

    // Force 1st time through initialization of full screen.
    cli_fullscreen_widget_state = UNINITIALIZED;

    // Force the initial time out.
    cli_data.short_update_timer = time(NULL) + 1;

    // CLI samaphore for cover art & full scree CLI commands.
    cli_data.outstanding_cli_message = FALSE;
    cli_data.outstanding_cli_message_timer = 0;

    // Initialize the play mode to "not start playing"
    // and "clear the buffer".
    outbuf->playmode = 3;

    // Sart with local menus off.
    remote_buttons.local_menu = FALSE;
    remote_buttons.local_menu_browse = FALSE;

    // Set volume to zero to start with.
    if (av_set_volume(vol_mvpmc) < 0)
    {
        printf("mclient:error:volume could not be set\n");
    }
}

 /*
  * Main routine for mclient.
  */
int
music_client(void)
{
    printf("mclient:Starting mclient pthread.\n");
    pthread_create(&mclient_loop_thread_handle, &thread_attr_small,
                   mclient_loop_thread, NULL);

    mvpw_set_dialog_text(mclient,
                         "Update text lines....................... \n line 2................................... \n line 3................................... \n");

    return 0;
}

void
mclient_exit(void)
{
    uint limit_counter = 500;

    hw_state = MVPMC_STATE_MCLIENT_SHUTDOWN;

    while ((hw_state == MVPMC_STATE_MCLIENT_SHUTDOWN) && (limit_counter > 0))
    {
        limit_counter--;
        printf(".");
        sleep(1);
    }
    printf("\n");

    if (hw_state == MVPMC_STATE_MCLIENT_SHUTDOWN)
    {
        printf
            (">>>TEST:mclient_exit: WARNING:The mclient thread did NOT FINISHED!\n");
    }
    else
    {
        printf(">>>TEST:mclient_exit: The mclient thread has FINISHED.\n");
    }
}

void
mclient_browse_by_cover(void)
{
    // How to get number of albums, album_id, track_id and album image:
    //
    // ...command examples:
    // info total albums
    // albums 0 6
    // ...that get's you back 6 album IDs and the title of each.  Now for each do:
    // titles 0 1 album_id:<album_id>
    // ...that get's you back the 1st track from the album.  Now you have that track's ID.
    // http://<server's ip addr>:9000/music/<track's ID>/cover.jpg

    char no_cover_art_text[500];
    int album_number;

    // State machine for requesting album and track id information.
    if (pending_cli_string[0] == '\0')
    {
        // Pull 6 albums
        switch (cli_data.state_for_cover_art)
        {
        case GET_1ST_ALBUM_COVERART:
            // Generate album index for next 6 albums here.
            for (album_number = 0; album_number < 6; album_number++)
            {
                cli_data.album_index_plus_start_for_cover_art[album_number] =
                    cli_data.album_start_index_for_cover_art + album_number;
                if (cli_data.
                    album_index_plus_start_for_cover_art[album_number] >=
                    cli_data.album_max_index_for_cover_art)
                {
                    cli_data.
                        album_index_plus_start_for_cover_art[album_number] %=
                        cli_data.album_max_index_for_cover_art;
                }
            }

            // While in the first state,
            // Show user a message indicating new images are loading.
            sprintf(no_cover_art_text, "Loading album %d (of %d)...",
                    cli_data.album_index_plus_start_for_cover_art[0],
                    cli_data.album_max_index_for_cover_art);
            mvpw_set_text_str(mclient_sub_alt_image_1_1, no_cover_art_text);
            sprintf(no_cover_art_text, "Loading album %d...",
                    cli_data.album_index_plus_start_for_cover_art[1]);
            mvpw_set_text_str(mclient_sub_alt_image_1_2, no_cover_art_text);
            sprintf(no_cover_art_text, "Loading album %d...",
                    cli_data.album_index_plus_start_for_cover_art[2]);
            mvpw_set_text_str(mclient_sub_alt_image_1_3, no_cover_art_text);
            sprintf(no_cover_art_text, "Loading album %d...",
                    cli_data.album_index_plus_start_for_cover_art[3]);
            mvpw_set_text_str(mclient_sub_alt_image_2_1, no_cover_art_text);
            sprintf(no_cover_art_text, "Loading album %d...",
                    cli_data.album_index_plus_start_for_cover_art[4]);
            mvpw_set_text_str(mclient_sub_alt_image_2_2, no_cover_art_text);
            sprintf(no_cover_art_text, "Loading album %d...",
                    cli_data.album_index_plus_start_for_cover_art[5]);
            mvpw_set_text_str(mclient_sub_alt_image_2_3, no_cover_art_text);
            {
                char album_info[500];
                sprintf(album_info,
                        ">>>Loading albums %d through %d of %d albums<<<\n--please wait--   --please wait--   --please wait---",
                        cli_data.album_index_plus_start_for_cover_art[0] + 1,
                        cli_data.album_index_plus_start_for_cover_art[5] + 1,
                        cli_data.album_max_index_for_cover_art);
                mvpw_set_text_str(mclient_sub_alt_image_info, album_info);
            }
            mvpw_show(mclient_sub_alt_image_1_1);
            mvpw_raise(mclient_sub_alt_image_1_1);
            mvpw_show(mclient_sub_alt_image_1_2);
            mvpw_raise(mclient_sub_alt_image_1_2);
            mvpw_show(mclient_sub_alt_image_1_3);
            mvpw_raise(mclient_sub_alt_image_1_3);
            mvpw_show(mclient_sub_alt_image_2_1);
            mvpw_raise(mclient_sub_alt_image_2_1);
            mvpw_show(mclient_sub_alt_image_2_2);
            mvpw_raise(mclient_sub_alt_image_2_2);
            mvpw_show(mclient_sub_alt_image_2_3);
            mvpw_raise(mclient_sub_alt_image_2_3);
            mvpw_show(mclient_sub_alt_image_info);
            mvpw_raise(mclient_sub_alt_image_info);
            // update the position we are in the album collecion.
            // Size bar propotional to where we are in album list.
            mvpw_set_graph_current
                (mclient_sub_browsebar,
                 ((100 * cli_data.album_start_index_for_cover_art) /
                  cli_data.album_max_index_for_cover_art));
            mvpw_show(mclient_sub_browsebar);
            mvpw_raise(mclient_sub_browsebar);

            // Get 1st album id.
            cli_data.album_index_1_of_6_for_cover_art = 0;
            cli_data.album_index_for_cover_art =
                cli_data.album_index_plus_start_for_cover_art[0];
            sprintf(pending_cli_string, "%s albums %d 1\n", decoded_player_id,
                    cli_data.album_index_for_cover_art);
            break;

        case GET_1ST_TRACK_COVERART:
            // Get 1st track id.

            // Initialize the album_id_for_cover_art for this album to default -1 in case there
            // is no cover art and no album art tag is returned form the server.
            cli_data.artwork_track_id[0] = -1;

            // Added "J" tag eventhough it is only defined stating w/7.0.
            // This change will not get: genre, artist, album & duration
            // The "J" tag only works in 7.0+ but doesn't appear to have a negative impact on earlier versions of
            // the server.
            sprintf(pending_cli_string,
                    "%s titles 0 1 album_id:%d tags:J\n", decoded_player_id,
                    cli_data.album_id_for_cover_art[0]);
            break;

        case GET_2ND_ALBUM_COVERART:
            // Display 1st album text in widget.
            mvpw_show(mclient_sub_alt_image_1_1);
            mvpw_raise(mclient_sub_alt_image_1_1);
            sprintf(no_cover_art_text, "%s...",
                    cli_data.album_name_for_cover_art[0]);
            mvpw_set_text_str(mclient_sub_alt_image_1_1, no_cover_art_text);

            // Get 2nd album id.
            cli_data.album_index_1_of_6_for_cover_art = 1;
            cli_data.album_index_for_cover_art =
                cli_data.album_index_plus_start_for_cover_art[1];
            sprintf(pending_cli_string, "%s albums %d 1\n", decoded_player_id,
                    cli_data.album_index_for_cover_art);
            break;

        case GET_2ND_TRACK_COVERART:
            // Get 2st track id.

            // Initialize the album_id_for_cover_art for this album to default -1 in case there
            // is no cover art and no album art tag is returned form the server.
            cli_data.artwork_track_id[1] = -1;

            sprintf(pending_cli_string,
                    "%s titles 0 1 album_id:%d tags:J\n", decoded_player_id,
                    cli_data.album_id_for_cover_art[1]);
            break;

        case GET_3RD_ALBUM_COVERART:
            // Display 2nd album text in widget.
            mvpw_show(mclient_sub_alt_image_1_2);
            mvpw_raise(mclient_sub_alt_image_1_2);
            sprintf(no_cover_art_text, "%s...",
                    cli_data.album_name_for_cover_art[1]);
            mvpw_set_text_str(mclient_sub_alt_image_1_2, no_cover_art_text);

            // Get 3rd album id.
            cli_data.album_index_1_of_6_for_cover_art = 2;
            cli_data.album_index_for_cover_art =
                cli_data.album_index_plus_start_for_cover_art[2];
            sprintf(pending_cli_string, "%s albums %d 1\n", decoded_player_id,
                    cli_data.album_index_for_cover_art);
            break;

        case GET_3RD_TRACK_COVERART:
            // Get 3rd track id.

            // Initialize the album_id_for_cover_art for this album to default -1 in case there
            // is no cover art and no album art tag is returned form the server.
            cli_data.artwork_track_id[2] = -1;

            sprintf(pending_cli_string,
                    "%s titles 0 1 album_id:%d tags:J\n", decoded_player_id,
                    cli_data.album_id_for_cover_art[2]);
            break;

        case GET_4TH_ALBUM_COVERART:
            // Display 3rd album text in widget.
            mvpw_show(mclient_sub_alt_image_1_3);
            mvpw_raise(mclient_sub_alt_image_1_3);
            sprintf(no_cover_art_text, "%s...",
                    cli_data.album_name_for_cover_art[2]);
            mvpw_set_text_str(mclient_sub_alt_image_1_3, no_cover_art_text);

            // Get 4th album id.
            cli_data.album_index_1_of_6_for_cover_art = 3;
            cli_data.album_index_for_cover_art =
                cli_data.album_index_plus_start_for_cover_art[3];
            sprintf(pending_cli_string, "%s albums %d 1\n", decoded_player_id,
                    cli_data.album_index_for_cover_art);
            break;

        case GET_4TH_TRACK_COVERART:
            // Get 4th track id.

            // Initialize the album_id_for_cover_art for this album to default -1 in case there
            // is no cover art and no album art tag is returned form the server.
            cli_data.artwork_track_id[3] = -1;

            sprintf(pending_cli_string,
                    "%s titles 0 1 album_id:%d tags:J\n", decoded_player_id,
                    cli_data.album_id_for_cover_art[3]);
            break;

        case GET_5TH_ALBUM_COVERART:
            // Display 4th album text in widget.
            mvpw_show(mclient_sub_alt_image_2_1);
            mvpw_raise(mclient_sub_alt_image_2_1);
            sprintf(no_cover_art_text, "%s...",
                    cli_data.album_name_for_cover_art[3]);
            mvpw_set_text_str(mclient_sub_alt_image_2_1, no_cover_art_text);

            // Get 5th album id.
            cli_data.album_index_1_of_6_for_cover_art = 4;
            cli_data.album_index_for_cover_art =
                cli_data.album_index_plus_start_for_cover_art[4];
            sprintf(pending_cli_string, "%s albums %d 1\n", decoded_player_id,
                    cli_data.album_index_for_cover_art);
            break;

        case GET_5TH_TRACK_COVERART:
            // Get 5th track id.

            // Initialize the album_id_for_cover_art for this album to default -1 in case there
            // is no cover art and no album art tag is returned form the server.
            cli_data.artwork_track_id[4] = -1;

            sprintf(pending_cli_string,
                    "%s titles 0 1 album_id:%d tags:J\n", decoded_player_id,
                    cli_data.album_id_for_cover_art[4]);
            break;

        case GET_6TH_ALBUM_COVERART:
            // Display 5th album text in widget.
            mvpw_show(mclient_sub_alt_image_2_2);
            mvpw_raise(mclient_sub_alt_image_2_2);
            sprintf(no_cover_art_text, "%s...",
                    cli_data.album_name_for_cover_art[4]);
            mvpw_set_text_str(mclient_sub_alt_image_2_2, no_cover_art_text);

            // Get 6th album id.
            cli_data.album_index_1_of_6_for_cover_art = 5;
            cli_data.album_index_for_cover_art =
                cli_data.album_index_plus_start_for_cover_art[5];
            sprintf(pending_cli_string, "%s albums %d 1\n", decoded_player_id,
                    cli_data.album_index_for_cover_art);
            break;

        case GET_6TH_TRACK_COVERART:
            // Get 6th track id.

            // Initialize the album_id_for_cover_art for this album to default -1 in case there
            // is no cover art and no album art tag is returned form the server.
            cli_data.artwork_track_id[5] = -1;

            sprintf(pending_cli_string,
                    "%s titles 0 1 album_id:%d tags:J\n", decoded_player_id,
                    cli_data.album_id_for_cover_art[5]);
            break;

        case DISPLAY_1ST_COVER_COVERART:
            // Display 6th album text in widget.
            mvpw_show(mclient_sub_alt_image_2_3);
            mvpw_raise(mclient_sub_alt_image_2_3);
            sprintf(no_cover_art_text, "%s...",
                    cli_data.album_name_for_cover_art[5]);
            mvpw_set_text_str(mclient_sub_alt_image_2_3, no_cover_art_text);

            // Pull cover art for all 6 albums.
            mclient_get_browser_cover_art(0, mclient_sub_image_1_1,
                                          mclient_sub_alt_image_1_1);
            break;

        case DISPLAY_2ND_COVER_COVERART:
            mclient_get_browser_cover_art(1, mclient_sub_image_1_2,
                                          mclient_sub_alt_image_1_2);
            break;

        case DISPLAY_3RD_COVER_COVERART:
            mclient_get_browser_cover_art(2, mclient_sub_image_1_3,
                                          mclient_sub_alt_image_1_3);
            break;

        case DISPLAY_4TH_COVER_COVERART:
            mclient_get_browser_cover_art(3, mclient_sub_image_2_1,
                                          mclient_sub_alt_image_2_1);
            break;

        case DISPLAY_5TH_COVER_COVERART:
            mclient_get_browser_cover_art(4, mclient_sub_image_2_2,
                                          mclient_sub_alt_image_2_2);
            break;

        case DISPLAY_6TH_COVER_COVERART:
            mclient_get_browser_cover_art(5, mclient_sub_image_2_3,
                                          mclient_sub_alt_image_2_3);
            break;

        case GET_TOTAL_NUM_ALBUMS:
            sprintf(pending_cli_string, "%s info total albums ?\n",
                    decoded_player_id);
            cli_data.album_index_for_cover_art++;
            break;

        default:
            cli_data.state_for_cover_art = IDLE_COVERART;

            // This is the last state, before going idle
            // set up to update widget after loading new cover art.
            cli_data.state_for_cover_art_widget = ADJ_COVERART_WIDGET;
            cli_data.pending_proc_for_cover_art_widget = TRUE;
            break;
        }
    }

    // Switch to next state before exeting.
    if (cli_data.state_for_cover_art != IDLE_COVERART)
    {
        cli_data.state_for_cover_art++;
        if (cli_data.state_for_cover_art >= LAST_STATE_COVERART)
        {
            cli_data.state_for_cover_art = IDLE_COVERART;
        }
    }
}

void
mclient_get_browser_cover_art(int index, mvp_widget_t * mclient_sub_image,
                              mvp_widget_t * mclient_sub_alt_image)
{
    char url_string[100];
    char no_cover_art_text[500];
    char cached_image_filename[50];
    int ret_val;

    // If file does not exits, get new image.
    ret_val = -1;

    // Part of back compat:
    // If we are connected to the perferred version of SlimServer/SqueezeServer use the optimal
    // method of retrieving a cover image.
    if (cli_data.slim_composit_ver == SLIMSERVER_VERSION_COMPOSIT)
    {
        sprintf(url_string, "http://%s:9000/music/%d/cover_100x100\n",
                mclient_server, cli_data.track_id_for_cover_art[index]);

        sprintf(cached_image_filename, "/tmp/cover_%d",
                cli_data.track_id_for_cover_art[index]);
        printf("mclient: Album cover URL:%s local file name:%s\n", url_string,
               cached_image_filename);

        // Do we already know there is no cover art for this album?
        printf("TEST>>>Album_id_for_cover_art:%d index:%d\n", cli_data.album_id_for_cover_art[index], index);   ///###
        if (cli_data.artwork_track_id[index] != -1)
        {

            if (access(cached_image_filename, R_OK) != 0)
            {
                printf("TEST>>> Didn't find the cover image:%s, getting another one.\n", cached_image_filename);        ///###
                unsigned int tries = 0;
                while ((ret_val != 0) && (tries++ < 4))
                {
                    ret_val =
                        fetch_cover_image(cached_image_filename, url_string);
                }
                // New cover?  Log it.
                if (ret_val == 0)
                {
                    // Too many covers? Delet oldest.
                    printf("TEST>>> Too many covers? head+1:%d tail:%d\n", (cached_album_covers_head + 1) % CACHED_ALBUM_COVERS_MAX, cached_album_covers_tail); ///###
                    if (((cached_album_covers_head +
                          1) % CACHED_ALBUM_COVERS_MAX) ==
                        cached_album_covers_tail)
                    {
                        // Delet album cover pointed at by tail.
                        unlink(cached_album_covers_names
                               [cached_album_covers_tail]);
                        printf("TEST>>> Deleting this cover:%s\n", cached_album_covers_names[cached_album_covers_tail]);        ///###
                        cached_album_covers_tail =
                            (cached_album_covers_tail +
                             1) % CACHED_ALBUM_COVERS_MAX;
                    }
                    cached_album_covers_head =
                        (cached_album_covers_head +
                         1) % CACHED_ALBUM_COVERS_MAX;
                    strncpy(cached_album_covers_names[cached_album_covers_head],
                            cached_image_filename,
                            strlen(cached_image_filename));
                }
            }
            else
            {
                ret_val = 0;
                printf("TEST>>> Found the cover image:%s, use the one we have.\n", cached_image_filename);      ///###
            }
        }

    }
    // If not, assume the image is a jpeg.
    else
    {
        sprintf(url_string, "http://%s:9000/music/%d/cover.jpg\n",
                mclient_server, cli_data.track_id_for_cover_art[index]);

        sprintf(cached_image_filename, "/tmp/cover_%d",
                cli_data.track_id_for_cover_art[index]);
        printf("mclient: Album cover URL:%s local file name:%s\n", url_string,
               cached_image_filename);

        if (access(cached_image_filename, R_OK) != 0)
        {
            printf("TEST>>> Didn't find the cover image:%s, getting another one.\n", cached_image_filename);    ///###
            unsigned int tries = 0;
            while ((ret_val != 0) && (tries++ < 4))
            {
                ret_val = fetch_cover_image(cached_image_filename, url_string);
            }
            // New cover?  Log it.
            if (ret_val == 0)
            {
                // Too many covers? Delet oldest.
                printf("TEST>>> Too many covers? head+1:%d tail:%d\n", (cached_album_covers_head + 1) % CACHED_ALBUM_COVERS_MAX, cached_album_covers_tail);     ///###
                if (((cached_album_covers_head +
                      1) % CACHED_ALBUM_COVERS_MAX) == cached_album_covers_tail)
                {
                    // Delet album cover pointed at by tail.
                    unlink(cached_album_covers_names[cached_album_covers_tail]);
                    printf("TEST>>> Deleting this cover:%s\n", cached_album_covers_names[cached_album_covers_tail]);    ///###
                    cached_album_covers_tail =
                        (cached_album_covers_tail +
                         1) % CACHED_ALBUM_COVERS_MAX;
                }
                cached_album_covers_head =
                    (cached_album_covers_head + 1) % CACHED_ALBUM_COVERS_MAX;
                strncpy(cached_album_covers_names[cached_album_covers_head],
                        cached_image_filename, strlen(cached_image_filename));
            }
        }
        else
        {
            ret_val = 0;
            printf("TEST>>> Found the cover image:%s, use the one we have.\n", cached_image_filename);  ///###
        }

        unsigned int tries = 0;
        while ((ret_val != 0) && (tries++ < 4))
        {
            ret_val = fetch_cover_image(cached_image_filename, url_string);
        }
    }

    if ((mvpw_set_image(mclient_sub_image, cached_image_filename) == 0)
        && (ret_val == 0))
    {
        mvpw_show(mclient_sub_image);
        mvpw_raise(mclient_sub_image);
    }
    else
    {
        mvpw_hide(mclient_sub_image);
        mvpw_show(mclient_sub_alt_image);
        mvpw_raise(mclient_sub_alt_image);
        sprintf(no_cover_art_text, "%s...\nNo ArtWork for this album.",
                cli_data.album_name_for_cover_art[index]);
        mvpw_set_text_str(mclient_sub_alt_image, no_cover_art_text);
    }
    // Set up for another call here as this case does not
    // deal w/the CLI parsing function where this normally
    // happens.
    cli_data.pending_proc_for_cover_art = TRUE;
}

void
mclient_browse_by_cover_widget(void)
{
    // State machine for updating album cover widgets
    if (pending_cli_string[0] == '\0')
    {
        switch (cli_data.state_for_cover_art_widget)
        {
        case ADJ_COVERART_WIDGET:
            {
                int row, col;
                bool user_focus;
                mvpw_text_attr_t attr;

                for (row = 0; row < 3; row++)
                {
                    for (col = 0; col < 3; col++)
                    {
                        if (((cli_data.
                              row_for_cover_art ==
                              row) & (cli_data.
                                      col_for_cover_art == col))
                            || ((cli_data.row_for_cover_art == row) &
                                (row == 2)))
                        {
                            user_focus = TRUE;
                        }
                        else
                        {
                            user_focus = FALSE;
                        }

                        switch ((row * 3) + col)
                        {
                        case 0:
                            mvpw_get_text_attr(mclient_sub_image_1_1, &attr);
                            if (user_focus == TRUE)
                            {
                                attr.border = MVPW_GREEN;
                            }
                            else
                            {
                                attr.border = MVPW_MIDNIGHTBLUE;
                            }
                            mvpw_set_text_attr(mclient_sub_image_1_1, &attr);

                            mvpw_get_text_attr(mclient_sub_alt_image_1_1,
                                               &attr);
                            if (user_focus == TRUE)
                            {
                                attr.border = MVPW_GREEN;
                            }
                            else
                            {
                                attr.border = MVPW_MIDNIGHTBLUE;
                            }
                            mvpw_set_text_attr(mclient_sub_alt_image_1_1,
                                               &attr);
                            if (user_focus == TRUE)
                            {
                                char album_info[500];
                                sprintf(album_info,
                                        "Album %d of %d\nAlbum:%s\nArtist:%s",
                                        cli_data.
                                        album_index_plus_start_for_cover_art[0]
                                        + 1,
                                        cli_data.album_max_index_for_cover_art,
                                        cli_data.album_name_for_cover_art[0],
                                        cli_data.artist_name_for_cover_art[0]);
                                mvpw_set_text_str(mclient_sub_alt_image_info,
                                                  album_info);
                            }
                            break;

                        case 1:
                            mvpw_get_text_attr(mclient_sub_image_1_2, &attr);
                            if (user_focus == TRUE)
                            {
                                attr.border = MVPW_GREEN;
                            }
                            else
                            {
                                attr.border = MVPW_MIDNIGHTBLUE;
                            }
                            mvpw_set_text_attr(mclient_sub_image_1_2, &attr);

                            mvpw_get_text_attr(mclient_sub_alt_image_1_2,
                                               &attr);
                            if (user_focus == TRUE)
                            {
                                attr.border = MVPW_GREEN;
                            }
                            else
                            {
                                attr.border = MVPW_MIDNIGHTBLUE;
                            }
                            mvpw_set_text_attr(mclient_sub_alt_image_1_2,
                                               &attr);

                            if (user_focus == TRUE)
                            {
                                char album_info[500];
                                sprintf(album_info,
                                        "Album %d of %d\nAlbum:%s\nArtist:%s",
                                        cli_data.
                                        album_index_plus_start_for_cover_art[1]
                                        + 1,
                                        cli_data.album_max_index_for_cover_art,
                                        cli_data.album_name_for_cover_art[1],
                                        cli_data.artist_name_for_cover_art[1]);
                                mvpw_set_text_str(mclient_sub_alt_image_info,
                                                  album_info);
                            }
                            break;

                        case 2:
                            mvpw_get_text_attr(mclient_sub_image_1_3, &attr);
                            if (user_focus == TRUE)
                            {
                                attr.border = MVPW_GREEN;
                            }
                            else
                            {
                                attr.border = MVPW_MIDNIGHTBLUE;
                            }
                            mvpw_set_text_attr(mclient_sub_image_1_3, &attr);

                            mvpw_get_text_attr(mclient_sub_alt_image_1_3,
                                               &attr);
                            if (user_focus == TRUE)
                            {
                                attr.border = MVPW_GREEN;
                            }
                            else
                            {
                                attr.border = MVPW_MIDNIGHTBLUE;
                            }
                            mvpw_set_text_attr(mclient_sub_alt_image_1_3,
                                               &attr);

                            if (user_focus == TRUE)
                            {
                                char album_info[500];
                                sprintf(album_info,
                                        "Album %d of %d\nAlbum:%s\nArtist:%s",
                                        cli_data.
                                        album_index_plus_start_for_cover_art[2]
                                        + 1,
                                        cli_data.album_max_index_for_cover_art,
                                        cli_data.album_name_for_cover_art[2],
                                        cli_data.artist_name_for_cover_art[2]);
                                mvpw_set_text_str(mclient_sub_alt_image_info,
                                                  album_info);
                            }
                            break;

                        case 3:
                            mvpw_get_text_attr(mclient_sub_image_2_1, &attr);
                            if (user_focus == TRUE)
                            {
                                attr.border = MVPW_GREEN;
                            }
                            else
                            {
                                attr.border = MVPW_MIDNIGHTBLUE;
                            }
                            mvpw_set_text_attr(mclient_sub_image_2_1, &attr);

                            mvpw_get_text_attr(mclient_sub_alt_image_2_1,
                                               &attr);
                            if (user_focus == TRUE)
                            {
                                attr.border = MVPW_GREEN;
                            }
                            else
                            {
                                attr.border = MVPW_MIDNIGHTBLUE;
                            }
                            mvpw_set_text_attr(mclient_sub_alt_image_2_1,
                                               &attr);

                            if (user_focus == TRUE)
                            {
                                char album_info[500];
                                sprintf(album_info,
                                        "Album %d of %d\nAlbum:%s\nArtist:%s",
                                        cli_data.
                                        album_index_plus_start_for_cover_art[3]
                                        + 1,
                                        cli_data.album_max_index_for_cover_art,
                                        cli_data.album_name_for_cover_art[3],
                                        cli_data.artist_name_for_cover_art[3]);
                                mvpw_set_text_str(mclient_sub_alt_image_info,
                                                  album_info);
                            }
                            break;

                        case 4:
                            mvpw_get_text_attr(mclient_sub_image_2_2, &attr);
                            if (user_focus == TRUE)
                            {
                                attr.border = MVPW_GREEN;
                            }
                            else
                            {
                                attr.border = MVPW_MIDNIGHTBLUE;
                            }
                            mvpw_set_text_attr(mclient_sub_image_2_2, &attr);

                            mvpw_get_text_attr(mclient_sub_alt_image_2_2,
                                               &attr);
                            if (user_focus == TRUE)
                            {
                                attr.border = MVPW_GREEN;
                            }
                            else
                            {
                                attr.border = MVPW_MIDNIGHTBLUE;
                            }
                            mvpw_set_text_attr(mclient_sub_alt_image_2_2,
                                               &attr);

                            if (user_focus == TRUE)
                            {
                                char album_info[500];
                                sprintf(album_info,
                                        "Album %d of %d\nAlbum:%s\nArtist:%s",
                                        cli_data.
                                        album_index_plus_start_for_cover_art[4]
                                        + 1,
                                        cli_data.album_max_index_for_cover_art,
                                        cli_data.album_name_for_cover_art[4],
                                        cli_data.artist_name_for_cover_art[4]);
                                mvpw_set_text_str(mclient_sub_alt_image_info,
                                                  album_info);
                            }
                            break;

                        case 5:
                            mvpw_get_text_attr(mclient_sub_image_2_3, &attr);
                            if (user_focus == TRUE)
                            {
                                attr.border = MVPW_GREEN;
                            }
                            else
                            {
                                attr.border = MVPW_MIDNIGHTBLUE;
                            }
                            mvpw_set_text_attr(mclient_sub_image_2_3, &attr);

                            mvpw_get_text_attr(mclient_sub_alt_image_2_3,
                                               &attr);
                            if (user_focus == TRUE)
                            {
                                attr.border = MVPW_GREEN;
                            }
                            else
                            {
                                attr.border = MVPW_MIDNIGHTBLUE;
                            }
                            mvpw_set_text_attr(mclient_sub_alt_image_2_3,
                                               &attr);

                            if (user_focus == TRUE)
                            {
                                char album_info[500];
                                sprintf(album_info,
                                        "Album %d of %d\nAlbum:%s\nArtist:%s",
                                        cli_data.
                                        album_index_plus_start_for_cover_art[5]
                                        + 1,
                                        cli_data.album_max_index_for_cover_art,
                                        cli_data.album_name_for_cover_art[5],
                                        cli_data.artist_name_for_cover_art[5]);
                                mvpw_set_text_str(mclient_sub_alt_image_info,
                                                  album_info);
                            }
                            break;

                        case 6:
                        case 7:
                        case 8:
                            mvpw_get_text_attr(mclient_sub_browsebar, &attr);
                            if (user_focus == TRUE)
                            {
                                attr.border = MVPW_GREEN;
                            }
                            else
                            {
                                attr.border = MVPW_MIDNIGHTBLUE;
                            }
                            mvpw_set_text_attr(mclient_sub_browsebar, &attr);
                            break;

                        }
                    }
                }
            }
            // We need to self flag as this state does not
            // produce a response. We usually flag when we
            // process a response.
            cli_data.pending_proc_for_cover_art_widget = TRUE;
            break;

        default:
            cli_data.state_for_cover_art = IDLE_COVERART_WIDGET;
            break;
        }
    }

    if (cli_data.state_for_cover_art_widget != IDLE_COVERART_WIDGET)
    {
        cli_data.state_for_cover_art_widget++;
        if (cli_data.state_for_cover_art_widget >= LAST_STATE_COVERART_WIDGET)
        {
            cli_data.state_for_cover_art_widget = IDLE_COVERART_WIDGET;
        }
    }
}
