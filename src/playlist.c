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
#include <fcntl.h>
#include <string.h>
#include <pthread.h>

#include <mvp_widget.h>
#include <mvp_av.h>

#include "mvpmc.h"

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
      audio_clear();
      av_reset();
      playlist_change(pl);
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

  pthread_once(&init_control,playlist_init);
  pthread_mutex_lock(&mutex);

  mvpw_set_menu_title(playlist_widget, filename);
  mvpw_clear_menu(playlist_widget);

  fprintf(stderr,"building playlist from file %s\n",filename);
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
	if(ch<0 || ch=='\n'){
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
		  printf("EXTINF: name '%s'\n", name);
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
			pl_item->filename = (char*)malloc(strlen(cwd)+strlen(tmpbuf)+2);
			sprintf(pl_item->filename,"%s/%s",cwd,tmpbuf);
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
  if (name)
    free(name);
  playlist_head = playlist;
  free(fdbuf);
  close(fd);
  /*fprintf(stderr,"playlist parsing done, %d items\n",count);*/
  pthread_mutex_unlock(&mutex);
  return(count);
}

static void
playlist_idle(void)
{
  playlist_file_t playlist_type;

  /* make sure we only get called once */
  mvpw_set_idle(NULL);

  if (playlist == NULL && current!=NULL) {
	if(playlist_current){
	  free(playlist_current);
	}
	playlist_current = current;
	current = NULL;
	switch ((playlist_type=get_playlist_type(playlist_current))) {
	case PLAYLIST_FILE_FILELIST:
	  if(build_playlist_from_file(playlist_current)<0){
		free(playlist_current);
		playlist_current=NULL;
	  }
	  break;
	case PLAYLIST_FILE_UNKNOWN:
	  mvpw_set_idle(NULL);
	  return;
	  break;
	}
	
	if(playlist){
	  /* play first item */
	  playlist_change(playlist);
	}
  }
}

void
playlist_play(mvp_widget_t *widget)
{
	mvpw_set_idle(playlist_idle);
	mvpw_set_timer(root, NULL, 0);
}

static void playlist_change(playlist_t *next)
{
  if(!playlist){
	return;
  }
  if (current){
    playlist = next;

    if (playlist) {
      mvpw_menu_hilite_item(playlist_widget, playlist->key);
    }
    free(current);
    current=NULL;
    if(!playlist){
      return;
    }
  }
  printf("playlist: play item '%s', file '%s'\n",
	 playlist->name, playlist->filename);
  current = strdup(playlist->filename);
  if (is_video(current)) {
	mvpw_set_timer(root, video_play, 50);
  } else if (is_audio(current)) {
	mvpw_set_timer(root, audio_play, 50);
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
  /* todo: call audio_clear or video_clear if playing*/
  free_playlist();
}

