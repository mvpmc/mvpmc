/*
*  Copyright (C) 2004,2005,2006,2007, Jon Gettler
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
#include <dirent.h>
#include <glob.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <assert.h>
#include <sys/time.h>
#include <time.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <fcntl.h>

#include <expat.h>

#include <mvp_widget.h>
#include <mvp_av.h>
#include <mvp_demux.h>

#include "mvpmc.h"
#include "http_stream.h"
#include "display.h"
#include "weather.h"

// Yahoo! Weather API page

// http://developer.yahoo.com/weather/index.html

//#define WEATHER_RSS_HOST is in http_stream.h used by http_stream too

#define WEATHER_RSS_PATH "/forecastrss"

#define WEATHER_IMAGE_HOST "http://l.yimg.com/us.yimg.com/i/us/we/52/%d.gif"

#define WEATHER_OUTPUT "Yahoo! Weather for %s, %s\n\
%s\n\
Sunrise: %s  Sunset: %s\n\
                            Current Conditions\n\
                            %s \n\
                            %s\n"

#define FORECAST_OUTPUT "%s\n\
%s\n\
High: %s\n\
Low :%s"

extern char *weather_location;
extern char *imagedir;

typedef struct {
	char location_id[20];
	char city[20];
	char region[20];
	char last_update[60];
	char current_temp[4];
	char current_condition[30];
	int  current_code;
	char sunrise[9];
	char sunset[9];
	char forecast_day[5][4];
	char forecast_date[5][12];
	char forecast_low[5][4];
	char forecast_high[5][4];
	char forecast_condition[5][30];
	int  forecast_code[5];
	int  current_forecast;
} weather_info_t;

#define rss_fmt(X) snprintf((X),sizeof((X)),"%s",attr[i + 1])

weather_code_t weather_codes_europe[] = {
	{ "Amsterdam", "NLXX0002_c" },
	{ "Berlin", "GMXX0007_c" },
	{ "London", "UKXX0085_c" },
	{ "Madrid", "SPXX0050_c" },
	{ "Paris", "FRXX0076_c" },
	{ "Rome", "ITXX0067_c" },
	{ NULL, NULL },
};

weather_code_t weather_codes_na[] = {
	{ "Chicago", "USIL0225" },
	{ "Denver", "USCO0105" },
	{ "Houston", "USTX0617" },
	{ "Minneapolis", "USMN0503" },
	{ "Ottawa", "CAXX0343_c" },
	{ "Raleigh", "USNC0558" },
	{ "San Francisco", "USCA0987" },
	{ NULL, NULL },
};

static void
start_tag(void *data, const char *el, const char **attr)
{
	int i;

	weather_info_t *weather_data = data;

	if (strcmp(el, "yweather:location") == 0) {
		for (i = 0; attr[i]; i += 2) {
			if (strcmp(attr[i], "city") == 0) {
				rss_fmt(weather_data->city);
			} else if (strcmp(attr[i], "region") == 0) {
				rss_fmt(weather_data->region);
			} else if (!weather_data->region[0] && strcmp(attr[i], "country") == 0) {
				rss_fmt(weather_data->region);
			}
		}
	} else if (strcmp(el, "yweather:astronomy") == 0) {
		for (i = 0; attr[i]; i += 2) {
			if (strcmp(attr[i], "sunrise") == 0) {
				rss_fmt(weather_data->sunrise);
			} else if (strcmp(attr[i], "sunset") == 0) {
				rss_fmt(weather_data->sunset);
			}
		}
	} else if (strcmp(el, "yweather:condition") == 0) {
		for (i = 0; attr[i]; i += 2) {
			if (strcmp(attr[i], "text") == 0) {
				rss_fmt(weather_data->current_condition);
			} else if (strcmp(attr[i], "code") == 0) {
				weather_data->current_code = atoi(attr[i + 1]);
			} else if (strcmp(attr[i], "temp") == 0) {
				rss_fmt(weather_data->current_temp);
			} else if (strcmp(attr[i], "date") == 0) {
				rss_fmt(weather_data->last_update);
			}
		}
	} else if (strcmp(el, "yweather:forecast") == 0) {
		for (i = 0; attr[i]; i += 2) {
			if (strcmp(attr[i], "day") == 0) {
				rss_fmt(weather_data->forecast_day[weather_data->current_forecast]);
			} else if (strcmp(attr[i], "date") == 0) {
				rss_fmt(weather_data->forecast_date[weather_data->current_forecast]);
			} else if (strcmp(attr[i], "low") == 0) {
				rss_fmt(weather_data->forecast_low[weather_data->current_forecast]);
			} else if (strcmp(attr[i], "high") == 0) {
				rss_fmt(weather_data->forecast_high[weather_data->current_forecast]);
			} else if (strcmp(attr[i], "text") == 0) {
				char temp[30];
				rss_fmt(temp);
				char *temp2 = strstr(temp, "Thunderstorms");
				if (temp2 != NULL)
					strcpy(temp2, "T-storms");
				temp2 = strstr(temp, "Thundershowers");
				if (temp2 != NULL)
					strcpy(temp2, "T-showers");
				strcpy(weather_data->
				       forecast_condition[weather_data->current_forecast],
				       temp);
			} else if (strcmp(attr[i], "code") == 0) {
				weather_data->forecast_code[weather_data->current_forecast]=
				       atoi(attr[i + 1]);
			}
		}
		weather_data->current_forecast++;
	}
}				/* End of start handler */

static void
parse_data(int data_stream, weather_info_t * weather_data)
{
	char Buff[STREAM_PACKET_SIZE+1]; //One byte extra for parsing
	char *ptr;

	XML_Parser p = XML_ParserCreate(NULL);
	if (!p) {
		fprintf(stderr, "Couldn't allocate memory for parser\n");
		strcpy(weather_data->city,"Error");
		sprintf(weather_data->last_update,"Yahoo! RSS Feed Error");
		return;
	}

	XML_SetElementHandler(p, start_tag, NULL);
	XML_SetUserData(p, weather_data);
	Buff[STREAM_PACKET_SIZE]=0;
	while (gui_state == MVPMC_STATE_WEATHER ) {
		int len;
		len = read(data_stream, Buff, STREAM_PACKET_SIZE);
		if (len < 0 ) {
			if ( errno==EAGAIN || errno==EINTR) {
				usleep(100000);
				continue;
			}
			fprintf(stderr, "Read error %d\n",errno);
			strcpy(weather_data->city,"Error");
			sprintf(weather_data->last_update,"Yahoo! RSS Feed Read Error");
		}
		if (!XML_Parse(p, Buff , len , len==0)) {
			fprintf(stderr, "Parse error at line %d:\n%s\n",
				XML_GetCurrentLineNumber(p),
				XML_ErrorString(XML_GetErrorCode(p)));

                        if ( XML_GetErrorCode(p) != XML_ERROR_FINISHED ) {
				strcpy(weather_data->city,"Error");
				sprintf(weather_data->last_update,"Yahoo! RSS Feed XML Error");
			}
			break;
		} else {
			if (weather_data->city[0]==0) {
				ptr = strstr(Buff,"Sorry, your location");
				if (ptr!=NULL ) {
					strcpy(weather_data->city,"Error");
					snprintf(weather_data->last_update,60,"%s",ptr);
					ptr = strstr(weather_data->last_update,"<");
					if (ptr!=NULL) {
						*ptr=0;
					}
					break;
				}
			}
		}
		if (len==0) {
			break;
		}
	}
        XML_ParserFree(p);
}

static int 
get_weather_data(weather_info_t * weather_data) {

	char path[100];
	int retcode;
	
	// yahoo says to use "http://%s%s?p=%s"	but that retrieves only 3 day

	snprintf(path,100,"http://%s%s/%s.xml",WEATHER_RSS_HOST,WEATHER_RSS_PATH,weather_data->location_id);
	current = strdup(path);
	retcode = http_main();
	free(current);

	if (retcode==HTTP_RSS_FILE_WEATHER) {
                parse_data(fd, weather_data);
		retcode = 0;
	} else {
		retcode =  -1;
	}
	close(fd);

	return retcode;
}

static int
fetch_weather_image(int code, char *filename)
{
	char path[80];
	int retcode;
	if (code == 0 ) {
		return -1;
	}
	snprintf(path,80,WEATHER_IMAGE_HOST,code);

	current = strdup(path);
	retcode = http_main();
	free(current);

	if (retcode==HTTP_IMAGE_FILE_GIF) {
		char buf[STREAM_PACKET_SIZE];
		FILE *outfile = fopen(filename, "wb");
		retcode = 0;
		int nitems = -1;
		while (nitems && gui_state == MVPMC_STATE_WEATHER) {
			nitems = read(fd,buf, STREAM_PACKET_SIZE);
			if (nitems < 0 ){
				if ( (errno==EAGAIN || errno==EINTR)  ) {
					usleep(100000);
					continue;
				} else {
					retcode = -1;
					break;
				}
			}
			fwrite(buf,1, nitems, outfile);
		}
		fclose(outfile);
	} else {
		retcode = -1;
	}
	close(fd);
	return retcode;
}

int
update_weather(mvp_widget_t * weather_widget, mvpw_text_attr_t * weather_attr)
{
	mvpw_show(root);
	mvpw_expose(root);

	busy_start();
	if (weather_location != NULL) {
		weather_info_t *weather_data = malloc(sizeof(weather_info_t));
		memset(weather_data, 0, sizeof(weather_info_t));
		strncpy(weather_data->location_id, weather_location,
			sizeof(weather_data->location_id));
		printf("Weather feed %s\n",weather_data->location_id);
		weather_data->current_forecast = 0;
		mvp_widget_t *text = mvpw_create_text(weather_widget, 0, 0, 600, 220, MVPW_BLACK, MVPW_BLACK, 0);
		if (get_weather_data(weather_data) == 0 && gui_state == MVPMC_STATE_WEATHER) {
			char output[300];
			char image[100];
			if (strcmp(weather_data->city,"Error")) {

				snprintf(output,300 ,WEATHER_OUTPUT, weather_data->city,
					weather_data->region, weather_data->last_update,
					weather_data->sunrise, weather_data->sunset,
					weather_data->current_temp,
					weather_data->current_condition);
				mvpw_set_text_str(text, output);
				mvpw_set_text_attr(text, weather_attr);
				mvpw_show(text);


				if ( weather_data->current_code != 33 ) {
					// Yahoo shows the sun at night!
					int hour;
	
					char *ptr = strchr(weather_data->last_update,':');
					hour = *(ptr-1)-48;
					if (*(ptr-2)=='1') {
						hour  +=10;
					}
					if (*(ptr+4)=='p' ) {
						hour += 12;
					} else if (hour==12) {
						hour = 0;
					}
					if (hour < 7 || hour > 19) {
						weather_data->current_code = 33;
					}
				}
	
				if (fetch_weather_image(weather_data->current_code, "/tmp/weather_image.gif") == 0) {
						mvpw_set_image(current_conditions_image, "/tmp/weather_image.gif");
					}
					else {
						snprintf(image,100,"%s/%s", imagedir, "weather_unknown.png");
						mvpw_set_image(current_conditions_image, image);
					}
				if ( gui_state == MVPMC_STATE_WEATHER ) {
					mvpw_raise(current_conditions_image);
					mvpw_show(current_conditions_image);
				}

	
				int i;
				for(i = 0; i < 5 && gui_state == MVPMC_STATE_WEATHER; i++) {
					snprintf(output,200, FORECAST_OUTPUT,
						weather_data->forecast_day[i],
						weather_data->forecast_condition[i],
						weather_data->forecast_high[i],
						weather_data->forecast_low[i]);
					mvpw_set_text_str(forecast[i], output);
					mvpw_set_text_attr(forecast[i], weather_attr);
					mvpw_show(forecast[i]);
					snprintf(image,100,"/tmp/weather_code%d.gif", weather_data->forecast_code[i]);
					if (access(image,F_OK) ) {
						if (fetch_weather_image(weather_data->forecast_code[i], image) != 0) {
							sprintf(image, "%s/%s", imagedir, "weather_unknown.png");
						}
					}
					mvpw_set_image(forecast_image[i], image);
					mvpw_raise(forecast_image[i]);
					mvpw_show(forecast_image[i]);
				}
				for(i = 0; i < 5; i++) {
					snprintf(image,100,"/tmp/weather_code%d.gif", weather_data->forecast_code[i]);
					if (access(image,F_OK)==0 ) {
						unlink(image);
					}
				}
				unlink("/tmp/weather_image.gif");						
			} else {
				if ( gui_state == MVPMC_STATE_WEATHER ) {
					snprintf(output,sizeof(output),"Yahoo! Weather - Error\n%s",weather_data->last_update);
					mvpw_set_text_str(text,output );
					mvpw_set_text_attr(text, weather_attr);
					mvpw_show(text);
				}
			}
		} else {
			if ( gui_state == MVPMC_STATE_WEATHER ) {
				mvpw_set_text_str(text,"There was a problem connecting to Yahoo! Weather feed" );
				mvpw_set_text_attr(text, weather_attr);
				mvpw_show(text);
			}
		}
		free(weather_data);
	}
	busy_end();
	return 0;
}

