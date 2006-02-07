/*
 *  Copyright (C) 2004, Alex Ashley
 *  http://mvpmc.sourceforge.net/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
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
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>

#include <mvp_widget.h>
#include <mvp_av.h>
#include <mvp_demux.h>

#include "mvpmc.h"

#include <id3.h>
#include "display.h"

extern mvpw_menu_attr_t fb_attr;
static mvpw_menu_item_attr_t item_attr = {
	.selectable = 1,
	.fg = MVPW_BLACK,
	.bg = MVPW_LIGHTGREY,
	.checkbox_fg = MVPW_GREEN,
};

typedef enum {
	PLAYLIST_FILE_UNKNOWN,
	PLAYLIST_FILE_FILELIST
} playlist_file_t;

static char *playlist_current = NULL;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t init_control = PTHREAD_ONCE_INIT;

playlist_t *playlist=NULL, *playlist_head = NULL;

static void playlist_change(playlist_t *next);

static void select_callback(mvp_widget_t *widget, char *item, void *key)
{
  playlist_t *pl = playlist_head;

  while (pl) {
    if (pl->key == key) {
      audio_stop = 1;
      pthread_kill(audio_thread, SIGURG);
      while (audio_playing)
        usleep(1000);
      audio_clear();
      video_clear();
      av_reset();
      playlist_change(pl);
      audio_play(NULL);
      mvpw_show(fb_progress);
      break;
    }
    pl = pl->next;
  }
}

/* playlist_init sets up the pthreads mutex used by this module */
static void playlist_init()
{
  static int in_init=0;
  if(in_init){
	return;
  }
  item_attr.fg = fb_attr.fg;
  item_attr.bg = fb_attr.bg;
  in_init = 1;
  pthread_mutex_init(&mutex,NULL);
  in_init=0;
}

/* guess type of playlist from its file extension */
static int get_playlist_type(char *path)
{
	char *suffix;

	suffix = ".m3u";
	if ((strlen(path) >= strlen(suffix)) &&
	    (strcmp(path+strlen(path)-strlen(suffix), suffix) == 0))
		return PLAYLIST_FILE_FILELIST;

	return PLAYLIST_FILE_UNKNOWN;
}

/* free the memory allocated for a play list */
static void free_playlist()
{
  pthread_once(&init_control,playlist_init);
  pthread_mutex_lock(&mutex);
  playlist = playlist_head;
  while(playlist){
	/*fprintf(stderr,"free playlist %x %x\n",playlist,playlist->next); */
	playlist_t *p = playlist;
	playlist = playlist->next;
	if(p->filename){
	  free(p->filename);
	}
	if (p->name)
	  free(p->name);
	free(p);
  }
  if(playlist_current){
	free(playlist_current);
	playlist_current = NULL;
  }
  playlist_head = NULL;
  pthread_mutex_unlock(&mutex);
}

/* read one character from an input file. A temporary buffer is used
 * to speed up the reading from file. */
static inline int read_chr(int fd, char *fdbuf, int buf_sz, 
						   int *wpos, int *rpos)
{
  int sz;
  int rv;

  if(*wpos==*rpos){
	if(*wpos==buf_sz){
	  *wpos = 0;
	  *rpos = 0;
	}
	if((sz=read(fd,fdbuf+(*wpos),buf_sz-(*wpos)))<1){
	  return(-1);
	}
	*wpos += sz;
  }
  rv = fdbuf[*rpos];
  (*rpos)++;
  return(rv);
}

#define FD_BUFSZ 500

/* build a playlist from a text file that contains a list of media files,
 * one per line.
 */
static int build_playlist_from_file(const char *filename)
{
  int fd;
  char *fdbuf;
  char tmpbuf[FILENAME_MAX+1];
  int fdwpos=0;
  int fdrpos=0;
  int sz;
  int ch;
  int done=0;
  playlist_t *pl_item;
  playlist_t *pl_dest=NULL;
  int count=0;
  char *cwd;
  char *sep = NULL, *name = NULL;
  int seconds = -1;
  char *ptr;
  char buf[128];
  struct timeval start, end, delta;

  pthread_once(&init_control,playlist_init);
  pthread_mutex_lock(&mutex);

  mvpw_clear_menu(playlist_widget);

  fprintf(stderr,"building playlist from file %s\n",filename);
  gettimeofday(&start, NULL);
  if ((fd=open(filename, O_RDONLY)) < 0){
	fprintf(stderr,"unable to open playlist file %s\n",filename);
	pthread_mutex_unlock(&mutex);
	return(-1);
  }
  fdbuf = (char*)malloc(FD_BUFSZ);
  if(fdbuf==NULL){
	fprintf(stderr,"unable to malloc %d bytes\n",FD_BUFSZ);
	close(fd);
	pthread_mutex_unlock(&mutex);
	return(-1);
  }
  cwd = strdup(filename);
  for(sz=strlen(filename)-1; sz>=0; --sz){
	if ((cwd[sz]=='/') || (cwd[sz]=='\\')) {
	  cwd[sz]='\0';
	  break;
	}
  }

  sz=0;
  while(!done){
	ch = read_chr(fd, fdbuf, FD_BUFSZ, &fdwpos, &fdrpos);
	if(ch<0){
	  done=1;
	}
	if(ch<0 || ch=='\n'|| ch=='\r'){
	  ch = '\0';
	}
	tmpbuf[sz] = ch;
	sz++;
	if(ch=='\0'){
	  /*fprintf(stderr,"playlist line %d: %s %s\n",count,cwd,tmpbuf);*/

	  switch (tmpbuf[0]) {
	  case '#':
	    if ((sep=strchr(tmpbuf, ':')) != NULL) {
	      *sep = '\0';
	      seconds = atoi(sep + 1);
	      if (strcasecmp(tmpbuf+1, "extinf") == 0) {
		if ((sep=strchr(sep+1, ',')) != NULL) {
		  *sep = '\0';
		  if (name)
		    free(name);
		  name = strdup(sep + 1);
		}
	      }
	    }
	    break;
	  case '\0':
	    break;
	  default:
		pl_item = (playlist_t*)malloc(sizeof(playlist_t));
		if(pl_item){
		  if ((tmpbuf[0]=='/') || (tmpbuf[0]=='\\')) {
			pl_item->filename = strdup(tmpbuf);
			if ((ptr=strrchr(pl_item->filename, '/')) == NULL)
			  ptr = strrchr(pl_item->filename, '\\');
		  }
		  else{
			  if (strncmp(tmpbuf,"http://",7)==0) {
				  pl_item->filename = strdup(tmpbuf);
			  } 
			  else {
				  pl_item->filename = (char*)malloc(strlen(cwd)+strlen(tmpbuf)+2);
				  sprintf(pl_item->filename,"%s/%s",cwd,tmpbuf);
			  }
			  ptr = tmpbuf;
		  }
		  if (seconds)
		    pl_item->seconds = seconds;
		  else
		    pl_item->seconds = -1;
		  if (name)
		    pl_item->name = ptr = name;
		  else
		    pl_item->name = NULL;
		  pl_item->key = (void*)count;
		  pl_item->next = NULL;
		  pl_item->prev = NULL;
		  item_attr.select = select_callback;
		  if (ptr[strlen(ptr)-1] == '\r')
			  ptr[strlen(ptr)-1] = '\0';
		  mvpw_add_menu_item(playlist_widget, ptr,
				     pl_item->key, &item_attr);
		  if(pl_dest==NULL){
			playlist = pl_item;
		  }
		  else{
			pl_dest->next = pl_item;
			pl_item->prev = pl_dest;
		  }
		  pl_dest = pl_item;
		  count++;
		}
		name = NULL;
		seconds = -1;
		break;
	  }
	  sz = 0;
	}
  }
  snprintf(buf, sizeof(buf), "%s - %d files", filename, count);
  mvpw_set_menu_title(playlist_widget, buf);
  if (name)
    free(name);
  playlist_head = playlist;
  free(fdbuf);
  close(fd);
  /*fprintf(stderr,"playlist parsing done, %d items\n",count);*/
  gettimeofday(&end, NULL);
  timersub(&end, &start, &delta);
  fprintf(stderr, "playlist parsing took %ld.%.2ld seconds\n",
	  delta.tv_sec, delta.tv_usec / 10000);
  pthread_mutex_unlock(&mutex);
  return(count);
}

static void
playlist_idle(mvp_widget_t *widget)
{
  playlist_file_t playlist_type;
  static playlist_t *pl_item = NULL;
  int rc = 0;

  if (playlist == NULL && current!=NULL) {
	if(playlist_current){
	  free(playlist_current);
	}
	playlist_current = current;
	current = NULL;
	switch ((playlist_type=get_playlist_type(playlist_current))) {
	case PLAYLIST_FILE_FILELIST:
	  if((rc=build_playlist_from_file(playlist_current))<0){
		free(playlist_current);
		playlist_current=NULL;
	  }
	  break;
	case PLAYLIST_FILE_UNKNOWN:
	  return;
	  break;
	}	
	if(playlist){
		if (strncasecmp(playlist->filename,"http://",7)==0) {
			if (rc==1 ) {
				/* play first item */
				current = strdup(playlist->filename);
				audio_play(NULL);
			}
		} else {
			/* play first item */
			playlist_change(playlist);
		}
	}
	pl_item = playlist;
	mvpw_set_timer(playlist_widget, playlist_idle, 100);
  } else {
	  if (pl_item == NULL) {
		  /* done parsing ID3 info */
		  mvpw_set_timer(playlist_widget, NULL, 0);
	  } else {

		  if (strncasecmp(pl_item->filename,"http://",7)) {
			  ID3 *info;

			  info = create_ID3(NULL);
			  if (parse_file_ID3(info, (unsigned char*)pl_item->filename) == 0) {
				  char artist[64], title[64], buf[128];
				  char *p;
    
				  snprintf(artist, sizeof(artist),
					   info->artist);
				  snprintf(title, sizeof(title), info->title);
				  p = artist + strlen(artist) - 1;
				  while (*p == ' ')
					  *(p--) = '\0';
				  snprintf(buf, sizeof(buf), "%s - %s",
					   artist, title);
    
				  mvpw_menu_change_item(playlist_widget,
							pl_item->key, buf);
			  }
			  destroy_ID3(info);
		  }

		  pl_item = pl_item->next;
	  }
  }
}

void
playlist_play(mvp_widget_t *widget)
{
	playlist_idle(NULL);
}

static void playlist_change(playlist_t *next)
{
  if(!playlist){
	return;
  }
    playlist = next;

    if (playlist) {
      mvpw_menu_hilite_item(playlist_widget, playlist->key);

      /*
       * Send hilited m3u file name to display.
       */
      snprintf(display_message,DISPLAY_MESG_SIZE,"File:%s \n", playlist->filename);
    }
  if(current)
    free(current);
    current=NULL;
    if(!playlist){
      return;
    }
  printf("playlist: play item '%s', file '%s' key %d\n",
	 playlist->name, playlist->filename, (int)playlist->key);

  /*
   * Find ID3 tag information for the selected
   * MP3 file.
   */
  {
    if (strncasecmp(playlist->filename,"http://",7)) {
        int rc = 0;
    
        ID3 *info= create_ID3(NULL);
    
        if (!(info = create_ID3(info))) 
          {
    	printf("Create Failed\n");
          }
    
        if ((rc = parse_file_ID3(info, (unsigned char*)playlist->filename))) 
          {
    	printf("File: %s\n", playlist->filename);
    	printf("MP3 ID3 Failed with code %d\n", rc);
          } else {
    	if (info->mask & TITLE_TAG )
    	  printf("Title %s \n", info->title);
    	if (info->mask & ARTIST_TAG )
    	  printf("Artist %s \n", info->artist);
    	if (info->mask & ALBUM_TAG )
    	  printf("Album %s \n", info->album);
    	if (info->mask & YEAR_TAG )
    	  printf("Year %s \n", info->year);
    	if (info->mask & COMMENT_TAG )
    	  printf("Comment %s \n", info->comment);
    	if (info->mask & TRACK_TAG )
    	  printf("Track %s\n", info->track);
    	if (info->mask & GENRE_TAG )
    	  printf("Genre %s \n", info->genre);
    	printf("Version %s \n", info->version);
          }
    
        snprintf(display_message,DISPLAY_MESG_SIZE,"\nTitle:%s \nArtist:%s \nAlbum:%s \n", info->title, info->artist, info->album);
    
        display_send(display_message);

        if (destroy_ID3(info)) 
          {
    	printf("Destroy Failed\n");
          }
    }
  }

  current = strdup(playlist->filename);
  if (strncasecmp(playlist->filename,"http://",7)==0) {
	audio_play(NULL);
  } else if (is_video(current)) {
	mvpw_set_timer(root, video_play, 50);
  } else if (is_audio(current)) {
	audio_play(NULL);
  } else if (is_image(current)) {
	mvpw_set_image(iw, current);
	mvpw_lower(iw);
	mvpw_show(iw);
  } else {
  }
}

/* move to the next file in a playlist. if this is the first call,
 * play the first item in the play list 
*/
void playlist_next()
{
  if (playlist)
    playlist_change(playlist->next);
}

void playlist_prev(void)
{
  if (playlist)
    playlist_change(playlist->prev);
}

void playlist_stop(void)
{
  if (current) {
	free(current);
	current=NULL;
  }
  playlist = NULL;
}

void playlist_clear(void)
{
	if (playlist_head) {
		free_playlist();
		printf("%s(): playlist cleared\n", __FUNCTION__);
	}
}

void playlist_create(char **item, int n, char *cwd)
{
	int i;
	playlist_t *pl_head, *pl_prev, *pl;

	pthread_once(&init_control,playlist_init);
	pthread_mutex_lock(&mutex);

	mvpw_clear_menu(playlist_widget);

	pl_head = pl_prev = NULL;
	for (i=0; i<n; i++) {
		pl = (playlist_t*)malloc(sizeof(playlist_t));
		memset(pl, 0, sizeof(*pl));
		if (pl_head == NULL)
			pl_head = pl;
		pl->filename = malloc(strlen(item[i])+strlen(cwd)+2);
		snprintf(pl->filename,
			 strlen(item[i])+strlen(cwd)+2,
			 "%s/%s", cwd, item[i]);
		pl->key = (void*)i;
		pl->next = NULL;
		pl->prev = pl_prev;
		if (pl_prev)
			pl_prev->next = pl;
		pl_prev = pl;
		item_attr.select = select_callback;
		mvpw_add_menu_item(playlist_widget, item[i],
				   pl->key, &item_attr);
	}

	playlist_head = pl_head;
	playlist = pl_head;
	pthread_mutex_unlock(&mutex);

	playlist_change(playlist);
}
