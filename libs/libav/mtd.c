/*
 *  Copyright (C) 2004, Jon Gettler
 *  http://mvpmc.sourceforge.net/
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ident "$Id$"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

#include "mvp_av.h"
#include "av_local.h"

/*
 * init_mtd1() - Read configuration data out of flash
 *
 * Arguments:
 *	none
 *
 * Returns:
 *	0 if mtd1 could be read, -1 if it failed
 */
int
init_mtd1(void)
{
	int fd;
	short *mtd;

	if ((fd=open("/dev/mtd1", O_RDONLY)) < 0)
		return -1;

	if ((mtd=malloc(8192)) == NULL)
		return -1;

	read(fd, mtd, 8192);

	close(fd);

	vid_mode = mtd[2119];
	aspect = mtd[2125];

	free(mtd);

	return 0;
}
