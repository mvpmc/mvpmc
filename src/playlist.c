/*
 *  Copyright (C) 2004-2006, Alex Ashley
 *  http://www.mvpmc.org/
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
	PLAYLIST_FILE_FILELIST,
	PLAYLIST_FILE_PLS
} playlist_file_t;

static char *playlist_current = NULL;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t init_control = PTHREAD_ONCE_INIT;

playlist_t *playlist=NULL, *playlist_head = NULL;

int playlist_repeat = 0;

void playlist_change(playlist_t *next);
int is_streaming(char *url);

static int build_playlist_from_pls_file(const char *filename);

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
      if (playlist==NULL) {
          // playlist has finished
          playlist = pl;
      }
      mvpw_show(fb_progress);
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
	suffix = ".pls";
	if ((strlen(path) >= strlen(suffix)) &&
	    (strcmp(path+strlen(path)-strlen(suffix), suffix) == 0))
		return PLAYLIST_FILE_PLS;
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
	if (p->label)
	  free(p->label);
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
  long count=0;
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
			  if ( is_streaming(tmpbuf) >= 0 ) {
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
		  pl_item->label = strdup(ptr);
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
  snprintf(buf, sizeof(buf), "%s - %ld files", filename, count);
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
	case PLAYLIST_FILE_PLS:
		if((rc=build_playlist_from_pls_file(playlist_current))<0){
			free(playlist_current);
			playlist_current=NULL;
		}
		break;
	case PLAYLIST_FILE_UNKNOWN:
		return;
		break;
	}
	if(playlist){
		if (is_streaming(playlist->filename)>=0) {
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

		  if (is_streaming(pl_item->filename) < 0  && strstr(pl_item->filename,"uPnP")==NULL) {
			  ID3 *info;

			  info = create_ID3(NULL);
			  if (parse_file_ID3(info, (unsigned char*)pl_item->filename) == 0) {
				  char artist[64], title[64], buf[128];
				  char *p;
    
				  snprintf(artist, sizeof(artist),
					   "%s", info->artist);
				  snprintf(title, sizeof(title),
					   "%s", info->title);
				  p = artist + strlen(artist) - 1;
				  while (*p == ' ')
					  *(p--) = '\0';
				  snprintf(buf, sizeof(buf), "%s - %s",
					   artist, title);
    
				  mvpw_menu_change_item(playlist_widget,
							pl_item->key, buf);
				  if (pl_item->label)
					  free(pl_item->label);
				  pl_item->label = strdup(buf);
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

void playlist_change(playlist_t *next)
{
  if(!playlist){
	return;
  }
  pthread_mutex_lock(&mutex);

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
	    goto out;
    }
  printf("playlist: play item '%s', file '%s' key %ld\n",
	 playlist->name, playlist->filename, (long)playlist->key);

  /*
   * Find ID3 tag information for the selected
   * MP3 file.
   */
  {
    if (is_streaming (playlist->filename) < 0 && strstr(playlist->filename,"uPnP")==NULL) {
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
  if (is_streaming(playlist->filename)>=0) {
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

 out:
  pthread_mutex_unlock(&mutex);
}

/* move to the next file in a playlist. if this is the first call,
 * play the first item in the play list 
*/
void playlist_next()
{
	if (playlist) {
		playlist_t *next = playlist->next;

		if ((next == NULL) && (playlist_repeat))
			next = playlist_head;

		playlist_change(next);
	}
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
		playlist_repeat = 0;
		mvpw_check_menu_item(pl_menu, (void*)PL_REPEAT, 0);
	}
}

void playlist_create(char **item, int n)
{
	long i;
	playlist_t *pl_head, *pl_prev, *pl;

	pthread_once(&init_control,playlist_init);
	pthread_mutex_lock(&mutex);

	mvpw_clear_menu(playlist_widget);
	playlist_repeat = 1;
	mvpw_check_menu_item(pl_menu, (void*)PL_REPEAT, playlist_repeat);

	pl_head = pl_prev = NULL;
	for (i=0; i<n; i++) {
		pl = (playlist_t*)malloc(sizeof(playlist_t));
		memset(pl, 0, sizeof(*pl));
		if (pl_head == NULL)
			pl_head = pl;
		pl->filename = malloc(strlen(item[i])+1);
		snprintf(pl->filename,
			 strlen(item[i])+1,
			 "%s", item[i]);
		pl->key = (void*)i;
		pl->next = NULL;
		pl->prev = pl_prev;
		if (pl_prev)
			pl_prev->next = pl;
		pl_prev = pl;

		char* trackname = NULL;

		// Try and parse ID3 tag information (obviously,
		// this fails for non-mp3 files)
		ID3 *info;
		info = create_ID3(NULL);
		if (parse_file_ID3(info, (unsigned char*) item[i]) == 0) {
			trackname = (char*) malloc(sizeof(char) * 1024);
			snprintf(trackname, 1024, "%s - %s / %s (%s)", info->track, info->title, info->album, info->artist);
		}
		else {
			// Fall back to the filename - no tag
			char* filename = strrchr(item[i], '/');
			if (filename == NULL) 
				filename = item[i];
			else
				filename++;
			trackname = strdup(filename);
		}

		item_attr.select = select_callback;
		mvpw_add_menu_item(playlist_widget, trackname,
				   pl->key, &item_attr);
		if (pl->label)
			free(pl->label);
		pl->label = strdup(trackname);

		destroy_ID3(info);
		free(trackname);
	}

	playlist_head = pl_head;
	playlist = pl_head;
	pthread_mutex_unlock(&mutex);

	playlist_change(playlist);
}

void
playlist_randomize(void)
{
	playlist_t *cur, *swap, *hilite = NULL;
	long i, j, count, r, old;

	pthread_mutex_lock(&mutex);

	if (playlist_head == NULL) {
		goto out;
	}

	old = (long)mvpw_menu_get_hilite(playlist_widget);

	count = 0;
	cur = playlist_head;
	while (cur) {
		if (old == count)
			hilite = cur;
		cur = cur->next;
		count++;
	}

	if (hilite == NULL)
		goto out;

	cur = playlist_head;
	for (i=0; i<count; i++) {
		int swapped = 0;

		swap = cur;
		r = rand() % count;
		for (j=0; j<r; j++) {
			if (swap->next == NULL) {
				swap = playlist_head;
			} else {
				swap = swap->next;
			}
		}
		if (swap != cur) {
			playlist_t tmp;

			memcpy(&tmp, cur, sizeof(*cur));

			cur->filename = swap->filename;
			cur->name = swap->name;
			cur->label = swap->label;
			cur->seconds = swap->seconds;

			swap->filename = tmp.filename;
			swap->name = tmp.name;
			swap->label = tmp.label;
			swap->seconds = tmp.seconds;

			if ((cur == hilite) && !swapped) {
				hilite = swap;
				swapped = 1;
			}
			if ((swap == hilite) && !swapped) {
				hilite = cur;
				swapped = 1;
			}
		}
		cur = cur->next;
	}

	mvpw_clear_menu(playlist_widget);

	cur = playlist_head;
	for (i=0; i<count; i++) {
		item_attr.select = select_callback;
		cur->key = (void*)i;
		mvpw_add_menu_item(playlist_widget, cur->label,
				   cur->key, &item_attr);
		if (hilite == cur)
			old = i;
		cur = cur->next;
	}

	mvpw_menu_hilite_item(playlist_widget, (void*)old);
	playlist = hilite;

 out:
	pthread_mutex_unlock(&mutex);
}

static int build_playlist_from_pls_file(const char *filename)
{
	int fd;
	char *fdbuf;
	char tmpbuf[FILENAME_MAX+1];
	char pls_filename[FILENAME_MAX];
	char pls_title[FILENAME_MAX];
	int fdwpos=0;
	int fdrpos=0;
	int sz;
	int ch;
	int done=0;
	playlist_t *pl_item;
	playlist_t *pl_dest=NULL;
	long count=0;
	char *cwd;
	int seconds = -1;
	char *ptr;
	char buf[128];
	struct timeval start, end, delta;

	pthread_once(&init_control,playlist_init);
	pthread_mutex_lock(&mutex);

	mvpw_clear_menu(playlist_widget);

	fprintf(stderr,"building playlist from file %s\n",filename);
	gettimeofday(&start, NULL);
	if ((fd=open(filename, O_RDONLY)) < 0) {
		fprintf(stderr,"unable to open playlist file %s\n",filename);
		pthread_mutex_unlock(&mutex);
		return(-1);
	}
	fdbuf = (char*)malloc(FD_BUFSZ);
	if (fdbuf==NULL) {
		fprintf(stderr,"unable to malloc %d bytes\n",FD_BUFSZ);
		close(fd);
		pthread_mutex_unlock(&mutex);
		return(-1);
	}
	cwd = strdup(filename);
	for (sz=strlen(filename)-1; sz>=0; --sz) {
		if ((cwd[sz]=='/') || (cwd[sz]=='\\')) {
			cwd[sz]='\0';
			break;
		}
	}
	pls_title[0] = 0;
	seconds = 0;
	pls_filename[0] = 0;
	sz=0;
	while (!done) {
		ch = read_chr(fd, fdbuf, FD_BUFSZ, &fdwpos, &fdrpos);
		if (ch<0) {
			done=1;
		}
		if (ch<0 || ch=='\n'|| ch=='\r') {
			ch = '\0';
		}
		tmpbuf[sz] = ch;
		if (ch=='\0') {
			if (sz==0) {
				continue;
			}
			sz = 0;
			if (!*pls_filename && !strncasecmp(tmpbuf, "File", 4)) {
				char *p = strchr(tmpbuf, '=');
				if (p++){
					snprintf(pls_filename,FILENAME_MAX,"%s", p);
				}
				pls_title[0] = 0;
				seconds = 0;
				continue;
			}
			if (*pls_filename && !*pls_title && !strncasecmp(tmpbuf, "Title", 5)) {
				char *p = strchr(tmpbuf, '=');
				if (p++)
					snprintf(pls_title,FILENAME_MAX,"%s", p);
				continue;
			}
			if (*pls_title && !seconds && !strncasecmp(tmpbuf, "Length", 6)) {
				char *p = strchr(tmpbuf, '=');
				if (p++)
					seconds = atoi(p);
				pl_item = (playlist_t*)malloc(sizeof(playlist_t));
				if (pl_item) {
					if ((pls_filename[0]=='/') || (pls_filename[0]=='\\')) {
						pl_item->filename = strdup(pls_filename);
					} else {
						if ( is_streaming(pls_filename) >= 0 ) {
							pl_item->filename = strdup(pls_filename);
						} else {
							pl_item->filename = (char*)malloc(strlen(cwd)+strlen(pls_filename)+2);
							sprintf(pl_item->filename,"%s/%s",cwd,pls_filename);
						}
					}
					if (seconds)
						pl_item->seconds = seconds;
					else
						pl_item->seconds = -1;
					pl_item->name = strdup(pls_title);
					pl_item->key = (void*)count;
					pl_item->next = NULL;
					pl_item->prev = NULL;
					item_attr.select = select_callback;
					ptr = pl_item->name;
					if (ptr[strlen(ptr)-1] == '\r')
						ptr[strlen(ptr)-1] = '\0';
					mvpw_add_menu_item(playlist_widget, pl_item->name,
							   pl_item->key, &item_attr);
					pl_item->label = strdup(pl_item->name);
					if (pl_dest==NULL) {
						playlist = pl_item;
					} else {
						pl_dest->next = pl_item;
						pl_item->prev = pl_dest;
					}
					pl_dest = pl_item;
					count++;
				}
				pls_title[0] = 0;
				seconds = 0;
				pls_filename[0] = 0;
			}
//			fprintf(stderr,"playlist line %d: %s %s\n",count,cwd,tmpbuf);
		} else {
			sz++;
		}
	}
	snprintf(buf, sizeof(buf), "%s - %ld files", filename, count);
	mvpw_set_menu_title(playlist_widget, buf);
	playlist_head = playlist;
	free(fdbuf);
	close(fd);
//	fprintf(stderr,"playlist parsing done, %d items\n",count);
	gettimeofday(&end, NULL);
	timersub(&end, &start, &delta);
	fprintf(stderr, "playlist parsing took %ld.%.2ld seconds\n",
		delta.tv_sec, delta.tv_usec / 10000);
	pthread_mutex_unlock(&mutex);
	return(count);
}
