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

#ifndef WEATHER_H
#define WEATHER_H

#include <mvp_widget.h>

typedef struct {
	char *name;
	char *code;
} weather_code_t;

extern mvp_widget_t *current_conditions_image;
extern mvp_widget_t *forecast_image[5];
extern mvp_widget_t *forecast[5];

extern weather_code_t weather_codes_europe[];
extern weather_code_t weather_codes_na[];

int update_weather(mvp_widget_t *weather_widget, mvpw_text_attr_t * weather_attr);

#endif /* WEATHER_H */
