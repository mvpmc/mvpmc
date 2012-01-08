/*
 *  Copyright (C) 2004, 2005, 2006, Jon Gettler
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
#include <ctype.h>
#include <unistd.h>
#include <dirent.h>
#include <glob.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <pthread.h>
#include <limits.h>

#include <mvp_widget.h>
#include <mvp_av.h>
#include <mvp_demux.h>

#include "mvpmc.h"
#include "display.h"
#include "web_config.h"

extern mvpw_text_attr_t display_attr;
extern mvpw_menu_attr_t fb_attr;
static mvpw_menu_item_attr_t item_attr = {
	.selectable = true,
	.fg = MVPW_BLACK,
	.bg = MVPW_LIGHTGREY,
	.checkbox_fg = MVPW_GREEN,
};

char cwd[1024] = "/";

static void add_dirs(mvp_widget_t*);
static void add_files(mvp_widget_t*);

char *current = NULL;
char *current_hilite = NULL;
extern char *vlc_server;

static char *current_pl = NULL;

static long int file_count = 0;
static long int dir_count = 0;

int loaded_offset = 0;
int loaded_status = 0;

int mount_djmount(char *);
int unmount_djmount(void);
int set_route(int rtwin);

int
is_video(char *item)
{
	char *wc[] = { ".mpg", ".mpeg", ".nuv", ".ts", ".vob", NULL };
	int i = 0;

	while (wc[i] != NULL) {
		if ((strlen(item) >= strlen(wc[i])) &&
		    (strcasecmp(item+strlen(item)-strlen(wc[i]), wc[i]) == 0))
			return 1;
		i++;
	}

	return 0;
}

int
is_image(char *item)
{
	char *wc[] = { ".bmp", ".gif", ".jpg", ".jpeg", ".png", NULL };
	int i = 0;

	while (wc[i] != NULL) {
		if ((strlen(item) >= strlen(wc[i])) &&
		    (strcasecmp(item+strlen(item)-strlen(wc[i]), wc[i]) == 0))
			return 1;
		i++;
	}

	return 0;
}

int
is_audio(char *item)
{
	char *wc[] = { ".mp3", ".ogg", ".wav", ".ac3", ".flac", ".fla", NULL };
	int i = 0;

	while (wc[i] != NULL) {
		if ((strlen(item) >= strlen(wc[i])) &&
		    (strcasecmp(item+strlen(item)-strlen(wc[i]), wc[i]) == 0))
			return 1;
		i++;
	}

	return 0;
}

static int
is_playlist(char *item)
{
	char *wc[] = { ".m3u",".pls", NULL };
	int i = 0;

	while (wc[i] != NULL) {
		if ((strlen(item) >= strlen(wc[i])) &&
		    (strcasecmp(item+strlen(item)-strlen(wc[i]), wc[i]) == 0))
			return 1;
		i++;
	}

	return 0;
}

static void
fb_osd_update(mvp_widget_t *widget)
{
	av_stc_t stc;
	char buf[256];
	struct stat64 sb;
	long long offset;
	int percent;

	if (fstat64(fd, &sb) < 0)
		return;

	av_current_stc(&stc);
	snprintf(buf, sizeof(buf), "%.2d:%.2d:%.2d",
		 stc.hour, stc.minute, stc.second);
	mvpw_set_text_str(fb_time, buf);

	snprintf(buf, sizeof(buf), "Bytes: %lld", (long long)sb.st_size);
	mvpw_set_text_str(fb_size, buf);

	offset = lseek(fd, 0, SEEK_CUR);
	percent = (int)((double)(offset/1000) /
		    (double)(sb.st_size/1000) * 100.0);

	snprintf(buf, sizeof(buf), "%d%%", percent);
	mvpw_set_text_str(fb_offset_widget, buf);
	mvpw_set_graph_current(fb_offset_bar, percent);
	mvpw_expose(fb_offset_bar);
}
int is_streaming(char *url);

static void
select_callback(mvp_widget_t *widget, char *item, void *key)
{
	char path[1024], *ptr;
	struct stat64 sb;

	sprintf(path, "%s/%s", cwd, item);
	if (stat64(path, &sb)!=0) {
		printf("Could not stat %s error %d\n",item,errno);
		if (strcmp(item,"../")==0 ) {
			// probably lost network put you back in root
			strcpy(cwd,"/");
			strcpy(path,"/");
			stat64(path, &sb);
		}
	}

	if (current_pl && !is_playlist(item)) {
		free(current_pl);
		current_pl = NULL;
	}

	if (current_pl && (playlist == NULL)) {
		free(current_pl);
		current_pl = NULL;
	}

	printf("%s(): path '%s'\n", __FUNCTION__, path);

	if (current && (strcmp(path, current) == 0)) {
		printf("selected current item\n");
		if (is_video(item) || (is_streaming(item) > 100)) {
			mvpw_hide(widget);
			mvpw_hide(fb_progress);
			av_move(0, 0, 0);
			screensaver_disable();
			return;
		}
	}

	if (current_pl && (strcmp(path, current_pl) == 0)) {
		if (is_playlist(item)) {
			mvpw_show(fb_progress);
			mvpw_set_timer(fb_progress, fb_osd_update, 500);
			mvpw_hide(widget);
			printf("Show playlist menu\n");
			mvpw_show(playlist_widget);
			mvpw_focus(playlist_widget);
			return;
		}
	}

	if (S_ISDIR(sb.st_mode)) {
		if (strcmp(item, "../") == 0) {
			strcpy(path, cwd);
			if (path[strlen(path)-1] == '/')
				path[strlen(path)-1] = '\0';
			if ((ptr=strrchr(path, '/')) != NULL)
				*ptr = '\0';
			if (path[0] == '\0')
				sprintf(path, "/");
		} else {
			if ((ptr=strrchr(path, '/')) != NULL)
				*ptr = '\0';
		}
		if (strstr(path,"/uPnP")!=NULL && strstr(cwd,"/uPnP")==NULL ){
			mount_djmount(path);
				
		} else if (strstr(path,"/uPnP")==NULL && strstr(cwd,"/uPnP")!=NULL ) { 
			unmount_djmount();
		}
		strncpy(cwd, path, sizeof(cwd));

		while ((cwd[0] == '/') && (cwd[1] == '/'))
			memmove(cwd, cwd+1, strlen(cwd));

		mvpw_clear_menu(widget);
		mvpw_set_menu_title(widget, cwd);

		busy_start();
		add_dirs(widget);
		add_files(widget);
		busy_end();

		mvpw_expose(widget);
	} else {
		switch_hw_state(MVPMC_STATE_FILEBROWSER);

		if (current)
			free(current);
		current = NULL;
		audio_stop = 1;
		pthread_kill(audio_thread, SIGURG);

		while (audio_playing)
			usleep(1000);

		current = strdup(path);

		if (is_streaming(item) > 100) {
			// Use VLC callbacks for streaming items
			video_functions = &vlc_functions;
			// Allow broadcast messages to be sent so
			// we can tell VLC to start the stream
			vlc_broadcast_enabled = 1;
		} else {
			video_functions = &file_functions;
		}

		add_osd_widget(fb_program_widget, OSD_PROGRAM,
			       osd_settings.program, NULL);

		mvpw_set_text_str(fb_name, item);

		/*
		 * This code sends the currently playing file name to the display.
		 */
		snprintf(display_message, sizeof(display_message),
			 "File:%s\n", item);
		display_send(display_message);

		audio_clear();
		video_clear();
		playlist_clear();

		if (is_video(item)) {
			if (key != NULL) {
				mvpw_hide(widget);
				mvpw_hide(fb_progress);
				av_move(0, 0, 0);
			} else {
				mvpw_show(fb_progress);
			}
			mvpw_set_timer(fb_progress, fb_osd_update, 500);
			video_play(NULL);
			mvpw_show(root);
			mvpw_expose(root);
			mvpw_focus(root);
		} else if (is_audio(item) || is_streaming(item)>=0 ) {
			mvpw_show(fb_progress);
			mvpw_set_timer(fb_progress, fb_osd_update, 500);
			audio_play(NULL);
		} else if (is_image(item)) {
			mvpw_hide(widget);
			printf("Displaying image '%s'\n", path);
			if (mvpw_load_image_jpeg(iw, path) == 0) {
				mvpw_show_image_jpeg(iw);
				av_wss_update_aspect(WSS_ASPECT_UNKNOWN);
			} else {
				mvpw_set_image(iw, path);
			}
			mvpw_show(iw);
			mvpw_focus(iw);
			loaded_offset = 0;
			loaded_status = 0;
			fb_next_image(1);
		} else if (is_playlist(item)) {
			if (current_pl)
				free(current_pl);
			current_pl = strdup(path);
			mvpw_show(fb_progress);
			mvpw_set_timer(fb_progress, fb_osd_update, 500);
			mvpw_hide(widget);
			printf("Show playlist menu\n");
			mvpw_show(playlist_widget);
			mvpw_focus(playlist_widget);
			playlist_clear();
			playlist_play(NULL);
		}
	}
}

void
fb_start_thumbnail(void)
{
	select_callback(file_browser, current_hilite, NULL);
}

static void
hilite_callback(mvp_widget_t *widget, char *item, void *key, bool hilite)
{
	char path[1024], str[1024], date[64];
	struct stat64 sb;

	if (hilite) {
		sprintf(path, "%s/%s", cwd, item);

		stat64(path, &sb);
		ctime_r(&sb.st_mtime, date);
		snprintf(str, sizeof(str),
			 "File: %s\nSize: %lld\nDate: %s",
			 item, (long long)sb.st_size, date);

		mvpw_set_text_str(fb_program_widget, str);

		if (current_hilite)
			free(current_hilite);
		current_hilite = strdup(item);

		/*
		 * Send the currently hilighted text to the display.
		 */
		snprintf(display_message, sizeof(display_message),
			 "File:%s\n", current_hilite);
		display_send(display_message);
	}
}

static void
add_dirs(mvp_widget_t *fbw)
{
	glob_t gb;
	char pattern[1024], buf[1024], *ptr;
	int i;
	struct stat64 sb;

	item_attr.select = select_callback;
	item_attr.hilite = hilite_callback;
	item_attr.fg = fb_attr.fg;
	item_attr.bg = fb_attr.bg;
	mvpw_add_menu_item(fbw, "../", 0, &item_attr);

	memset(&gb, 0, sizeof(gb));
	snprintf(pattern, sizeof(pattern), "%s/*", cwd);

	dir_count = 1;
	if (glob(pattern, GLOB_ONLYDIR, NULL, &gb) == 0) {
		i = 0;
		while (gb.gl_pathv[i]) {
			stat64(gb.gl_pathv[i], &sb);
			if (S_ISDIR(sb.st_mode)) {
				if ((ptr=strrchr(gb.gl_pathv[i], '/')) == NULL)
					ptr = gb.gl_pathv[i];
				else
					ptr++;
				snprintf(buf, sizeof(buf)/sizeof(*buf) - 1,
					 "%s", ptr);
				strcat(buf, "/");
				mvpw_add_menu_item(fbw, buf,
						   (void*)dir_count++,
						   &item_attr);
			}
			i++;
		}
	}
	globfree(&gb);
}

static int
do_glob(mvp_widget_t *fbw, char *wc[])
{
	int w, i = 0;
	struct stat64 sb;
	glob_t gb;
	char pattern[1024], *ptr;

	w = 0;
	while (wc[w] != NULL) {
		memset(&gb, 0, sizeof(gb));
		snprintf(pattern, sizeof(pattern), "%s/%s", cwd, wc[w]);
		if (glob(pattern, 0, NULL, &gb) == 0) {
			i = 0;
			while (gb.gl_pathv[i]) {
				stat64(gb.gl_pathv[i], &sb);
				if ((ptr=strrchr(gb.gl_pathv[i], '/')) == NULL)
					ptr = gb.gl_pathv[i];
				else
					ptr++;
				mvpw_add_menu_item(fbw, ptr,
						   (void*)(dir_count+
							   file_count++),
						   &item_attr);
				i++;
			}
		}
		globfree(&gb);
		w++;
	}

	return i;
}

static void
add_files(mvp_widget_t *fbw)
{
	char *wc[] = { "*.mpg", "*.mpeg", "*.mp3", "*.nuv", "*.vob", "*.gif",
			"*.bmp", "*.m3u", "*.pls", "*.jpg", "*.jpeg", "*.png", "*.wav",
			"*.ac3", "*.ogg", "*.ts", "*.flac", ".fla", NULL };
	char *WC[] = { "*.MPG", "*.MPEG", "*.MP3", "*.NUV", "*.VOB", "*.GIF",
			"*.BMP", "*.M3U", "*.JPG", "*.JPEG", "*.PNG", "*.WAV",
			"*.AC3", "*.OGG", "*.TS", "*.FLAC", "*.FLA", NULL };


	item_attr.select = select_callback;
	item_attr.hilite = hilite_callback;
	item_attr.fg = fb_attr.fg;
	item_attr.bg = fb_attr.bg;

	file_count = 0;
	do_glob(fbw, wc);
	do_glob(fbw, WC);
	if (vlc_server!=NULL) {
		char *vlc[] = { "*.divx", "*.DIVX", "*.flv", "*.FLV", "*.avi", "*.AVI", "*.wmv",
			"*.WMV", "*.wma", "*.WMA", "*.mp4", "*.MP4",
 			"*.mov", "*.MOV",
			"*.mkv", "*.MKV",
			"*.rm", "*.RM", "*.ogm", "*.OGM", "*.iso", "*.ISO", NULL };
		do_glob(fbw, vlc);
	}
}

int
fb_update(mvp_widget_t *fb)
{
	mvpw_show(root);
	mvpw_expose(root);

	mvpw_clear_menu(fb);
	mvpw_set_menu_title(fb, cwd);

	busy_start();
	if (strstr(cwd,"/uPnP")!=NULL ){
		mount_djmount(cwd);
	}
	add_dirs(fb);
	add_files(fb);
	busy_end();

	return 0;
}

void
fb_program(mvp_widget_t *widget)
{
	/*
	 * Nothing to do here...
	 */
}

void
fb_exit(void)
{
	audio_stop = 1;
	audio_clear();

	pthread_kill(audio_thread, SIGURG);

	if (current) {
		free(current);
		current = NULL;
	}
	mvpw_hide(fb_progress);

	video_clear();
	av_stop();
	if (!mvpw_visible(playlist_widget))
		playlist_clear();
}

static void 
recurse_find_audio(char* cur, char** item, int* nextitem)
{
	DIR *d = NULL;
	struct dirent *de = NULL;
	struct stat64 sb;
	int i;
        char comp1[PATH_SIZE];
        char comp2[PATH_SIZE];
	char curdir[PATH_SIZE];

	// Append a trailing slash to the current path
	snprintf(curdir, PATH_SIZE, "%s/", cur);
	printf("Recursing directory %s for audio files\n", curdir);

	d = opendir(curdir);
	if (d == NULL) {
		printf("Not a directory: %s", curdir);
		return;
	}

	int noitems = 0;
	char** contents = alloca(sizeof(char*) * MAX_PLAYLIST_ENTRIES);
	while ( (de = readdir(d)) != NULL && noitems < MAX_PLAYLIST_ENTRIES) {

		// Ignore ./.. or blank name directories
		if (*de->d_name == '.' || *de->d_name == '/' || *de->d_name == '\0') {
			continue;
		}

		char path[PATH_SIZE];
		snprintf(path, PATH_SIZE, "%s/%s", cur, de->d_name);

		if (stat64(path, &sb) != 0) {
			printf("Couldn't stat file: %s\n", path);
			return;
		}
		else if (S_ISDIR(sb.st_mode)) {
			// It's a directory, recurse it
			printf("Found directory %s, recursing...\n", path);
			recurse_find_audio(path, item, nextitem);
		}
		else if (is_audio(de->d_name)) {
			// We have an audio file, add it to the list
			printf("Found audio file %s, adding as item %d\n", path, noitems);
			contents[noitems++] = strdup(path);
			printf("Next item is %d\n",  noitems);
		}
		else {
			printf("Skipping non-audio file %s\n", path);
		}
	}
	closedir(d);

        // Sort the directory contents via bubblesort (space efficient - 
        // everything else inefficient :-)
        printf("Sorting directory contents\n");
        int sorted = 0;
        while (!sorted) {
            sorted = 1;
            for (i = 0; i < noitems-1; i++) {
               
                // Create a copy of our two items to compare. If
                // they start with a single digit by itself, then prepend
                // a zero for alphanumeric comparison - we're guessing the 
                // filename must start with a track number and we'd like 
                // numbers < 10 to order correctly to be below numbers > 10.
		char* fname1 = (char*) strrchr(contents[i], '/');
		char* fname2 = (char*) strrchr(contents[i+1], '/');
		fname1++;
		fname2++;

                if (isdigit(*fname1) && !isdigit(*(fname1 + 1)))
                    sprintf(comp1, "0%s", fname1);
                else
                    strcpy(comp1, fname1);
                if (isdigit(*fname2) && !isdigit(*(fname2 + 1)))
                    sprintf(comp2, "0%s", fname2);
                else
                    strcpy(comp2, fname2);
            
                if (strcmp(comp1, comp2) > 0) {
                    printf("SWAP: %s with %s\n", comp1, comp2);
                    char* tmp = contents[i];
                    contents[i] = contents[i+1];
                    contents[i+1] = tmp;
                    sorted = 0;
                }
                else {
                    printf("NOSWAP: %s with %s\n", comp1, comp2);
                }
            }
        }

        // Append the sorted contents to our list of items
        printf("Adding sorted contents to item list\n");
        for (i = 0; i < noitems; i++) {

            // If we've hit the limit of the playlist size, free up
            // directory entries from now on as we can't reassign them
            // to the playlist (where they would be freed)
            if (*nextitem == MAX_PLAYLIST_ENTRIES) {
                free(contents[i]);
                continue;
            }

            // Add the item to the playlist
            item[*nextitem] = contents[i];
            printf("playlist item %d: %s\n", *nextitem, contents[i]);
            (*nextitem)++;

        }

}

void
fb_shuffle(int shuffle)
{
	char **item, *tmp;
	int i, j, k, n;

	if (shuffle)
		mvpw_set_menu_title(playlist_widget, "Shuffle Play");
	else
		mvpw_set_menu_title(playlist_widget, "Play All");
	
	item = alloca(sizeof(char*) * MAX_PLAYLIST_ENTRIES);

	// Recurse from the current directory and find all
	// audio files
	n = 0;
	recurse_find_audio(cwd, item, &n);
	
	if (n == 0) {
		gui_error("No audio files exist in this directory or its subdirectories");
		return;
	}

	if (shuffle && (n > 1)) {
		for (i=0; i < MAX_PLAYLIST_ENTRIES; i++) {
			j = rand() % n;
			k = rand() % n;
			tmp = item[k];
			item[k] = item[j];
			item[j] = tmp;
		}
	}

	printf("created playlist of %d songs\n", n);

	switch_hw_state(MVPMC_STATE_FILEBROWSER);
	video_functions = &file_functions;

	playlist_clear();

	mvpw_show(playlist_widget);
	mvpw_focus(playlist_widget);

	playlist_create(item, n);

	// Release the list of items
	for (i = 0; i < n; i++)
		free(item[i]);

	if (shuffle)
		mvpw_set_text_str(fb_name, "Shuffle Play");
	else
		mvpw_set_text_str(fb_name, "Play All");

	mvpw_show(fb_progress);
	mvpw_set_timer(fb_progress, fb_osd_update, 500);
	playlist_play(NULL);
}

void
fb_thruput(void)
{
	char path[256];

	switch_hw_state(MVPMC_STATE_FILEBROWSER);

	sprintf(path, "%s/%s", cwd, current_hilite);

	if (current)
		free(current);
	current = strdup(path);

	video_functions = &file_functions;

	demux_reset(handle);
	demux_attr_reset(handle);
	video_play(NULL);
}

int
fb_next_image(int offset)
{
	char path[1024];
	char *label;
	long int c, i, n, o;

	if( offset == 0 ) {
		if( loaded_offset ) {
			offset = loaded_offset;
		} else {
			loaded_status = mvpw_load_image_jpeg(iw, current);
			av_wss_update_aspect(WSS_ASPECT_UNKNOWN);
			offset = 1;
			loaded_offset = 1;
		}
	}

	if( loaded_offset ) {
		if( offset != loaded_offset ) {
			if( offset == INT_MIN || offset == INT_MAX ) {
				o = offset;
			} else {
				o = offset-loaded_offset;
			}
			loaded_offset = 0;
			fb_next_image(o);
		}
		printf("Displaying image '%s'\n", current);
		if( loaded_status == -1 ) {
			mvpw_set_image(iw, current);
		} else {
			mvpw_show_image_jpeg(iw);
		}
	}

	loaded_offset = offset;
	o = 0;
	n = 0;
	c = 0;
	while( (label = mvpw_get_menu_item(file_browser, (void*)n)) ) {
		if( current && strcasecmp( current+strlen(cwd)+1, label) == 0 ) c = n;
		n++;
	}
	if( offset == INT_MIN ) {
		i = 0;
		while( (label = mvpw_get_menu_item(file_browser, (void*)i++)) )
			if( is_image(label) ) break;
	 } else if( offset == INT_MAX ) {
		i = n;
		while( (label = mvpw_get_menu_item(file_browser, (void*)i--)) )
			if( is_image(label) ) break;
	} else {
		if( offset>0 ) o = 1;
		if( offset<0 ) o = -1;
		i = (c+o+n)%n;
		while( (label = mvpw_get_menu_item(file_browser, (void*)i)) && (i!=c) ) {
			if( is_image(label) ) offset-=o;
			if( offset == 0 ) break;
			i = (i+o+n)%n;
		}
	}
	if( label == NULL )
		return -1;
	sprintf(path, "%s/%s", cwd, label);
	free(current);
	current = strdup(path);
	loaded_status = mvpw_load_image_jpeg(iw, current);
	av_wss_update_aspect(WSS_ASPECT_UNKNOWN);

	return 0;
}


int mount_djmount(char *mycwd)
{
	int rc=-1;
	if (strstr(mycwd,"/uPnP")!=NULL ) {
		set_route(1448*4);
		printf("Mounting %s\n",mycwd);
		av_set_audio_output(AV_AUDIO_CLOSE);
		if (fork()==0) {
			if (display_attr.utf8 == 1 ) {
				rc = execlp("/bin/djmount","djmount","-o", "iocharset=utf8","/uPnP",(char*)0);
			} else {
				rc = execlp("/bin/djmount","djmount","/uPnP",(char *)0);
			}
		}
		busy_start();
		usleep(2500000);
		busy_end();
		mvpw_expose(file_browser);
		av_set_audio_output(AV_AUDIO_MPEG);
	}
	return rc;
}
int unmount_djmount(void)
{
	int rc = -1;
	printf("Unmounting %s\n",cwd);
	if (web_config->fs_rtwin !=-1) {
		set_route(web_config->fs_rtwin);
	} else if (web_config->rtwin !=-1) {
		set_route(web_config->rtwin);
	} else {
		set_route(0);
	}
	if (fork()==0) {
		rc = execlp("/bin/fusermount","fusermount","-u","/uPnP",(char *)0);
	}
	return rc;
}

int quickdir_change(mvp_widget_t *widget,char *path )
{
	int rc;
	struct stat64 sb;
	rc = stat64(path, &sb);
	if (rc==0) {
		if (strstr(path,"/uPnP")!=NULL && strstr(cwd,"/uPnP")==NULL ){
			mount_djmount(path);
				
		} else if (strstr(path,"/uPnP")==NULL && strstr(cwd,"/uPnP")!=NULL ) { 
			unmount_djmount();
		}
		snprintf(cwd,1024,"%s",path);
		fb_update(widget);
	}
	return rc;
}


