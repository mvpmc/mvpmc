/*
 * Copyright (C) 2002 John Todd Larason <jtl@molehill.org>
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 */

#include "sleep.h"

#ifdef WIN32
#include "windows.h"

void rtv_sleep(u16 msec) 
{
    Sleep(msec);
}
#else
#include <unistd.h>
void rtv_sleep(u16 msec)
{
    usleep(msec * 1000);
}
#endif
