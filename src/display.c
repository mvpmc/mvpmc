/*
 *  Copyright (C) 2005-2006, Rick Stuart
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

#include "display.h"

#include <mvp_widget.h>

static int client_socket_descriptor;

char display_message[DISPLAY_MESG_SIZE];
static char display_server_message[DISPLAY_MESG_SIZE];

int display_type;

static FILE *out_to_display;

static FILE *child_display_file_ptr;
static unsigned int fromlen;
static int socket_descriptor, new_socket_descriptor;
static struct sockaddr_in sain, from_sain, client_sain;

static struct utsname myname;

static char accum_message[DISPLAY_MESG_SIZE];
static char last_message[DISPLAY_MESG_SIZE];

static char *accum_message_ptr;

struct display_struct_sub_type {
  char String[DISPLAY_SUBMESG_SIZE];
  int Ready;
  int Scroll;
  int Event;
};

struct display_struct_type {
  struct display_struct_sub_type Title;
  struct display_struct_sub_type Artist;
  struct display_struct_sub_type Album;
  struct display_struct_sub_type Year;
  struct display_struct_sub_type Genre;
  struct display_struct_sub_type Time;
  struct display_struct_sub_type rTime;
  struct display_struct_sub_type Progress;
  struct display_struct_sub_type File;
  struct display_struct_sub_type pFile;
  struct display_struct_sub_type nFile;
  struct display_struct_sub_type Message;
  struct display_struct_sub_type Line1;
  struct display_struct_sub_type Line2;
  int Dimmer_Event;
  int display_busy;
  unsigned int long Interval;
} display_struct;

static pthread_t display_thread_handle;

static int rc;
static int this_trys_errno;


/******************************
 * Initialize the display and 
 * thread.
 ******************************/
int display_init(void)
{
  /*
   * Initialize how fast display thread will run.
   */
  display_struct.Interval = DISPLAY_NORMAL_INTERVAL;

  /*
   * Start display thread
   */
  pthread_create(&display_thread_handle, &thread_attr_small, display_thread, NULL);

  /*
   * Send opening test message.
   */
  snprintf(display_message, DISPLAY_MESG_SIZE, "Message:MVPMC MediaMVP /  www.mvpmc.org\n");

  display_send(display_message);

  /*
   * For now, always return a good indicatoin.
   */
  return(0);
}


/******************************
 * Get elapsed program time.
 ******************************/
void
display_timecode(mvp_widget_t *widget)
{
  demux_attr_t *attr;
  av_stc_t stc;

  attr = demux_get_attr(handle);
  av_current_stc(&stc);
  snprintf(display_struct.Time.String, sizeof(display_struct.Time.String), "%.2d:%.2d:%.2d",
	   stc.hour, stc.minute, stc.second);
  /*
   * Tell main display function new time data
   * is available.
   */
  display_struct.Time.Ready = 1;

  /*
   * Hide the time widget.
   */
  mvpw_hide(widget);
}

/******************************
 * Get real time clock.
 ******************************/
void
display_clock(mvp_widget_t *widget)
{
  time_t t;
  struct tm *tm;

  t = time(NULL);
  tm = localtime(&t);

  sprintf(display_struct.rTime.String, "%.2d:%.2d:%.2d", tm->tm_hour, tm->tm_min, tm->tm_sec);

#if 0
  printf("4444 %.2d:%.2d:%.2d", tm->tm_hour, tm->tm_min, tm->tm_sec);
#endif

  /*
   * Tell main display function new time data
   * is available.
   */
  display_struct.rTime.Ready = 1;
}
/******************************
 * Get program progress.
 ******************************/
void
display_progress(mvp_widget_t *widget)
{
  long long offset, size;
  int off;
  static int last_off;

  /*
   * Hide the offset widget.
   */
  mvpw_hide(widget);

  size=video_functions->size();

  offset = video_functions->seek(0, SEEK_CUR);
  off = (int)((double)(offset/1000) /
	      (double)(size/1000) * 100.0);

  sprintf(display_struct.Progress.String, "Progress:%d%%", off);

  /*
   * Tell main display function new time data
   * is available.
   *
   * As the progress informaion is continuously being updated,
   * don't tell the main display function to print out this
   * data unless the progress message has changed.
   */
  if(off != last_off)
    {
      display_struct.Progress.Ready = 1;
      last_off = off;
    }
}

/******************************
 * Common send routine
 ******************************/
void
display_send(char *msg)
{
  struct hostent *hptr;

  if (display_type == DISPLAY_DISABLE)
    return;

  /*
   * Get a socket to work with.  This socket will
   * be in the UNIX domain, and will be a
   * stream socket.
   */
  if((client_socket_descriptor = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
//###      perror("client: socket");
//###      exit(1);
      printf("Problem:display:Unable to get client socket descriptor.\n");
      return;
    }

  /*
   * Determine the IP address of this node.
   */
  if(uname(&myname) < 0)
    {
//###      perror("uname: myname");
//###      exit(1);
      printf("Problem:display:Unable to find my own name.\n");
      return;
    }

  /*
   * Get host information.
   */
  if((hptr = gethostbyname( myname.nodename )) == NULL)
    {
//###      perror("gethostbyname error");
//###      exit(1);
      printf("Problem:display:Unable to get host info.\n");
      return;
    }

  /*
   * Now get the IP address for this node.
   *
   * hptr points to the hostent structure returned by gethostby....
   * member h_addr_list is a list of pointers to in_addr structs.
   * the list is terminated by a NULL.
   * there may be more than one pointer in this list, but 
   * usually a node only has one IP address. so we cheat and assume
   * that the first pointer in h_addr_list is the primary and only
   * in_addr for this node. copy the in_addr information to the caller's
   * storage and return.
   */

  memcpy(&client_sain.sin_addr.s_addr, *hptr->h_addr_list, sizeof( struct in_addr));

  /*
   * Create the address we will be connecting to.
   */
  client_sain.sin_family = AF_INET;
  client_sain.sin_port = htons(8005);

  if(connect(client_socket_descriptor, (struct sockaddr *)&client_sain, sizeof(client_sain)) < 0)
    {
//###      perror("client: connect");
//###      exit(1);
      printf("Problem:display:Unable to connect to the display process.\n");
      return;
    }

  send(client_socket_descriptor, msg, strlen(msg), 0);

  close(client_socket_descriptor);
}


/******************************
 * Server socket code.
 ******************************/
void* display_thread(void *arg)
{
  struct hostent *hptr;

  int char_pos;

  /*
   * Setup On Screen Display feature to
   * update elapsed time for our display.
   *
   * Some of these callbacks are buggy.
   *
   * OSD_TIMECODE:
   * Starts counting as soon as the call back is set up.  I think code in fd.c resets it
   * as soon as a selection starts playing.  However, the timer continues long after the
   * selection is done playing.
   *
   * OSD_PROGRESS:
   * Appears to work as needed.  Howver, leaves a blank box on OSD duing the first few seconds 
   * of initialization.
   *
   * OSD_CLOCK:
   * Haven't gotten around to trying this one out.
   */
  //  set_osd_callback(OSD_TIMECODE, display_timecode);
  //  set_osd_callback(OSD_PROGRESS, display_progress);
  //  set_osd_callback(OSD_CLOCK, display_clock);

  /*
   * Open serial port to display.
   */
  out_to_display=fopen("/dev/ttyS0","r+");

  if(!out_to_display)
    { 
      fprintf(stderr, "Unable to open /dev/tty\n");
      exit(1);
    }

  /*
   * Get a socket to work with.  This socket will
   * be a TCP socket.
   */
  if((socket_descriptor = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
      perror("server: socket cmd failed");
      exit(1);
    }

  /*
   * Get the particulars on the socket and chang it from the default 
   * blocking to non-blocking.
   */
  rc = fcntl( socket_descriptor, F_GETFL );
  if ( rc >= 0 )
    {
      rc = fcntl( socket_descriptor, F_SETFL, rc | O_NONBLOCK );
      if ( rc < 0 )
	{
	  perror("server: couldn't set socket opens" );
	  exit(1);
	}
    }
  else
    {
      perror("server: couldn't get socket options");
      exit(1);
    }

  /*
   * Determine the IP address of this node.
   */
  if(uname(&myname) < 0)
    {
      perror("server: uname cmd failed");
      exit(1);
    }

  /*
   * Get host information.
   */
  if((hptr = gethostbyname(myname.nodename )) == NULL)
    {
      perror("server: gethostbyname cmd failed");
      exit(1);
    }

  memcpy(&sain.sin_addr.s_addr, *hptr->h_addr_list, sizeof( struct in_addr));

  /*
   * Populate the rest of struct sain.
   */
  sain.sin_family = AF_INET;
  sain.sin_port = htons(8005);

  /*
   * Try to bind the address to the socket.
   *
   * The third argument indicates the "length" of
   * the structure, not just the length of the
   * socket name.
   */
  if(bind(socket_descriptor, (struct sockaddr *)&sain, sizeof(sain)) < 0)
    {
      perror("server: bind cmd failed");
      exit(1);
    }

  /*
   * Listen on the socket.
   */
  if(listen(socket_descriptor, 5) < 0)
    {
      perror("server: listen cmd failed");
      exit(1);
    }

  /*
   * Start of infinite loop to repetitively open the socket
   * for each client which tries to connect.  We are only
   * handling 1 client at a time.
   */
  for(;;)
    {
      /*
       * Accept connections.  When we accept one, new_socket_descriptor
       * will be connected to the client.  from_sain will
       * contain the address of the client.
       */
      fromlen = sizeof(from_sain);

      /*
       * Assume new_socket_desciptor will be positive number if accept 
       * works. In which case break out of while loop and process new
       * information from client.
       */
      new_socket_descriptor = -1;
      while(new_socket_descriptor < 0)
	{
	  if((new_socket_descriptor = accept(socket_descriptor, (struct sockaddr *)&from_sain, &fromlen)) < 0)
	    {
	      this_trys_errno = errno;
	      if(this_trys_errno == EAGAIN)
		{
		  /*
		   * If we are here, there is no connection waiting to be
		   * accepted. Decrement any event timers and take care of 
		   * the dispaly then wait a bit.
		   */

		  /*
		   * Take care of timed events.
		   */
		  if(display_struct.Dimmer_Event > 0)
		    {
		      display_struct.Dimmer_Event--;
		    }
		  else
		    {
		      display_struct.Dimmer_Event = 0;
		    }

		  /*
		   * Update the display by first processing the new received data. How 
		   * we do this probably depends on the display.
		   *
		   * Call fuction to manage selected display.
		   */
		  switch(display_type)
		    {
		    case DISPLAY_IEE40X2:
		      display_iee_40x2();
		      break;
		    case DISPLAY_IEE16X1:
		      display_iee_16x1();
		      break;
		    }
		  /*
		   * Send formatted message to display.
		   */
		  for(char_pos = 0; (char_pos < strlen(display_server_message)) && (char_pos < DISPLAY_MESG_SIZE); char_pos++)
		    {
		      /*
		       * Get rid of all line feeds
		       */
		      if(
			 (display_server_message[char_pos] == 0x0a) ||
			 (display_server_message[char_pos] == 0x0b)
			 )
			{
			  /*
			   * Don't print out carrage returns (0x0a) or line feeds (0x0b), skip to next
			   * character.
			   */

			  /*
			   * Actually, this messes up the output as '\n' are counted
			   * as one of the 16 printable characters.  Not printing anything 
			   * might leave garbage on the display.  So, print a space char.
			   */
			  display_server_message[char_pos] = 0x20;
			}

		      /*
		       * If this is at least the 2nd char pos and the prev was a 
		       * position cmd (DISPLAY_IEE_POSITION), sub val 1 from
		       * this char pos to get real pos info. Explination is in
		       * display.h file.
		       */
		      if(char_pos > 0)
			{
			  if(display_server_message[char_pos-1] == 0x1b)
			    {
			      fputc((display_server_message[char_pos] - 1), out_to_display);
			    }
			  else
			    {
			      fputc(display_server_message[char_pos], out_to_display);
			    }
			}
		      fflush(out_to_display);

		      /*
		       * If we just sent a control character, wait
		       * for a moment. This is a problem with the popular 
		       * IEE VFD displays.
		       */
		      if(display_server_message[char_pos] < ' ')
			{
			  /*
			   * The usleep cmd is in ms but one OS tick is about 
			   * 10ms.
			   */
			  usleep(1);
			}
		    }

#if 0
		  /*
		   * Debugging code to print out printable chars and octal of unprintable chars
		   * that are to be sent to display.
		   */
		  if(strlen(display_server_message) > 0)
		  {
		    int i;
		    printf(">>>1234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890\n");
		    printf("\n>>>");
		    for(i=0;i<strlen(display_server_message);i++)
		      {
			if(display_server_message[i] < 0x20)
			  {
			    printf(":%2.2x:",display_server_message[i]);
			  }
			else
			  {
			    printf("%c",display_server_message[i]);
			  }
		      }
		    printf("<<<\n");
		  }
#endif

		  /*
		   * Once we display the message clear it.  That should
		   * prevent us from re-entering the above display loop.
		   */
		  display_server_message[0] = '\0';

		  /*
		   * Wait a while here.  The desired effect
		   * (length) has been pre-programmed.
		   */
		  struct timespec req, rem;
		  req.tv_sec = 0;
		  req.tv_nsec = display_struct.Interval;
		  (void) nanosleep(&req, &rem);
		}
	      else
		{
		  perror("server: accept cmd failed");
		  printf( "errno = %d\n", this_trys_errno );
		  exit(1);
		}
	    }
	}

      /*
       * The accept cmd has found a pending client message
       * on the socket.  Process the message.
       *
       * We'll use stdio for reading from socket.
       */
      child_display_file_ptr = fdopen(new_socket_descriptor, "r");

      accum_message[0] = '\0';

      display_struct.Title.Ready = 0;
      display_struct.Artist.Ready = 0;
      display_struct.Album.Ready = 0;
      display_struct.Time.Ready = 0;
      display_struct.Year.Ready = 0;
      display_struct.Genre.Ready = 0;
      display_struct.Time.Ready = 0;
      display_struct.rTime.Ready = 0;
      display_struct.Progress.Ready = 0;
      display_struct.File.Ready = 0;
      display_struct.Line1.Ready = 0;
      display_struct.Line2.Ready = 0;
      //      display_struct.pFile.Ready = 0; //Need history
      //      display_struct.nFile.Ready = 0; //Need history
      display_struct.Message.Ready = 0;

      /*
       * Read socket.  "fgets" should get 1 line at a time which
       * ends with a new line.  We will assume the received message 
       * will always look like: "<desciption>:<data>\n".
       */

      while(fgets(accum_message, DISPLAY_MESG_SIZE, child_display_file_ptr) != NULL)
	{
	  /*
	   * Pars the received values and store them away:
	   */
	  if((accum_message_ptr = strstr(accum_message, "Title:")) != 0)	
	    {
	      strcpy(display_struct.Title.String, (strstr(accum_message_ptr,":") + 1));
	      display_struct.Title.Ready = 1;
	      //	      printf("Found Title: %s\n", display_struct.Title.String);
	    }

	  if((accum_message_ptr = strstr(accum_message, "Artist:")) != 0)
	    {
	      strcpy(display_struct.Artist.String, (strstr(accum_message_ptr,":") + 1));
	      display_struct.Artist.Ready = 1;
	      //	      printf("Found Artist: %s\n", display_struct.Artist.String);
	    }

	  if((accum_message_ptr = strstr(accum_message, "Album:")) != 0)
	    {
	      strcpy(display_struct.Album.String, (strstr(accum_message_ptr,":") + 1));
	      display_struct.Album.Ready = 1;
	      //	      printf("Found Album: %s\n", display_struct.Album.String);
	    }

	  if((accum_message_ptr = strstr(accum_message, "Time:")) != 0)
	    {
	      strcpy(display_struct.Time.String, (strstr(accum_message_ptr,":") + 1));
	      display_struct.Time.Ready = 1;
	      //	      printf("Found Time: %s\n", display_struct.Time); //### comment out if doesn't work
	    }

	  if((accum_message_ptr = strstr(accum_message, "Progress:")) != 0)
	    {
	      strcpy(display_struct.Progress.String, (strstr(accum_message_ptr,":") + 1));
	      display_struct.Progress.Ready = 1;
	      //	      printf("Found Progress: %s\n", display_struct.Progress.String);
	    }

	  if((accum_message_ptr = strstr(accum_message, "File:")) != 0)
	    {
	      strcpy(display_struct.File.String, (strstr(accum_message_ptr,":") + 1));
	      display_struct.File.Ready = 1;
	      //	      printf("Found File: %s\n", display_struct.File.String);
	    }

	  if((accum_message_ptr = strstr(accum_message, "Message:")) != 0)
	    {
	      strcpy(display_struct.Message.String, (strstr(accum_message_ptr,":") + 1));
	      display_struct.Message.Ready = 1;
	      //	      printf("Found Message: %s\n", display_struct.Message.String);
	    }

	  if((accum_message_ptr = strstr(accum_message, "Line1:")) != 0)
	    {
	      strcpy(display_struct.Line1.String, (strstr(accum_message_ptr,":") + 1));
	      display_struct.Line1.Ready = 1;
	      //	      printf("Found Line1: %s\n", display_struct.Line1.String);
	    }

	  if((accum_message_ptr = strstr(accum_message, "Line2:")) != 0)
	    {
	      strcpy(display_struct.Line2.String, (strstr(accum_message_ptr,":") + 1));
	      display_struct.Line2.Ready = 1;
	      //	      printf("Found Line2: %s\n", display_struct.Line2.String);
	    }

	  /*
	   * Clear out message after processing for data.
	   */
	  accum_message[0] = '\0';
	}

      /*
       * We can simply use close() to terminate the
       * connection.
       */
      fclose(child_display_file_ptr);
    }

  /*
   * Child should never return.
   * But, close the socket and the serial port to 
   * the display here just in case.
   */
  close(socket_descriptor);
  fclose(out_to_display);
  return 0;

}

/******************************
 * Display managment for IEE 
 * 40x2 display.
 ******************************/
void
display_iee_40x2()
{

  
  /*
   * Debugging code to print out Ready, Scroll and Busy status of display code.
   */
#if 0
  printf("READY/SCROLL Busy:%d Mes:%d/%d Title:%d/%d art:%d/%d alb:%d/%d file:%d/%d/%s time:%d/0\n",
	 display_struct.display_busy,
	 display_struct.Message.Ready, display_struct.Message.Scroll, 
	 display_struct.Title.Ready, display_struct.Title.Scroll,
	 display_struct.Artist.Ready, display_struct.Artist.Scroll,
	 display_struct.Album.Ready, display_struct.Album.Scroll,
	 display_struct.File.Ready, display_struct.File.Scroll,
	 display_struct.File.String,
	 display_struct.Time.Ready
	 );
#endif


  /*
   * Format dimmer message.
   */
  if(display_struct.Dimmer_Event == 1)
    {
      snprintf(display_server_message, DISPLAY_MESG_SIZE, "%c", DISPLAY_IEE_DIM);
    }
  else
    {
      if(display_struct.Dimmer_Event == (DIMMER_EVENT_WAIT - 1))
	{
	  snprintf(display_server_message, DISPLAY_MESG_SIZE, "%c", DISPLAY_IEE_BRIGHT);
	}
    }



  /*
   * Format "message" message.
   */
  if((display_struct.Message.Ready == 1) && (display_struct.display_busy == 0))
    {
      /*
       * Busy out the display and up the message priority.
       */
      display_struct.display_busy = 1;
      display_struct.Message.Ready = 2;

      /*
       * Use Message length to calculate number
       * of times to scroll.
       */
      display_struct.Message.Scroll = strlen(display_struct.Message.String);
      display_struct.Dimmer_Event = DIMMER_EVENT_WAIT;
      display_struct.Message.Event = 10;

      /*
       * If the string fits the display, don't scroll.
       */
      if((display_struct.Message.Scroll <= DISPLAY_IEE40X2_WIDTH) && (display_struct.Message.Scroll != 0))
	{
	  /*
	   * String fits display, don't scroll.
	   */
	  display_struct.Message.Scroll = 0;
	}

      /*
       * Setup to display string.
       */
      sprintf(display_server_message, "%c%c%c%c%-40.40s", DISPLAY_IEE_HOME,DISPLAY_IEE_CLR,DISPLAY_IEE_POSITION,DISPLAY_IEE_POS_0_1,display_struct.Message.String);
    }
  else
    {
      if(display_struct.Message.Ready == 2)
	{
	  if(display_struct.Message.Scroll > 0)
	    {
	      int starting_char;

	      starting_char = strlen(display_struct.Message.String) - display_struct.Message.Scroll;

	      /*
	       * Copy only what can be shown on the display.
	       */
	      sprintf(display_server_message, "%c%c%c%c%-40.40s", DISPLAY_IEE_HOME,DISPLAY_IEE_CLR,DISPLAY_IEE_POSITION,DISPLAY_IEE_POS_1_0,display_struct.Message.String);
	      display_struct.Message.Scroll--;

	      /*
	       * Switch to faster scrolling interval.
	       */
	      display_struct.Interval = DISPLAY_SCROLL_INTERVAL;
	    }
	  else
	    {
	      sprintf(last_message, "%c%c%c%c%-40.40s", DISPLAY_IEE_HOME,DISPLAY_IEE_CLR,DISPLAY_IEE_POSITION,DISPLAY_IEE_POS_0_1,display_struct.Message.String);
	      display_struct.Message.Scroll = 0;
	      
	      /*
	       * Either decrement event counter or if we are done with the events, 
	       * release control of the display.
	       */
	      if(display_struct.Message.Event > 0)
		{
		  display_struct.Message.Event--;
		}
	      else
		{
		  /*
		   * Un-busy the display and release the message priority.
		   */
		  display_struct.display_busy = 0;
		  display_struct.Message.Ready = 0;
		  display_struct.Interval = DISPLAY_NORMAL_INTERVAL;
		  display_struct.Message.Event = 0;

		  /*
		   * If (at least) the title string is populated, set up to 
		   * re-display what is (or was last) playing (after displaying
		   * a message). 
		   */
		  if(strlen(display_struct.Title.String) > 0)
		    {
		      display_struct.Title.Ready = 1;
		      display_struct.Artist.Ready = 1;
		      display_struct.Album.Ready = 1;
		    }
		}
	    }
	}
    }


  /*************************************************************
   *
   * We want to develop a message with the following message
   * format:
   *
   * 1234567890123456789012345678901234567890
   * <Title.................................>
   * <Artist............><Album.............>
   *
   * And we want to scroll any oversize strings.
   */
  {
    static char display_server_message_title[DISPLAY_MESG_SIZE];
    static char display_server_message_artist[DISPLAY_MESG_SIZE];
    static char display_server_message_album[DISPLAY_MESG_SIZE];

    /*
     * Format title message.
     */
    if(
       (display_struct.Title.Ready == 1) && 
       (
	(display_struct.display_busy == 0) ||
	(
	 (display_struct.Album.Ready == 2)||
	 (display_struct.Artist.Ready == 2)
	 )
	)
       )
      {
	/*
	 * Busy out the display and up the title priority.
	 */
	display_struct.display_busy = 1;
	display_struct.Title.Ready = 2;
	display_struct.Interval = DISPLAY_SCROLL_INTERVAL;

	/*
	 * Use Message length to calculate number
	 * of times to scroll.
	 */
	display_struct.Title.Scroll = strlen(display_struct.Title.String);
	display_struct.Title.Event = 10;
	display_struct.Dimmer_Event = DIMMER_EVENT_WAIT;

	/*
	 * If the string fits the display, don't scroll.
	 */
	if(display_struct.Title.Scroll <= DISPLAY_IEE40X2_WIDTH )
	  {
	    /*
	     * String fits display, don't scroll.
	     */
	    display_struct.Title.Scroll = 0;
	  }

	/*
	 * Setup to display string.
	 */
	sprintf(display_server_message_title, "%c%c%-40.40s",DISPLAY_IEE_POSITION, DISPLAY_IEE_POS_0_0, display_struct.Title.String);
      }
    else 
      {
	if(display_struct.Title.Ready == 2)
	  {
	    if(display_struct.Title.Scroll > 0)
	      {
		int starting_char;

		starting_char = strlen(display_struct.Title.String) - display_struct.Title.Scroll;

		/*
		 * Copy only what can be shown on the display.
		 */
		sprintf(display_server_message_title, "%c%c%-40.40s",DISPLAY_IEE_POSITION, DISPLAY_IEE_POS_0_0, &display_struct.Title.String[starting_char]);
		display_struct.Title.Scroll--;

		/*
		 * Switch to faster scrolling interval.
		 */
		display_struct.Interval = DISPLAY_SCROLL_INTERVAL;
	      }
	    else
	      {
		sprintf(display_server_message_title, "%c%c%-40.40s",DISPLAY_IEE_POSITION, DISPLAY_IEE_POS_0_0, display_struct.Title.String);
		display_struct.Title.Scroll = 0;

		/*
		 * Either decrement event counter or if we are done with the events, 
		 * release control of the display.
		 */
		if(display_struct.Title.Event > 0)
		  {
		    display_struct.Title.Event--;
		  }
		else
		  {
		    /*
		     * Un-busy the display and release the title priority.
		     */
		    display_struct.display_busy = 0;
		    display_struct.Title.Ready = 0;
		    display_struct.Interval = DISPLAY_NORMAL_INTERVAL;
		    display_struct.Title.Event = 0;
		  }
	      }
	  }
      }


    /*
     * Format artist message.
     */
    if(
       (display_struct.Artist.Ready == 1) && 
       (
	(display_struct.display_busy == 0) ||
	(
	 (display_struct.Album.Ready == 2)||
	 (display_struct.Title.Ready == 2)
	 )
	)
       )
      {
	/*
	 * Busy out the display and up the artist priority.
	 */
	display_struct.display_busy = 1;
	display_struct.Artist.Ready = 2;
	display_struct.Interval = DISPLAY_SCROLL_INTERVAL;

	/*
	 * Use Message length to calculate number
	 * of times to scroll.
	 */
	display_struct.Artist.Scroll = strlen(display_struct.Artist.String);
	display_struct.Artist.Event = 10;
	display_struct.Dimmer_Event = DIMMER_EVENT_WAIT;

	/*
	 * If the string fits the display, don't scroll.
	 */
	  {
	    /*
	     * String fits display, don't scroll.
	     */
	    display_struct.Artist.Scroll = 0;
	  }

	/*
	 * Setup to display string.
	 */
	sprintf(display_server_message_artist, "%c%c(%-18.18s)",DISPLAY_IEE_POSITION, DISPLAY_IEE_POS_1_0, display_struct.Artist.String);
      }
    else
      {
	if(display_struct.Artist.Ready == 2)
	  {
	    if(display_struct.Artist.Scroll > 0)
	      {
		int starting_char;

		starting_char = strlen(display_struct.Artist.String) - display_struct.Artist.Scroll;

		/*
		 * Copy only what can be shown on the display.
		 */
		sprintf(display_server_message_artist, "%c%c(%-18.18s)",DISPLAY_IEE_POSITION, DISPLAY_IEE_POS_1_0, &display_struct.Artist.String[starting_char]);
		display_struct.Artist.Scroll--;

		/*
		 * Switch to faster scrolling interval.
		 */
		display_struct.Interval = DISPLAY_SCROLL_INTERVAL;
	      }
	    else
	      {
		sprintf(display_server_message_artist, "%c%c(%-18.18s)",DISPLAY_IEE_POSITION, DISPLAY_IEE_POS_1_0, display_struct.Artist.String);
		display_struct.Artist.Scroll = 0;

		/*
		 * Either decrement event counter or if we are done with the events, 
		 * release control of the display.
		 */
		if(display_struct.Artist.Event > 0)
		  {
		    display_struct.Artist.Event--;
		  }
		else
		  {
		    /*
		     * Un-busy out the display and release the artist priority.
		     */
		    display_struct.display_busy = 0;
		    display_struct.Artist.Ready = 0;
		    display_struct.Interval = DISPLAY_NORMAL_INTERVAL;
		    display_struct.Artist.Event = 0;
		  }
	      }
	  }
      }


    /*
     * Format album message.
     */
    if(
       (display_struct.Album.Ready == 1) && 
       (
	(display_struct.display_busy == 0) ||
	(
	 (display_struct.Artist.Ready == 2)||
	 (display_struct.Title.Ready == 2)
	 )
	)
       )
      {
	/*
	 * Busy out the display and up the album priority.
	 */
	display_struct.display_busy = 1;
	display_struct.Album.Ready = 2;
	display_struct.Interval = DISPLAY_SCROLL_INTERVAL;

	/*
	 * Use Message length to calculate number
	 * of times to scroll.
	 */
	display_struct.Album.Scroll = strlen(display_struct.Album.String);
	display_struct.Album.Event = 10;
	display_struct.Dimmer_Event = DIMMER_EVENT_WAIT;

	/*
	 * If the string fits the display, don't scroll.
	 */
	if((display_struct.Album.Scroll <= ((DISPLAY_IEE40X2_WIDTH / 2) - 3)) && (display_struct.Album.Scroll != 0))
	  {
	    /*
	     * String fits display, don't scroll.
	     */
	    display_struct.Album.Scroll = 0;
	  }

	/*
	 * Setup to display string.
	 */
	sprintf(display_server_message_album, "%c%c(      -ALBUM-     )",DISPLAY_IEE_POSITION, DISPLAY_IEE_POS_1_20);
      }
    else
      {
	if(display_struct.Album.Ready == 2)
	  {
	    if(display_struct.Album.Scroll > 0)
	      {
		int starting_char;

		starting_char = strlen(display_struct.Album.String) - display_struct.Album.Scroll;

		/*
		 * Copy only what can be shown on the display.
		 */
		sprintf(display_server_message_album, "%c%c(%-17.17s)",DISPLAY_IEE_POSITION, DISPLAY_IEE_POS_1_20, &display_struct.Album.String[starting_char]);
		display_struct.Album.Scroll--;

		/*
		 * Switch to faster scrolling interval.
		 */
		display_struct.Interval = DISPLAY_SCROLL_INTERVAL;
	      }
	    else
	      {
		sprintf(display_server_message_album, "%c%c(%-17.17s)",DISPLAY_IEE_POSITION, DISPLAY_IEE_POS_1_20, display_struct.Album.String);
		display_struct.Album.Scroll = 0;

		/*
		 * Either decrement event counter or if we are done with the events, 
		 * release control of the display.
		 */
		if(display_struct.Album.Event > 0)
		  {
		    display_struct.Album.Event--;
		  }
		else
		  {
		    /*
		     * Un-busy out the display and release the artist priority.
		     */
		    display_struct.display_busy = 0;
		    display_struct.Album.Ready = 0;
		    display_struct.Interval = DISPLAY_NORMAL_INTERVAL;
		    display_struct.Album.Event = 0;
		  }
	      }
	  }
      }


    /*
     * If any (Title, Artist or Album) of the above strings are
     * ready, assemble a composit string.
     */
    sprintf(display_server_message, "%c%c", DISPLAY_IEE_CLR, DISPLAY_IEE_CURINV);

    if(display_struct.Title.Ready == 2)
      {
	strcat(display_server_message, display_server_message_title);
      }
    if(display_struct.Artist.Ready == 2)
      {
	strcat(display_server_message, display_server_message_artist);
      }
    if(display_struct.Album.Ready == 2)
      {
	strcat(display_server_message, display_server_message_album);
      }


  }



  /*
   * Format elapsed time message.  (Back Space:0x08)
   */
  if(display_struct.Time.Ready == 1)
    {
      display_struct.Time.Ready = 0;

      //      snprintf(display_server_message, DISPLAY_MESG_SIZE, "%-8.8s%c%c%c%c%c%c%c%c", display_struct.Time,
      //	       0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08);
    }








  /*
   * Format progress message.
   */
  if((display_struct.Progress.Ready == 1) && (display_struct.display_busy == 0))
    {
      /*
       * Busy out the display and up the progress priority.
       */
      display_struct.display_busy = 1;
      display_struct.Progress.Ready = 2;
      display_struct.Interval = DISPLAY_SCROLL_INTERVAL;

      /*
       * Use Message length to calculate number
       * of times to scroll.
       */
      display_struct.Progress.Scroll = strlen(display_struct.Progress.String);
      display_struct.Progress.Event = 10;
      display_struct.Dimmer_Event = DIMMER_EVENT_WAIT;

      /*
       * If the string fits the display, don't scroll.
       */
      if((display_struct.Progress.Scroll <= DISPLAY_IEE40X2_WIDTH) && (display_struct.Progress.Scroll != 0))
        {
          /*
           * String fits display, don't scroll.
           */
          display_struct.Progress.Scroll = 0;
        }

      /*
       * Setup to display string.
       */
      sprintf(display_server_message, "%c%-40.40s", DISPLAY_IEE_CR,display_struct.Progress.String);
    }
  else 
    {
      if(display_struct.Progress.Ready == 2)
	{
	  if(display_struct.Progress.Scroll > 0)
	    {
	      int starting_char;

	      starting_char = strlen(display_struct.Progress.String) - display_struct.Progress.Scroll;

	      /*
	       * Copy only what can be shown on the display.
	       */
	      sprintf(display_server_message, "%c%-40.40s", DISPLAY_IEE_CR, &display_struct.Progress.String[starting_char]);

	      display_struct.Progress.Scroll--;

              /*
               * Switch to faster scrolling interval.
               */
              display_struct.Interval = DISPLAY_SCROLL_INTERVAL;
	    }
	  else
	    {
	      sprintf(last_message, "%c%-40.40s", DISPLAY_IEE_CR, display_struct.Progress.String);
	      display_struct.Progress.Scroll = 0;

              /*
               * Either decrement event counter or if we are done with the events, 
               * release control of the display.
               */
              if(display_struct.Progress.Event > 0)
                {
                  display_struct.Progress.Event--;
                }
              else
                {
		  /*
		   * Un-busy the display and release the progress priority.
		   */
		  display_struct.display_busy = 0;
		  display_struct.Progress.Ready = 0;
		  display_struct.Interval = DISPLAY_NORMAL_INTERVAL;
		  display_struct.Progress.Event = 0;
		}
	    }
	}
    }


  /*
   * Format file name message.
   */
  if((display_struct.File.Ready == 1) && (display_struct.display_busy == 0))
    {
      /*
       * Busy out the display and up the File priority.
       */
      display_struct.display_busy = 1;
      display_struct.File.Ready = 2;
      display_struct.Interval = DISPLAY_SCROLL_INTERVAL;

      /*
       * Use Message length to calculate number
       * of times to scroll.
       */
      display_struct.File.Scroll = strlen(display_struct.File.String);
      display_struct.File.Event = 10;
      display_struct.Dimmer_Event = DIMMER_EVENT_WAIT;

      /*
       * If the string fits the display, don't scroll.
       */
      if((display_struct.File.Scroll <= DISPLAY_IEE40X2_WIDTH) && (display_struct.File.Scroll != 0))
        {
          /*
           * String fits display, don't scroll.
           */
          display_struct.File.Scroll = 0;
        }

      /*
       * Setup to display string.
       */
      sprintf(display_server_message, "%c%c%c%c%c%-40.40s", DISPLAY_IEE_HOME,DISPLAY_IEE_CLR, DISPLAY_IEE_POSITION, DISPLAY_IEE_POS_0_0, DISPLAY_IEE_CURINV, display_struct.File.String);
    }
  else
    {
      if(display_struct.File.Ready == 2)
	{
	  if(display_struct.File.Scroll > 0)
            {
	      int starting_char;

	      starting_char = strlen(display_struct.File.String) - display_struct.File.Scroll;
	      /*
	       * Copy only what can be shown on the display.
	       */
	      sprintf(display_server_message, "%c%c%c%c%c%-40.40s", DISPLAY_IEE_HOME,DISPLAY_IEE_CLR, DISPLAY_IEE_POSITION, DISPLAY_IEE_POS_0_0, DISPLAY_IEE_CURINV, &display_struct.File.String[starting_char]);
	      display_struct.File.Scroll--;

              /*
               * Switch to faster scrolling interval.
               */
              display_struct.Interval = DISPLAY_SCROLL_INTERVAL;
	    }
	  else
	    {
	      sprintf(last_message, "%c%c%c%c%c%-40.40s", DISPLAY_IEE_HOME,DISPLAY_IEE_CLR, DISPLAY_IEE_POSITION, DISPLAY_IEE_POS_0_0, DISPLAY_IEE_CURINV, display_struct.File.String);
	      display_struct.File.Scroll = 0;
		
              /*
               * Either decrement event counter or if we are done with the events, 
               * release control of the display.
               */
              if(display_struct.File.Event > 0)
                {
                  display_struct.File.Event--;
                }
              else
                {
		  /*
		   * Un-busy the display and release the album priority.
		   */
		  display_struct.display_busy = 0;
		  display_struct.File.Ready = 0;
		  display_struct.Interval = DISPLAY_NORMAL_INTERVAL;
		  display_struct.File.Event = 0;
		}
	    }
	}
    }


  /*
   * Format "Line1 & Line2" message.
   *
   * Assume Lines 1 & 2 will always be done together and that Line2 will be
   * done last, so use Lin2 flags to trigger update.
   */
  if((display_struct.Line2.Ready == 1) && (display_struct.display_busy == 0))
    {
      /*
       * Busy out the display and up the message priority.
       */
      display_struct.display_busy = 1;
      display_struct.Line2.Ready = 2;

      /*
       * We will probably never scroll as the format from the server is
       * a 40 char string.
       */
      display_struct.Line2.Scroll = 0;

      /*
       * Setup to display string.
       */
      sprintf(display_server_message, "%c%c%c%c%-40.40s%-39.39s", 
              DISPLAY_IEE_CLR, DISPLAY_IEE_CURINV, DISPLAY_IEE_POSITION,DISPLAY_IEE_POS_0_0,
              display_struct.Line1.String, display_struct.Line2.String);
    }
  else
    {
      if(display_struct.Line2.Ready == 2)
	{
	  sprintf(last_message, "%c%-40.40s", DISPLAY_IEE_CR, display_struct.Line1.String);
	  display_struct.Line2.Scroll = 0;
	      
	  /*
	   * Either decrement event counter or if we are done with the events, 
	   * release control of the display.
	   */
	  if(display_struct.Line2.Event > 0)
	    {
	      display_struct.Line2.Event--;
	    }
	  else
	    {
	      /*
	       * Un-busy the display and release the message priority.
	       */
	      display_struct.display_busy = 0;
	      display_struct.Line2.Ready = 0;
	      display_struct.Interval = DISPLAY_NORMAL_INTERVAL;
	      display_struct.Line2.Event = 0;
	    }
	}
    }


  /*
   * If we are in a busy state and no one is updating the display, assume
   * new data from the client interrupted us and start displaying any new 
   * data that is marked ready.
   */
  if(
     (display_struct.Title.Ready != 2) &&
     (display_struct.Artist.Ready != 2) &&
     (display_struct.Album.Ready != 2) &&
     (display_struct.Year.Ready != 2) &&
     (display_struct.Genre.Ready != 2) &&
     (display_struct.Time.Ready != 2) &&
     (display_struct.rTime.Ready != 2) &&
     (display_struct.Progress.Ready != 2) &&
     (display_struct.File.Ready != 2) &&
     (display_struct.pFile.Ready != 2) &&
     (display_struct.nFile.Ready != 2) &&
     (display_struct.Message.Ready != 2) &&
     (display_struct.Line1.Ready != 2) &&
     (display_struct.Line2.Ready != 2) &&
     (display_struct.display_busy == 1)
     )
    {
      display_struct.display_busy = 0;
      display_struct.Interval = DISPLAY_NORMAL_INTERVAL;
    }
  else
    {
      /*
       * If we are not busy, display the beginning (as wide as this display 
       * is) of the last message if there is one.
       */
      //      if(
      //	 (display_struct.display_busy == 0) &&
      //	 (strlen(last_message) > 0)
      //	 )
      //	{
      //	  sprintf(display_server_message, "%-40.40s", last_message);
      //	  last_message[0] = '\0';
      //	  display_struct.Interval = DISPLAY_NORMAL_INTERVAL;
      //	}
    }

}


/******************************
 * Display managment for IEE 
 * 16x1 display.
 ******************************/
void
display_iee_16x1()
{

  static char total_line1_and_line2[100];

#if 0
  printf("READY/SCROLL Busy:%d Mes:%d/%d Title:%d/%d art:%d/%d alb:%d/%d file:%d/%d/%s time:%d/0\n",
	 display_struct.display_busy,
	 display_struct.Message.Ready, display_struct.Message.Scroll, 
	 display_struct.Title.Ready, display_struct.Title.Scroll,
	 display_struct.Artist.Ready, display_struct.Artist.Scroll,
	 display_struct.Album.Ready, display_struct.Album.Scroll,
	 display_struct.File.Ready, display_struct.File.Scroll,
	 display_struct.File.String,
	 display_struct.Time.Ready
	 );
#endif

  /*
   * Format dimmer message.
   */
  if(display_struct.Dimmer_Event == 1)
    {
      snprintf(display_server_message, DISPLAY_MESG_SIZE, "%c", DISPLAY_IEE_DIM);
    }
  else
    {
      if(display_struct.Dimmer_Event == (DIMMER_EVENT_WAIT - 1))
	{
	  snprintf(display_server_message, DISPLAY_MESG_SIZE, "%c", DISPLAY_IEE_BRIGHT);
	}
    }



  /*
   * Format "message" message.
   */
  if((display_struct.Message.Ready == 1) && (display_struct.display_busy == 0))
    {
      /*
       * Busy out the display and up the message priority.
       */
      display_struct.display_busy = 1;
      display_struct.Message.Ready = 2;

      /*
       * Use Message length to calculate number
       * of times to scroll.
       */
      display_struct.Message.Scroll = strlen(display_struct.Message.String);
      display_struct.Dimmer_Event = DIMMER_EVENT_WAIT;
      display_struct.Message.Event = 10;

      /*
       * If the string fits the display, don't scroll.
       */
      if((display_struct.Message.Scroll <= DISPLAY_IEE16X1_WIDTH) && (display_struct.Message.Scroll != 0))
	{
	  /*
	   * String fits display, don't scroll.
	   */
	  display_struct.Message.Scroll = 0;
	}

      /*
       * Setup to display string.
       */
      snprintf(display_server_message, 18, "%c%-16.16s", DISPLAY_IEE_CR,display_struct.Message.String);
    }
  else
    {
      if(display_struct.Message.Ready == 2)
	{
	  if(display_struct.Message.Scroll > 0)
	    {
	      int starting_char;

	      starting_char = strlen(display_struct.Message.String) - display_struct.Message.Scroll;

	      /*
	       * Copy only what can be shown on the display.
	       */
	      snprintf(display_server_message, 18, "%c%-16.16s", DISPLAY_IEE_CR, &display_struct.Message.String[starting_char]);

	      display_struct.Message.Scroll--;

	      /*
	       * Switch to faster scrolling interval.
	       */
	      display_struct.Interval = DISPLAY_SCROLL_INTERVAL;
	    }
	  else
	    {
	      snprintf(last_message, 18, "%c%-16.16s", DISPLAY_IEE_CR, display_struct.Message.String);
	      display_struct.Message.Scroll = 0;
	      
	      /*
	       * Either decrement event counter or if we are done with the events, 
	       * release control of the display.
	       */
	      if(display_struct.Message.Event > 0)
		{
		  display_struct.Message.Event--;
		}
	      else
		{
		  /*
		   * Un-busy the display and release the message priority.
		   */
		  display_struct.display_busy = 0;
		  display_struct.Message.Ready = 0;
		  display_struct.Interval = DISPLAY_NORMAL_INTERVAL;
		  display_struct.Message.Event = 0;

		  /*
		   * If (at least) the title string is populated, set up to 
		   * re-display what is (or was last) playing (after displaying
		   * a message). 
		   */
		  if(strlen(display_struct.Title.String) > 0)
		    {
		      display_struct.Title.Ready = 1;
		      display_struct.Artist.Ready = 1;
		      display_struct.Album.Ready = 1;
		    }
		}
	    }
	}
    }


  /*
   * Format artist message.
   */
  if((display_struct.Artist.Ready == 1) && (display_struct.display_busy == 0))
    {
      /*
       * Busy out the display and up the artist priority.
       */
      display_struct.display_busy = 1;
      display_struct.Artist.Ready = 2;
      display_struct.Interval = DISPLAY_SCROLL_INTERVAL;

      /*
       * Use Message length to calculate number
       * of times to scroll.
       */
      display_struct.Artist.Scroll = strlen(display_struct.Artist.String);
      display_struct.Artist.Event = 10;
      display_struct.Dimmer_Event = DIMMER_EVENT_WAIT;

      /*
       * If the string fits the display, don't scroll.
       */
      if((display_struct.Artist.Scroll <= DISPLAY_IEE16X1_WIDTH) && (display_struct.Artist.Scroll != 0))
        {
          /*
           * String fits display, don't scroll.
           */
          display_struct.Artist.Scroll = 0;
        }

      /*
       * Setup to display string.
       */
      snprintf(display_server_message, 18, "%c%-16.16s", DISPLAY_IEE_CR,display_struct.Artist.String);
    }
  else
    {
      if(display_struct.Artist.Ready == 2)
	{
	  if(display_struct.Artist.Scroll > 0)
	    {
	      int starting_char;

	      starting_char = strlen(display_struct.Artist.String) - display_struct.Artist.Scroll;

	      /*
	       * Copy only what can be shown on the display.
	       */
	      snprintf(display_server_message, 18, "%c%-16.16s", DISPLAY_IEE_CR, &display_struct.Artist.String[starting_char]);

	      display_struct.Artist.Scroll--;

              /*
               * Switch to faster scrolling interval.
               */
              display_struct.Interval = DISPLAY_SCROLL_INTERVAL;
	    }
	  else
	    {
	      snprintf(last_message, 18, "%c%-16.16s", DISPLAY_IEE_CR, display_struct.Artist.String);
	      display_struct.Artist.Scroll = 0;

              /*
               * Either decrement event counter or if we are done with the events, 
               * release control of the display.
               */
	      if(display_struct.Artist.Event > 0)
		{
		  display_struct.Artist.Event--;
		}
	      else
		{
		  /*
		   * Un-busy out the display and release the artist priority.
		   */
		  display_struct.display_busy = 0;
		  display_struct.Artist.Ready = 0;
		  display_struct.Interval = DISPLAY_NORMAL_INTERVAL;
		  display_struct.Artist.Event = 0;
		}
	    }
	}
    }


  /*
   * Format album message.
   */
  if((display_struct.Album.Ready == 1) && (display_struct.display_busy == 0))
    {
      /*
       * Busy out the display and up the album priority.
       */
      display_struct.display_busy = 1;
      display_struct.Album.Ready = 2;
      display_struct.Interval = DISPLAY_SCROLL_INTERVAL;

      /*
       * Use Message length to calculate number
       * of times to scroll.
       */
      display_struct.Album.Scroll = strlen(display_struct.Album.String);
      display_struct.Album.Event = 10;
      display_struct.Dimmer_Event = DIMMER_EVENT_WAIT;

      /*
       * If the string fits the display, don't scroll.
       */
      if((display_struct.Album.Scroll <= 16) && (display_struct.Album.Scroll != 0))
        {
          /*
           * String fits display, don't scroll.
           */
          display_struct.Album.Scroll = 0;
        }

      /*
       * Setup to display string.
       */
      snprintf(display_server_message, 18, "%c%-16.16s", DISPLAY_IEE_CR,display_struct.Album.String);
    }
  else
    {
      if(display_struct.Album.Ready == 2)
	{
	  if(display_struct.Album.Scroll > 0)
	    {
	      int starting_char;

	      starting_char = strlen(display_struct.Album.String) - display_struct.Album.Scroll;

	      /*
	       * Copy only what can be shown on the display.
	       */
	      snprintf(display_server_message, 18, "%c%-16.16s", DISPLAY_IEE_CR, &display_struct.Album.String[starting_char]);

	      display_struct.Album.Scroll--;

              /*
               * Switch to faster scrolling interval.
               */
              display_struct.Interval = DISPLAY_SCROLL_INTERVAL;
	    }
	  else
	    {
	      snprintf(last_message, 18, "%c%-16.16s", DISPLAY_IEE_CR, display_struct.Album.String);
	      display_struct.Album.Scroll = 0;

              /*
               * Either decrement event counter or if we are done with the events, 
               * release control of the display.
               */
	      if(display_struct.Album.Event >0)
		{
		  display_struct.Album.Event--;
		}
	      else
		{
		  /*
		   * Un-busy out the display and release the artist priority.
		   */
		  display_struct.display_busy = 0;
		  display_struct.Album.Ready = 0;
		  display_struct.Interval = DISPLAY_NORMAL_INTERVAL;
		  display_struct.Album.Event = 0;
		}
	    }
	}
    }


  /*
   * Format title message.
   */
  if((display_struct.Title.Ready == 1) && (display_struct.display_busy == 0))
    {
      /*
       * Busy out the display and up the title priority.
       */
      display_struct.display_busy = 1;
      display_struct.Title.Ready = 2;
      display_struct.Interval = DISPLAY_SCROLL_INTERVAL;

      /*
       * Use Message length to calculate number
       * of times to scroll.
       */
      display_struct.Title.Scroll = strlen(display_struct.Title.String);
      display_struct.Title.Event = 10;
      display_struct.Dimmer_Event = DIMMER_EVENT_WAIT;

      /*
       * If the string fits the display, don't scroll.
       */
      if((display_struct.Title.Scroll <= DISPLAY_IEE16X1_WIDTH) && (display_struct.Title.Scroll != 0))
        {
          /*
           * String fits display, don't scroll.
           */
          display_struct.Title.Scroll = 0;
        }

      /*
       * Setup to display string.
       */
      snprintf(display_server_message, 18, "%c%-16.16s", DISPLAY_IEE_CR,display_struct.Title.String);
    }
  else 
    {
      if(display_struct.Title.Ready == 2)
	{
	  if(display_struct.Title.Scroll > 0)
	    {
	      int starting_char;

	      starting_char = strlen(display_struct.Title.String) - display_struct.Title.Scroll;

	      /*
	       * Copy only what can be shown on the display.
	       */
	      snprintf(display_server_message, 18, "%c%-16.16s", DISPLAY_IEE_CR, &display_struct.Title.String[starting_char]);

	      display_struct.Title.Scroll--;

              /*
               * Switch to faster scrolling interval.
               */
              display_struct.Interval = DISPLAY_SCROLL_INTERVAL;
	    }
	  else
	    {
	      snprintf(last_message, 18, "%c%-16.16s", DISPLAY_IEE_CR, display_struct.Title.String);
	      display_struct.Title.Scroll = 0;

              /*
               * Either decrement event counter or if we are done with the events, 
               * release control of the display.
               */
              if(display_struct.Title.Event > 0)
                {
                  display_struct.Title.Event--;
                }
              else
                {
		  /*
		   * Un-busy the display and release the title priority.
		   */
		  display_struct.display_busy = 0;
		  display_struct.Title.Ready = 0;
		  display_struct.Interval = DISPLAY_NORMAL_INTERVAL;
		  display_struct.Title.Event = 0;
		}
	    }
	}
    }


  /*
   * Format elapsed time message.  (Back Space:0x08)
   */
  if(display_struct.Time.Ready == 1)
    {
      display_struct.Time.Ready = 0;

      //      snprintf(display_server_message, DISPLAY_MESG_SIZE, "%-8.8s%c%c%c%c%c%c%c%c", display_struct.Time,
      //	       0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08);
    }


  /*
   * Format progress message.
   */
  if((display_struct.Progress.Ready == 1) && (display_struct.display_busy == 0))
    {
      /*
       * Busy out the display and up the progress priority.
       */
      display_struct.display_busy = 1;
      display_struct.Progress.Ready = 2;
      display_struct.Interval = DISPLAY_SCROLL_INTERVAL;

      /*
       * Use Message length to calculate number
       * of times to scroll.
       */
      display_struct.Progress.Scroll = strlen(display_struct.Progress.String);
      display_struct.Progress.Event = 10;
      display_struct.Dimmer_Event = DIMMER_EVENT_WAIT;

      /*
       * If the string fits the display, don't scroll.
       */
      if((display_struct.Progress.Scroll <= DISPLAY_IEE16X1_WIDTH) && (display_struct.Progress.Scroll != 0))
        {
          /*
           * String fits display, don't scroll.
           */
          display_struct.Progress.Scroll = 0;
        }

      /*
       * Setup to display string.
       */
      snprintf(display_server_message, 18, "%c%-16.16s", DISPLAY_IEE_CR,display_struct.Progress.String);
    }
  else 
    {
      if(display_struct.Progress.Ready == 2)
	{
	  if(display_struct.Progress.Scroll > 0)
	    {
	      int starting_char;

	      starting_char = strlen(display_struct.Progress.String) - display_struct.Progress.Scroll;

	      /*
	       * Copy only what can be shown on the display.
	       */
	      snprintf(display_server_message, 18, "%c%-16.16s", DISPLAY_IEE_CR, &display_struct.Progress.String[starting_char]);

	      display_struct.Progress.Scroll--;

              /*
               * Switch to faster scrolling interval.
               */
              display_struct.Interval = DISPLAY_SCROLL_INTERVAL;
	    }
	  else
	    {
	      snprintf(last_message, 18, "%c%-16.16s", DISPLAY_IEE_CR, display_struct.Progress.String);
	      display_struct.Progress.Scroll = 0;

              /*
               * Either decrement event counter or if we are done with the events, 
               * release control of the display.
               */
              if(display_struct.Progress.Event > 0)
                {
                  display_struct.Progress.Event--;
                }
              else
                {
		  /*
		   * Un-busy the display and release the progress priority.
		   */
		  display_struct.display_busy = 0;
		  display_struct.Progress.Ready = 0;
		  display_struct.Interval = DISPLAY_NORMAL_INTERVAL;
		  display_struct.Progress.Event = 0;
		}
	    }
	}
    }


  /*
   * Format file name message.
   */
  if((display_struct.File.Ready == 1) && (display_struct.display_busy == 0))
    {
      /*
       * Busy out the display and up the File priority.
       */
      display_struct.display_busy = 1;
      display_struct.File.Ready = 2;
      display_struct.Interval = DISPLAY_SCROLL_INTERVAL;

      /*
       * Use Message length to calculate number
       * of times to scroll.
       */
      display_struct.File.Scroll = strlen(display_struct.File.String);
      display_struct.File.Event = 10;
      display_struct.Dimmer_Event = DIMMER_EVENT_WAIT;

      /*
       * If the string fits the display, don't scroll.
       */
      if((display_struct.File.Scroll <= DISPLAY_IEE16X1_WIDTH) && (display_struct.File.Scroll != 0))
        {
          /*
           * String fits display, don't scroll.
           */
          display_struct.File.Scroll = 0;
        }

      /*
       * Setup to display string.
       */
      snprintf(display_server_message, 18, "%c%-16.16s", DISPLAY_IEE_CR,display_struct.File.String);
    }
  else
    {
      if(display_struct.File.Ready == 2)
	{
	  if(display_struct.File.Scroll > 0)
            {
	      int starting_char;

	      starting_char = strlen(display_struct.File.String) - display_struct.File.Scroll;
	      /*
	       * Copy only what can be shown on the display.
	       */
	      snprintf(display_server_message, 18, "%c%-16.16s", DISPLAY_IEE_CR, &display_struct.File.String[starting_char]);

	      display_struct.File.Scroll--;

              /*
               * Switch to faster scrolling interval.
               */
              display_struct.Interval = DISPLAY_SCROLL_INTERVAL;
	    }
	  else
	    {
	      snprintf(last_message, 18, "%c%-16.16s", DISPLAY_IEE_CR, display_struct.File.String);
	      display_struct.File.Scroll = 0;
		
              /*
               * Either decrement event counter or if we are done with the events, 
               * release control of the display.
               */
              if(display_struct.File.Event > 0)
                {
                  display_struct.File.Event--;
                }
              else
                {
		  /*
		   * Un-busy the display and release the album priority.
		   */
		  display_struct.display_busy = 0;
		  display_struct.File.Ready = 0;
		  display_struct.Interval = DISPLAY_NORMAL_INTERVAL;
		  display_struct.File.Event = 0;
		}
	    }
	}
    }


  /*
   * Format "Line1 & Line2" message.
   *
   * Assume Lines 1 & 2 will always be done together and that Line2 will be
   * done last, so use Lin2 flags to trigger update.
   */
  if((display_struct.Line2.Ready == 1) && (display_struct.display_busy == 0))
    {
      /*
       * Busy out the display and up the File priority.
       */
      display_struct.display_busy = 1;
      display_struct.Line2.Ready = 2;
      display_struct.Interval = DISPLAY_SCROLL_INTERVAL;

      /*
       * Use TOTAL Message length to calculate number
       * of times to scroll.
       */
      sprintf(total_line1_and_line2, "%40.40s%40.40s", display_struct.Line1.String, display_struct.Line2.String);
      display_struct.Line2.Scroll = strlen(total_line1_and_line2);
      display_struct.Line2.Event = 10;
      display_struct.Dimmer_Event = DIMMER_EVENT_WAIT;

      /*
       * If the string fits the display, don't scroll.
       */
      if((display_struct.Line2.Scroll <= DISPLAY_IEE16X1_WIDTH) && (display_struct.Line2.Scroll != 0))
        {
          /*
           * String fits display, don't scroll.
           */
          display_struct.Line2.Scroll = 0;
        }

      /*
       * Setup to display string.
       */
      snprintf(display_server_message, 18, "%c%-16.16s", DISPLAY_IEE_CR, total_line1_and_line2);
    }
  else
    {
      if(display_struct.Line2.Ready == 2)
	{
	  if(display_struct.Line2.Scroll > 0)
            {
	      int starting_char;

	      starting_char = strlen(total_line1_and_line2) - display_struct.Line2.Scroll;
	      /*
	       * Copy only what can be shown on the display.
	       */
	      snprintf(display_server_message, 18, "%c%-16.16s", DISPLAY_IEE_CR, &total_line1_and_line2[starting_char]);

	      display_struct.Line2.Scroll--;

              /*
               * Switch to faster scrolling interval.
               */
              display_struct.Interval = DISPLAY_SCROLL_INTERVAL;
	    }
	  else
	    {
	      snprintf(last_message, 18, "%c%-16.16s", DISPLAY_IEE_CR, display_struct.Line2.String);
	      display_struct.Line2.Scroll = 0;
		
              /*
               * Either decrement event counter or if we are done with the events, 
               * release control of the display.
               */
              if(display_struct.Line2.Event > 0)
                {
                  display_struct.Line2.Event--;
                }
              else
                {
		  /*
		   * Un-busy the display and release the album priority.
		   */
		  display_struct.display_busy = 0;
		  display_struct.Line2.Ready = 0;
		  display_struct.Interval = DISPLAY_NORMAL_INTERVAL;
		  display_struct.Line2.Event = 0;
		}
	    }
	}
    }


  /*
   * If we are in a busy state and no one is updating the display, assume
   * new data from the client interrupted us and start displaying any new 
   * data that is marked ready.
   */
  if(
     (display_struct.Title.Ready != 2) &&
     (display_struct.Artist.Ready != 2) &&
     (display_struct.Album.Ready != 2) &&
     (display_struct.Year.Ready != 2) &&
     (display_struct.Genre.Ready != 2) &&
     (display_struct.Time.Ready != 2) &&
     (display_struct.rTime.Ready != 2) &&
     (display_struct.Progress.Ready != 2) &&
     (display_struct.File.Ready != 2) &&
     (display_struct.pFile.Ready != 2) &&
     (display_struct.nFile.Ready != 2) &&
     (display_struct.Message.Ready != 2) &&
     (display_struct.Line1.Ready != 2) &&
     (display_struct.Line2.Ready != 2) &&
     (display_struct.display_busy == 1)
     )
    {
      display_struct.display_busy = 0;
      display_struct.Interval = DISPLAY_NORMAL_INTERVAL;
    }
  else
    {
      /*
       * If we are not busy, display the beginning (as wide as this display 
       * is) of the last message if there is one.
       */
      if(
	 (display_struct.display_busy == 0) &&
	 (strlen(last_message) > 0)
	 )
	{
	  snprintf(display_server_message, 18, "%s", last_message);
	  last_message[0] = '\0';
	  display_struct.Interval = DISPLAY_NORMAL_INTERVAL;
	}
    }


}






 
