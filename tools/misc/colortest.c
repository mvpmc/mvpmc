/*
 *  Copyright (C) 2004, John Honeycutt
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
#include <string.h>
#include <pthread.h>

#define MWINCLUDECOLORS
#include "nano-X.h"
#include "nxcolors.h"

#include <mvp_widget.h>
#include <mvp_av.h>
#include <mvp_demux.h>
#include <mvp_osd.h>

#include "colortest.h"

#include "a52dec/a52.h"
#include "a52dec/mm_accel.h"

#define INCR_COLOR(cpos, cincr) ( (((cpos)+(cincr)) < 0) ? (num_colors-1) : (((cpos)+(cincr)) % (num_colors))  )
const int num_colors = sizeof(color_list) / sizeof(color_info);

const char *start_fg_color;
const char *start_bg_color;

int fontid;
char *saved_argv[32];
int saved_argc = 0;
mvpw_screen_info_t si;

static mvpw_text_attr_t text_box_attr = {
	.wrap = 1,
	.justify = MVPW_TEXT_CENTER,
	.margin = 6,
	.font = 0,
	.fg = MVPW_GREEN,
};

static mvpw_text_attr_t fgbg_box_attr = {
	.wrap = 1,
	.justify = MVPW_TEXT_LEFT,
	.margin = 6,
	.font = 0,
	.fg = MVPW_GREEN,
};

mvp_widget_t        *root;
static mvp_widget_t *text_box;
static mvp_widget_t *fg_box;
static mvp_widget_t *bg_box;

int h, w;
char buf[255], fg_buf[255], bg_buf[255];
int  clist_fg, clist_bg;
int *cur_list = &clist_fg;
int  cur_dir  = 1;
int  incr_val = 1;
 
GR_FONT_INFO finfo;
GR_LOGFONT  plogf;

static void print_help(char *prog);
static int findcolor(const char *colorstr);
static void root_callback(mvp_widget_t *widget, char key);

/*
 * main()
 */ 
int
main(int argc, char **argv)
{
	int c, i, bufpos;
	char *fontstr = NULL;
	int mode = -1, output = -1, aspect = -1;
	int width, height;
   int max_cols, max_rows, num_cols, num_rows;

   start_fg_color = "SNOW";
   start_bg_color = "BLACK";

	if (argc > 32) {
		fprintf(stderr, "too many arguments\n");
		exit(1);
	}

	for (i=0; i<argc; i++)
		saved_argv[i] = strdup(argv[i]);

	while ((c=getopt(argc, argv, "a:f:b:hi:o:")) != -1) {
		switch (c) {
		case 'a':
			if (strcmp(optarg, "4:3") == 0) {
				aspect = 0;
			} else if (strcmp(optarg, "16:9") == 0) {
				aspect = 1;
			} else {
				fprintf(stderr, "unknown aspect ratio '%s'\n",
					optarg);
				print_help(argv[0]);
				exit(1);
			}
			break;
		case 'h':
			print_help(argv[0]);
			exit(0);
			break;
		case 'f':
			start_fg_color = strdup(optarg);
			break;
		case 'b':
			start_bg_color = strdup(optarg);
			break;
		case 'o':
			if (strcasecmp(optarg, "svideo") == 0) {
				output = AV_OUTPUT_SVIDEO;
			} else if (strcasecmp(optarg, "composite") == 0) {
				output = AV_OUTPUT_COMPOSITE;
			} else {
				fprintf(stderr, "unknown output '%s'\n",
					optarg);
				print_help(argv[0]);
				exit(1);
			}
			break;
		default:
			print_help(argv[0]);
			exit(1);
			break;
		}
	}

   if ( argc - optind != 1 ) {
      printf("Parm Error: <font> required\n");
      exit(0); 
   }
   fontstr = strdup(argv[optind]);

   printf("font=%s\n", fontstr);

	if (av_init() < 0) {
		fprintf(stderr, "failed to initialize av hardware!\n");
		exit(1);
	}
   
	if (mode != -1)
		av_set_mode(mode);

	if (av_get_mode() == AV_MODE_PAL) {
		printf("PAL mode, 720x576\n");
		width = 720;
		height = 576;
	} else {
		printf("NTSC mode, 720x480\n");
		width = 720;
		height = 480;
	}

	osd_set_surface_size(width, height);

	av_attach_fb();
	av_play();

	if (aspect != -1)
		av_set_aspect(aspect);

	if (output != -1)
		av_set_output(output);


	/*
	 * XXX: moving to uClibc seems to have exposed a problem...
	 *
	 * It appears that uClibc must make more use of malloc than glibc
	 * (ie, uClibc probably frees memory when not needed).  This is
	 * exposing a problem with memory exhaustion under linux where
	 * mvpmc needs memory, and it will hang until linux decides to
	 * flush some of its NFS pages.
	 *
	 * BTW, the heart of this problem is the linux buffer cache.  Since
	 * all file systems use the buffer cache, and linux assumes that
	 * the buffer cache is flushable, all hell breaks loose when you
	 * have a ramdisk.  Essentially, Linux is waiting for something
	 * that will never happen (the ramdisk to get flushed to backing
	 * store).
	 *
	 * So, force the stack and the heap to grow, and leave a hole
	 * in the heap for future calls to malloc().
	 */
	{
#define HOLE_SIZE (1024*1024*1)
#define PAGE_SIZE 4096
		int i, n = 0;
		char *ptr[HOLE_SIZE/PAGE_SIZE];
		char *last = NULL, *guard;
		char stack[1024*512];

		for (i=0; i<HOLE_SIZE/PAGE_SIZE; i++) {
			if ((ptr[i]=malloc(PAGE_SIZE)) != NULL) {
				*(ptr[i]) = 0;
				n++;
				last = ptr[i];
			}
		}

		for (i=1; i<1024; i++) {
			guard = malloc(1024*i);
			if ((unsigned int)guard < (unsigned int)last) {
				if (guard)
					free(guard);
			} else {
				break;
			}
		}
		if (guard)
			*guard = 0;

		memset(stack, 0, sizeof(stack));

		for (i=0; i<HOLE_SIZE/PAGE_SIZE; i++) {
			if (ptr[i] != NULL)
				free(ptr[i]);
		}

		if (guard)
			printf("Created hole in heap of size %d bytes\n",
			       n*PAGE_SIZE);
		else
			printf("Failed to create hole in heap\n");
	}

//+******************************************************
	mvpw_init();
	root = mvpw_get_root();
	mvpw_set_key(root, root_callback);

   memset(&plogf, 0, sizeof(plogf));
   if ( fontstr ) {
      printf("Loading font: %s\n", fontstr);
		//fontid = mvpw_load_font(fontstr);
      strncpy( &(plogf.lfFaceName[0]), fontstr, MWLF_FACESIZE);
      plogf.lfSansSerif = 1;
      plogf.lfQuality = 1;
      plogf.lfHeight = 24;
      plogf.lfWeight = MWLF_WEIGHT_BOLD;     
      fontid = GrCreateFont(NULL, 0, &plogf);
   }
   else {
      printf("Using default font\n");
   }
   GrGetFontInfo(fontid, &finfo);
   GrSetFontAttr(fontid, GR_TFKERNING | GR_TFANTIALIAS, 0);


	mvpw_get_screen_info(&si);

   printf("NUM_COLORS: %d\n", num_colors);
   if ( (clist_fg = findcolor(start_fg_color)) == num_colors ) {
      printf("couldn't find color: %s", start_fg_color);
      exit(1);
   }
   if ( (clist_bg = findcolor(start_bg_color)) == num_colors ) {
      printf("couldn't find color: %s", start_bg_color);
      exit(1);
   }
   snprintf(fg_buf, 254, "FG: %3d: %06X: %s", clist_fg, color_list[clist_fg].val & 0x00ffffff, color_list[clist_fg].name);
   snprintf(bg_buf, 254, "BG: %3d: %06X: %s",clist_bg, color_list[clist_bg].val & 0x00ffffff, color_list[clist_bg].name);
   printf("--->%s\n", fg_buf);
   printf("--->%s\n", bg_buf);
   

	fgbg_box_attr.font = fontid;
   fg_box = mvpw_create_text(NULL, 100, 300, 500, 50, BLACK, 0, 0);	
   mvpw_set_text_attr(fg_box, &fgbg_box_attr);
   mvpw_set_text_str(fg_box, fg_buf);
	mvpw_show(fg_box);
   bg_box = mvpw_create_text(NULL, 100, 350, 500, 50, BLACK, 0, 0);	
   mvpw_set_text_attr(bg_box, &fgbg_box_attr);
   mvpw_set_text_str(bg_box, bg_buf);
	mvpw_show(bg_box);


	text_box_attr.font = fontid;
   max_cols = (si.cols - (2 * text_box_attr.margin) - 100) / (finfo.height + 2);
   max_rows = (si.rows - (2 * text_box_attr.margin) - 100) / (finfo.maxwidth + 2);
   printf ("Text max cols:%d   max rows: %d\n", max_cols, max_rows);

   if ( max_cols <= 16 ) {
      num_cols = max_cols;
      num_rows = max_rows;
   }
   else {
      num_cols = 16;
      num_rows = (96/16);
   }

   h = (finfo.height + 10) * ((95 / num_cols) + 1);
   //h = si.rows;
	//w = ((finfo.maxwidth + 2) * num_cols) + 10;
   w = si.cols;
   printf ("Box w: %d h: %d\n", w, h);

   buf[0] = '\n';
   bufpos = 1;
   for (i = 0; i < 95; i++) {
      if ( i && !(i % num_cols) ) {
         buf[bufpos++] = '\n';
      }  
      buf[bufpos++] = i+32;
   }
   buf[bufpos] = '\0';
   printf("txt:\n%s\n", buf);

	text_box_attr.font = fontid;
   text_box_attr.fg = color_list[clist_fg].val;
   text_box = mvpw_create_text(NULL, 0, 0, w, h, color_list[clist_bg].val, 0, 0);	
   mvpw_set_text_attr(text_box, &text_box_attr);

   GrSetFontAttr(fontid, GR_TFKERNING | GR_TFANTIALIAS, 0);
   mvpw_set_text_str(text_box, buf);

	mvpw_show(text_box);
	mvpw_event_flush();

	mvpw_set_idle(NULL);
	mvpw_event_loop();

	return 0;
}

static int findcolor(const char *colorstr)
{
   int x;
   for (x=0; x < num_colors; x++) {
      if ( strcmp(colorstr, color_list[x].name) == 0 ) {
         printf("Found color: %s: %08x\n", color_list[x].name, color_list[x].val);
         break;
      }
   }
   return(x);
}

static void
root_callback(mvp_widget_t *widget, char key)
{
   int jmp;
   //printf("rootcallback: %c\n", key);

   switch ( key) {
   case MVPW_KEY_LEFT:
      incr_val = 1;
      cur_list = &clist_bg;
      cur_dir  = -1;
      break;
   case MVPW_KEY_RIGHT:
      incr_val = 1;
      cur_list = &clist_bg;
      cur_dir  = 1;
      break;
   case MVPW_KEY_DOWN:
      incr_val = 1;
      cur_list = &clist_fg;
      cur_dir  = -1;
      break;
   case MVPW_KEY_UP:
      incr_val = 1;
      cur_list = &clist_fg;
      cur_dir  = 1;
      break;
   case MVPW_KEY_ONE:
      incr_val = 1;      
      break;
   case MVPW_KEY_TWO:
      incr_val = 2;      
      break;
   case MVPW_KEY_THREE:
      incr_val = 3;      
      break;
   case MVPW_KEY_FOUR:
      incr_val = 4;      
      break;
   case MVPW_KEY_FIVE:
      incr_val = 5;      
      break;
   case MVPW_KEY_SIX:
      incr_val = 6;      
      break;
   case MVPW_KEY_SEVEN:
      incr_val = 7;      
      break;
   case MVPW_KEY_EIGHT:
      incr_val = 8;      
      break;
   case MVPW_KEY_NINE:
      incr_val = 9;      
      break;
   case MVPW_KEY_ZERO:
      incr_val = 10;      
      break;
   default:
      break;
   } //switch

   jmp = incr_val * cur_dir;
   *cur_list = INCR_COLOR(*cur_list, jmp);
   if ( cur_list ==  &clist_fg ) {
      snprintf(fg_buf, 254, "FG: %3d: %06X: %s", clist_fg, color_list[clist_fg].val & 0x00ffffff, color_list[clist_fg].name);
      mvpw_set_text_str(fg_box, fg_buf);
      mvpw_hide(fg_box);
      mvpw_show(fg_box);
      text_box_attr.fg = color_list[clist_fg].val;
      mvpw_set_text_attr(text_box, &text_box_attr);
      mvpw_hide(text_box);
      mvpw_show(text_box);
   }
   else {
      snprintf(bg_buf, 254, "BG: %3d: %06X: %s",clist_bg, color_list[clist_bg].val & 0x00ffffff, color_list[clist_bg].name);
      mvpw_set_text_str(bg_box, bg_buf);
      mvpw_hide(bg_box);
      mvpw_show(bg_box);
      mvpw_set_bg(text_box, color_list[clist_bg].val);
      mvpw_hide(text_box);
      mvpw_show(text_box);
   }
   //printf("--->%s\n", fg_buf);
   //printf("--->%s\n", bg_buf);

//		video_callback(widget, key);
}

static void print_help(char *prog)
{
	printf("Usage: %s <options> <font file>\n", prog);

	printf("\t-a aspect \taspect ratio (4:3 or 16:9)\n");
	printf("\t-f color  \tforeground color\n");
	printf("\t-b color  \tbackground color\n");
	printf("\t-h        \tprint this help\n");
	printf("\t-o output \toutput device (composite or svideo)\n");
}
