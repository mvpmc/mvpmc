/*
 *  Copyright (C) 2004, John Honeycutt
 *  http://mvpmc.sourceforge.net/
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

#include <string.h>
#include <stdlib.h>
#include "rtv.h"
#include "rtvlib.h"


void rtv_set_dbgmask(u32 mask)
{
   rtv_debug = mask;
}
u32 rtv_get_dbgmask(void)
{
   return(rtv_debug);
}
