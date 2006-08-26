/*
 *  $Id$
 *
 *  Copyright (C) 2006, Joe Carter
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

/* To obtain MAC address. */
#include <net/if.h>
#include <sys/ioctl.h>

#include "mclient.h"

// debug on
//#define debug(...) printf(__VA_ARGS__)
// debug off
#define debug(...)              //printf(__VA_ARGS__)

// fine debug on
//#define debug_fine(...) printf(__VA_ARGS__)
// fine debug off
#define debug_fine(...)         //printf(__VA_ARGS__)

typedef int boolean;

static int debug = 1;

/*
 * Global pointer to alloc'd memory for data received from 
 * the client.
 */
/* background receieve buffer. */
char *recvbuf_back;

/* Address of server */
struct in_addr *cli_server_addr = NULL;

/* Mutex for sending on socket */
pthread_mutex_t mclient_cli_mutex = PTHREAD_MUTEX_INITIALIZER;

/*
 * Need the MAC to uniquely identify this mvpmc box to
 * mclient server.
 */
unsigned char *mac_address_ptr;
struct ifreq ifr;

/*
 * The ID of this player.
 */
char encoded_player_id[32];
char decoded_player_id[32];

/*
 * Tracks states of cli interface.
 */
cli_data_type cli_data;

/*
 * Tracks delays between identical CLI states.
 */
int cli_identical_state_interval_timer = 0;

/*
 * Tracks when user button press requires resetting audio hardware buffer.
 */
int reset_mclient_hardware_buffer;

/*
 * Tracks current state to display on OSD.
 */
int mclient_display_state;

/*
 * Tracks "Next Up" to "Now Playing" transition.
 */
int now_playing_timeout;

/*
 * Converts the mac address into a format recognised as 
 * the slimserver player id.
 */
void
mac_to_encoded_player_id (char *s)
{
    sprintf (s, "%02x%%3A%02x%%3A%02x%%3A%02x%%3A%02x%%3A%02x",
             mac_address_ptr[0],
             mac_address_ptr[1],
             mac_address_ptr[2],
             mac_address_ptr[3], mac_address_ptr[4], mac_address_ptr[5]);
}

/*
 * Converts the mac address into a human readable format
 * this matches the decoded player id format.
 */
void
mac_to_decoded_player_id (char *s)
{
    sprintf (s, "%02x:%02x:%02x:%02x:%02x:%02x",
             mac_address_ptr[0],
             mac_address_ptr[1],
             mac_address_ptr[2],
             mac_address_ptr[3], mac_address_ptr[4], mac_address_ptr[5]);
}

/*
 * Get socket handle for the server's Command Line Interface (CLI)
 */
int
cli_server_connect (void)
{
    struct sockaddr_in my_addr;

    int socket_handle = socket (AF_INET, SOCK_STREAM, 0);

    /*
     * Get the MAC address for the first ethernet port.
     */
    strcpy (ifr.ifr_name, "eth0");
    ioctl (socket_handle, SIOCGIFHWADDR, &ifr);
    mac_address_ptr = (unsigned char *) ifr.ifr_hwaddr.sa_data;

    if (socket_handle == -1)
    {
        debug ("mclient_cli:Could not get descriptor\n");
    }
    else
    {
        debug ("mclient_cli:Was able get descriptor:%d\n", socket_handle);
    }

    /*
     * Translate MAC to char strings suitable for sending to server.
     */
    mac_to_encoded_player_id (encoded_player_id);
    mac_to_decoded_player_id (decoded_player_id);
    debug ("mclient_cli:encoded_player:%s\n", encoded_player_id);
    debug ("mclient_cli:decoded_player:%s\n", decoded_player_id);

    debug ("mclient_cli:Connecting...\n");
    my_addr.sin_family = AF_INET;
    my_addr.sin_port = htons (CLI_PORT);
    my_addr.sin_addr = *server_addr_mclient;
    if (connect (socket_handle, (struct sockaddr *) &my_addr, sizeof (struct sockaddr)))
    {
        debug ("mclient_cli:Unable to connect to descriptor endpoint:%d\n",
               socket_handle);
    }
    else
    {
        debug ("mclient_cli:Was able to connect to descriptor endpoint:%d\n",
               socket_handle);
    }

    return socket_handle;
}

/*
 * Ensures that the values are reset in the structure.
 */
void
cli_reset_cmd (mclient_cmd * response)
{
    int i;
    response->cmd[0] = '\0';
    response->player_id[0] = '\0';
    for (i = 0; i < MAX_PARAMS; i++)
    {
        response->param[i][0] = '\0';
    }
}

/*
 * Replace printable ASCII HEX values found in passed in string with 
 * numerical value.
 */
void
cli_decode_string (char *param)
{
    int i;
    unsigned int the_char;
    int pos = 0;
    char buf[MAX_REPLY_LENGTH];
    char hex_string[3];

    if (param != NULL)
    {
        if (strlen (param) > MAX_REPLY_LENGTH)
        {
            debug ("mclient_cli:Parameter too long. Cannot cope. Ignoring command %s\n",
                   param);
        }
        else
        {
            for (i = 0; i < strlen (param); i++)
            {
                if (param[i] != '%')
                {
                    buf[pos++] = param[i];
                }
                else if ((i + 2) >= strlen (param))
                {
                    debug ("mclient_cli:Unexpected percent sign in param %s pos %d",
                           param, i);
                }
                else
                {
                    strncpy (hex_string, &param[i + 1], 2);
                    hex_string[2] = '\0';
                    debug_fine ("mclient_cli:decoding hex %s\n", hex_string);
                    sscanf (hex_string, "%x", &the_char);
                    debug_fine ("mclient_cli:value %i", the_char);
                    buf[pos++] = the_char;
                    // skip two more chars
                    i += 2;
                }
            }
            buf[pos] = '\0';
            strcpy (param, buf);
        }
    }
}

/*
 * Split the parameters of each individual response into an array of
 * character strings.
 */
void
cli_parse_parameters (mclient_cmd * response, char **token_buffer)
{
    char *param;
    int i;
    int length;

    for (i = 0; i < MAX_PARAMS; i++)
    {
        param = strtok_r (NULL, " ", token_buffer);
        cli_decode_string (param);
        if (param == NULL)
        {
            break;
        }
        else
        {
            strncpy (response->param[i], param, MAX_PARAM_SIZE);

            length = strlen (response->param[i]);
            if ((length > 0) && (response->param[i][length - 1] == '\n'))
            {
                response->param[i][length - 1] = '\0';
            }

            debug ("mclient_cli:param %d: |%s|\n", i, response->param[i]);
        }
    }
}

/*
 * Extract tokens (space separated strings) from passed in string.
 */
void
cli_decode_response (int socket_handle, char *buf, mclient_cmd * response)
{
    char *cmd = NULL;
    char *player_id;
    char token_buffer_overall[strlen (buf)];

    char token_buffer[strlen (recvbuf_back)];

    char *param;
    int i;

    cli_reset_cmd (response);

    i = 0;
    param = strtok_r (buf, "\n", (char **) &token_buffer_overall);
    while (param != NULL)
    {
        i++;
        debug ("mclient:line %d:|%s|\n", i, param);

        /*
         * Extract player ID from returned message.
         */
        player_id = strtok_r (param, " ", (char **) &token_buffer);
        cli_decode_string (player_id);
        strncpy (response->player_id, player_id, MAX_ID_SIZE);
        debug ("mplayer_cli:Player ID found in response: |%s|\n", player_id);

        /*
         * Extract CMD from returned message.
         */
        cmd = strtok_r (NULL, " ", (char **) &token_buffer);
        strncpy (response->cmd, cmd, MAX_CMD_SIZE);
        debug ("mplayer_cli:CMD found in response: |%s|\n", cmd);

        /*
         * Extract all parameters from returned message.
         * Returns 0 for 1 response, 1 for 2 or more responses.
         */
        cli_parse_parameters (response, (char **) &token_buffer);

        /*
         * Process if player ID matches.
         */
        if (strncmp (decoded_player_id, player_id, strlen (player_id)) == 0)
        {
            cli_parse_response (socket_handle, response);
        }

        param = strtok_r (NULL, "\n", (char **) &token_buffer_overall);
    }
}

void
cli_parse_response (int socket_handle_cli, mclient_cmd * response)
{
    if (response->cmd != NULL)
    {
        if (strncmp ("player", response->cmd, strlen ("player")) == 0)
        {
            cli_parse_player (response);
        }
        else if (strncmp ("playlist", response->cmd, strlen ("playlist")) == 0)
        {
            debug ("mclient_cli:Found playlist in response.\n");
            cli_parse_playlist (response);
            cli_update_playlist (socket_handle_cli);
        }
        else if (strncmp ("stop", response->cmd, strlen ("stop")) == 0)
        {
            debug ("mclient:cli_parse_response: Found stop.\n");
            mclient_display_state = STOP;
            reset_mclient_hardware_buffer = 1;
        }
        else if (strncmp ("status", response->cmd, strlen ("status")) == 0)
        {
            debug ("mclient:cli_parse_response: Found status.\n");
        }
        else if (strncmp ("newsong", response->cmd, strlen ("newsong")) == 0)
        {
            debug ("mclient:cli_parse_response: Found newsong.\n");
            /*
             * Found "newsong" message, the server is announcing it is
             * going to a new song - update the play list.
             * Set to fist state (always MINMUNIS1 plus 1).
             */
            cli_data.state = UPDATE_PLAYLIST_MINMINUS1 + 1;
            cli_update_playlist (socket_handle_cli);

            /*
             * Set up for about 5 second "Next Up" to "Now Playing" transition.
             */
            now_playing_timeout = time (NULL) + 5;
        }
        else if (strncmp ("open", response->cmd, strlen ("open")) == 0)
        {
            debug ("mclient:cli_parse_response: Found open.\n");
        }
        else if (strncmp ("button", response->cmd, strlen ("button")) == 0)
        {
            debug ("mclient:cli_parse_response: Found button.\n");
            /*
             * Found "button" message, someone is pressing remote control
             * buttons - let's try and grab the button action.
             */
            cli_parse_button (response);
            /*
             * Also, let's update the play list, but maybe we will take this
             * out later.
             * Set to fist state (always MINMUNIS1 plus 1).
             */
            cli_data.state = UPDATE_PLAYLIST_MINMINUS1 + 1;
            cli_update_playlist (socket_handle_cli);
        }
        else if (strncmp ("listen", response->cmd, strlen ("listen")) == 0)
        {
            debug ("mclient:cli_parse_response: Found listen.\n");
            /*
             * Found "listen" message, mclient is initializing the
             * server - let's try and grab the play list.
             * Set to fist state (always MINMUNIS1 plus 1).
             */
            cli_data.state = UPDATE_PLAYLIST_MINMINUS1 + 1;
            cli_update_playlist (socket_handle_cli);
        }
        else if (strncmp ("play", response->cmd, strlen ("play")) == 0)
        {
            debug ("mclient:cli_parse_response: Found play.\n");
            mclient_display_state = PLAY;
            reset_mclient_hardware_buffer = 1;
        }
        else if (strncmp ("display", response->cmd, strlen ("display")) == 0)
        {
            debug ("mclient:cli_parse_response: Found display.\n");
            cli_parse_display (response);
            cli_identical_state_interval_timer = time (NULL) + 10;
        }
        else
        {
            debug ("mclient:cli_parse_response: Command |%s| not handled yet.\n",
                   response->cmd);
        }
    }
}

/*
 * Parse response to "playlist" command.
 */
void
cli_parse_playlist (mclient_cmd * response)
{
    char string[200];

    /*
     * If param 0 is "tracks" this is response to "playlist tracks ?"
     * If param 0 is "artist" this is response to "playlist artist n ?"
     * If param 0 is "title"  this is response to "playlist title n ?"
     */
    if (strncmp ("tracks", response->param[0], strlen ("tracks")) == 0)
    {
        debug ("mclient_cli:Found track in response.\n");

        /*
         * Place number of tracks in appropriate positions on full OSD.
         */
        cli_data.tracks = atoi (response->param[1]);

        /*
         * If there is only 1 track, let's assume this is a streamming radio 
         * station and follow a set of steps designed for handling radio station
         * information.
         * It sounds like slimserver 6.5 might include support for identifying
         * the source and streaming song name.
         */
        if (cli_data.tracks == 1)
        {
            /*
             * Check if this is the first time through radio code.
             */
            if (cli_fullscreen_widget_state != STREAMING_RADIO)
            {
                int i;

                /*
                 * Force a fullscreen update.
                 * Actually, print out what's in the history buffer onto
                 * the full screen widget.
                 */
                for (i = CLI_MAX_TRACKS; i >= 0; i--)
                {
                    mvpw_menu_change_item (mclient_fullscreen, (void *) (i + 3),
                                           cli_data.title_history[i + 1]);
                    debug ("mclient_cli:Printing history:%d.\n", i);
                }
                mvpw_menu_change_item (mclient_fullscreen, (void *) (2),
                                       "-waiting for data-");

                cli_fullscreen_widget_state = STREAMING_RADIO;
            }
            /*
             * Detected streaming radio, change to next radio state.
             */
            cli_data.state = UPDATE_RADIO_MINMINUS1 + 1;
            /*
             * Turn on attempts to recover radio data on a periodic bases.
             */
            cli_identical_state_interval_timer = (time (NULL) + 10);
        }
        /*
         * If there are more than 1 track, let's assume this is an album and
         * follow a set of steps designed for handling album information.
         */
        else
        {
            /*
             * Check if this is the first time through paly list code.
             */
            if (cli_fullscreen_widget_state != PLAY_LISTS)
            {
                /*
                 * Force a fullscreen update.
                 */
                /*
                 * Doesn't appear tracking this state is necessary, but we'll keep it
                 * around just in case.
                 */
                cli_fullscreen_widget_state = PLAY_LISTS;
            }
            /*
             * Turn off attempts to recover radio data on a periodic bases.
             */
            cli_identical_state_interval_timer = 0;
            sprintf (string, "Author: %s   Playing track %d of %d", cli_data.artist,
                     (cli_data.index_playing + 1), cli_data.tracks);
            mvpw_menu_change_item (mclient_fullscreen, (void *) (1), string);

            /*
             * Change to next state
             */
            cli_data.state++;
        }
    }
    else if (strncmp ("artist", response->param[0], strlen ("artist")) == 0)
    {
        debug ("mclient_cli:Found artist in response.\n");
        /*
         * Place artist in appropriate positions on full OSD.
         */
        sprintf (cli_data.artist, "%s", response->param[2]);
        switch (mclient_display_state)
        {
        case STOP:
            sprintf (string, "Author: %s   Stopped on %d of %d", cli_data.artist,
                     (cli_data.index_playing + 1), cli_data.tracks);
            break;
        case PLAY:
            sprintf (string, "Author: %s   Playing track %d of %d", cli_data.artist,
                     (cli_data.index_playing + 1), cli_data.tracks);
            break;
        case PAUSE:
            sprintf (string, "Author: %s   Paused on %d of %d", cli_data.artist,
                     (cli_data.index_playing + 1), cli_data.tracks);
            break;
        case STREAMING:
            sprintf (string, "Author: %s   Streaming %d of %d", cli_data.artist,
                     (cli_data.index_playing + 1), cli_data.tracks);
            break;
        }
        mvpw_menu_change_item (mclient_fullscreen, (void *) (1), string);

        /*
         * Change to next state
         */
        cli_data.state++;
    }
    else if (strncmp ("title", response->param[0], strlen ("title")) == 0)
    {
        debug ("mclient_cli:Found title in response.\n");
        /*
         * If there is only 1 track, let's assume this is a streamming radio 
         * station and follow a set of steps designed for handling radio station
         * information.
         * It sounds like slimserver 6.5 might include support for identifying
         * the source and streaming song name.
         */
        if (cli_data.tracks == 1)
        {
            /*
             * As this is looking like a radio station, the title is acturally
             * the name of the streaming source.  Place it appropriately on the
             * OSD.
             */
            sprintf (string, "Streaming:   %s", response->param[2]);
            mvpw_menu_change_item (mclient_fullscreen, (void *) (1), string);

            /*
             * Change to next state
             */
            cli_data.state++;
        }
        else
        {
            /*
             * Place titles in appropriate positions on full OSD.
             */
            if (cli_data.index_info == cli_data.index_playing)
            {

                if (now_playing_timeout > time (NULL))
                {
                    sprintf (string, "%d) %s <- Up Next", (cli_data.index_info + 1),
                             response->param[2]);
                }
                else
                {
                    sprintf (string, "%d) %s <- Now Playing", (cli_data.index_info + 1),
                             response->param[2]);
                }
                mvpw_menu_change_item (mclient_fullscreen,
                                       (void *) (cli_data.index_line + 2), string);
/// ### doesn't work?    mvpw_menu_hilite_item (mclient_fullscreen, (void *) (cli_data.index_info + 2));
            }
            else
            {
                sprintf (string, "%d) %s", (cli_data.index_info + 1), response->param[2]);
                mvpw_menu_change_item (mclient_fullscreen,
                                       (void *) (cli_data.index_line + 2), string);
            }

            /*
             * If we still have room & haven't listed all titles, get another title.
             */
            cli_data.index_info++;
            cli_data.index_line++;
            if ((cli_data.index_line > CLI_MAX_TRACKS)
                || (cli_data.index_info >= cli_data.tracks))
            {
                /*
                 * Blank out empty lines.
                 */
                while (cli_data.index_line <= CLI_MAX_TRACKS)
                {
                    string[0] = '\0';
                    mvpw_menu_change_item (mclient_fullscreen,
                                           (void *) (cli_data.index_line + 2), string);
                    cli_data.index_line++;
                }
                cli_pick_starting_index ();
                cli_data.state++;
            }
        }
    }
    else if (strncmp ("index", response->param[0], strlen ("index")) == 0)
    {
        debug ("mclient_cli:Found index in response.\n");
        cli_data.index_playing = atoi (response->param[1]);
        cli_data.state++;
    }
    else if (strncmp ("jump", response->param[0], strlen ("jump")) == 0)
    {
        debug ("mclient:cli_parse_playlist: Found jump.\n");
        mclient_display_state = PLAY;
        reset_mclient_hardware_buffer = 1;
    }
}


/*
 * Parse response to "dispaly" command.
 */
void
cli_parse_display (mclient_cmd * response)
{
    char string[200];
    int i;

    /*
     * After slimserver 6.5 is widly availble, we should use the autonomous
     * listen command to find out what is streaming.  Not this code that handles
     * the return from the display command.
     *
     * We want what is streaming / playing, or the 2nd line of the display.
     * But we only want it if the fist line starts with "Now playing".
     */
    if (strncmp ("Now playing", response->param[0], strlen ("Now playing")) == 0)
    {
        sprintf (string, "%s <- Now Playing", response->param[1]);
        mvpw_menu_change_item (mclient_fullscreen, (void *) (2), string);

        /*
         * Save this information in the palying history and display
         * history.
         */
        debug ("mclient_cli:Length of response->param[1]:%d\n",
               strlen (response->param[1]));

        if (strncmp (response->param[1], cli_data.title_history[0], 49) != 0)
        {
            debug
                ("mclient_cli:Top of history does NOT match current title cur:%s his:%s.\n",
                 response->param[1], cli_data.title_history[0]);

            for (i = (CLI_MAX_TRACKS - 1); i >= 0; i--)
            {
                strncpy (cli_data.title_history[i + 1], cli_data.title_history[i], 49);
                mvpw_menu_change_item (mclient_fullscreen, (void *) (i + 3),
                                       cli_data.title_history[i + 1]);
                debug ("mclient_cli:Moving history:%d.\n", i);
            }

            if (strlen (response->param[1]) >= 49)
            {
                strncpy (cli_data.title_history[0], response->param[1], 49);
                cli_data.title_history[0][49] = '\0';
            }
            else
            {
                strcpy (cli_data.title_history[0], response->param[1]);
            }
        }
        else
        {
            debug
                ("mclient_cli:Top of history MATCHES current title cur:%s his:%s.\n",
                 response->param[1], cli_data.title_history[0]);

            for (i = CLI_MAX_TRACKS; i > 0; i--)
            {
                mvpw_menu_change_item (mclient_fullscreen, (void *) (i + 3),
                                       cli_data.title_history[i + 1]);
                debug ("mclient_cli:Printing history:%d.\n", i);
            }
        }
    }
    else
    {
        debug
            ("mclient_cli:TEST:Inside of display parse, but didn't match Now playing, STATE:%d\n",
             cli_data.state);
    }
}

/*
 * Parse response to "button" command.
 */
void
cli_parse_button (mclient_cmd * response)
{
    /*
     * We want to record the button pressed.
     */
    debug ("mclient:cli_parse_button:%s <- cmd \n", response->cmd);
    debug ("mclient:cli_parse_button:%s <- param 0\n", response->param[0]);
    debug ("mclient:cli_parse_button:%s <- param 1\n", response->param[1]);

    if (strcmp (response->param[0], "play") == 0)
    {
        mclient_display_state = PLAY;
        reset_mclient_hardware_buffer = 1;
    }

    if (strcmp (response->param[0], "stop") == 0)
    {
        mclient_display_state = STOP;
        reset_mclient_hardware_buffer = 1;
    }

    if (strcmp (response->param[0], "pause") == 0)
    {
        mclient_display_state = PAUSE;
    }
}



/*
 * Parse response to "player" command.
 * (Not needed as of now.)
 */
void
cli_parse_player (mclient_cmd * cmd)
{
    int count = 0;

    if ((cmd != NULL) && (cmd->param[0] != NULL))
    {
        if (strcmp ("count", cmd->param[0]) == 0)
        {
            count = atoi (cmd->param[1]);
            debug ("number of players: %d\n", count);
        }
        else if (strcmp ("id", cmd->param[0]) == 0)
        {
            debug ("id is %s\n", cmd->param[1]);
        }
        else
        {
            debug ("player command %s is not handled yet\n", cmd->cmd);
        }
    }
}

/*
 * Send command for updating play list to CLI depending 
 * on state flag.
 */
void
cli_update_playlist (int socket_handle_cli)
{
    char cmd[256];

    /*
     * Check if the state machine is within bounds.
     * The state machine will execute in the order of the enumerated
     * defins in mclient.h.
     */
    if (cli_data.state == UPDATE_PLAYLIST_MAXPLUS1)
    {
        /*
         * Start "update playlist" state machine over & don't send out another cmd.
         */
        cli_data.state = UPDATE_PLAYLIST_MINMINUS1 + 1;
    }
    else
    {
        switch (cli_data.state)
        {
        case UPDATE_PLAYLIST_NUM_TRACKS:
            sprintf (cmd, "%s playlist tracks ?\n", encoded_player_id);
            cli_send_packet (socket_handle_cli, cmd);
            cli_pick_starting_index ();
            break;

        case UPDATE_PLAYLIST_ARTIST:
            sprintf (cmd, "%s playlist artist ?\n", encoded_player_id);
            cli_send_packet (socket_handle_cli, cmd);
            cli_pick_starting_index ();
            break;

        case UPDATE_PLAYLIST_TITLE:
            sprintf (cmd, "%s playlist title %d ?\n", encoded_player_id,
                     cli_data.index_info);
            cli_send_packet (socket_handle_cli, cmd);
            break;

        case UPDATE_PLAYLIST_INDEX:
            sprintf (cmd, "%s playlist index ?\n", encoded_player_id);
            cli_send_packet (socket_handle_cli, cmd);
            cli_pick_starting_index ();
            break;

        case UPDATE_RADIO_STATION:
            sprintf (cmd, "%s playlist title ?\n", encoded_player_id);
            cli_send_packet (socket_handle_cli, cmd);
            cli_pick_starting_index ();
            break;

        case UPDATE_RADIO_NOWPLAYING:
            sprintf (cmd, "%s display ? ?\n", encoded_player_id);
            cli_send_packet (socket_handle_cli, cmd);
            cli_pick_starting_index ();
            break;
        }
    }
}

/*
 * Pick a starting index for lists of titles which exceed the 
 * size of the full screen display.
 */
void
cli_pick_starting_index (void)
{
    /*
     * Info tracks num of title and line tracks num of next line 
     * to print to on screen.
     */
    cli_data.index_info = 0;
    cli_data.index_line = 0;
    /*
     * If we are half way down the screen.
     */
    if (cli_data.index_playing > (CLI_MAX_TRACKS / 2))
    {
        /*
         * And there are more titles than can fit on a screen,
         * advance to a new starting point.
         */
        if (cli_data.tracks >= (CLI_MAX_TRACKS - 1))
        {
            cli_data.index_info = (cli_data.index_playing - (CLI_MAX_TRACKS / 2));
            /*
             * But not more then a screen full from the end of the list of
             * titles.
             */
            if ((cli_data.tracks - cli_data.index_info) <= CLI_MAX_TRACKS)
            {
                cli_data.index_info = cli_data.tracks - (CLI_MAX_TRACKS + 1);
            }
        }
    }
}


/*
 * Top CLI function called from mclient's main loop.
 */
void
cli_read_data (int socket_handle)
{
    int cli_bytes_read = 1;
    mclient_cmd response;

    cli_bytes_read = cli_read_message (socket_handle, recvbuf_back);

    /*
     * If there was a reading error, skip decoding response as
     * it tends to crash this client.
     */
    if (cli_bytes_read > 0)
    {
        debug ("mclient:cli_read_data: Read the CLI and found this:\n|>>>|%s|<<<|\n",
               recvbuf_back);
        /*
         * Decode and prase the response.
         */
        cli_decode_response (socket_handle, recvbuf_back, &response);
    }

    /*
     * If the data/control port has switched us into the stop state consider resetting
     * hardware audio buffer...
     * But only reset the buffer when the user is stopping or skipping around
     * with either the remote or web interface.  Use the CLI from the server to
     * detect these events.  As we don't know which stop indication will come first
     * (the server's data/control or CLI port) have similar code in the data/conrol processing
     * functions (we are in the CLI processing functions).
     */
    if (outbuf->playmode == 3)
    {
        if (reset_mclient_hardware_buffer == 1)
        {
            printf
                ("mclient_cli:cli_read_data: Resetting HW audio buffer from CLI functions.\n");
            av_reset ();
            reset_mclient_hardware_buffer = 0;
        }
    }
}


/*
 * Read CLI data from server.
 */
int
cli_read_message (int socket_handle, char *buffer)
{
    int cli_bytes_read = 0;
    /*
     * Using the recv function:
     * ssize_t recv(int s, void *buf, size_t len, int flags);
     */
    cli_bytes_read = recv (socket_handle, buffer, RECV_BUF_SIZE_CLI, 0);
    if (cli_bytes_read < 0)
    {
        if (errno != EINTR)
        {
            debug ("mclient_cli:recv from errno %i\n", errno);
        }
    }
    else if (cli_bytes_read < 1)
    {
        debug ("mclient_cli:Peer closed connection.\n");
    }
    else if (cli_bytes_read > 0)
    {
        buffer[cli_bytes_read] = 0;
    }

    debug ("mclient_cli:Got msg (%d bytes): %s\n", cli_bytes_read, buffer);
    buffer[cli_bytes_read] = 0;
    return cli_bytes_read;
}

void
cli_send_packet (int socket_handle, char *b)
{
    int bytes;
    int l;
    debug ("mclient_cli:Sending msg %s\n", b);
    l = strlen (b);
    /* 
     * Protect from multiple sends.
     */
///    pthread_mutex_lock (&mclient_cli_mutex);
    debug ("mclient_cli:Handle:%d Bytes:%d Message:%s.\n", socket_handle, l, b);
    bytes = send (socket_handle, b, l, 0);
    if (debug)
        debug ("mclient_cli: %i bytes sent\n", bytes);
    if (bytes < 0)
    {
        debug ("mclient_cli:error %i\n", errno);
    }
///    pthread_mutex_unlock (&mclient_cli_mutex);
}

void
cli_init (void)
{
    debug ("mclient_cli:Entering mclinet_cli init.\n");
    /*
     * Create the buffer to store data from the server.
     */
    recvbuf_back = (void *) calloc (1, RECV_BUF_SIZE_CLI);
}

void
cli_send_discovery (int socket_handle_cli)
{
    char cmd[256];
    /*
     * Configure music server to autonomously send updates.
     */
    sprintf (cmd, "listen 1\n");
    cli_send_packet (socket_handle_cli, cmd);
}
