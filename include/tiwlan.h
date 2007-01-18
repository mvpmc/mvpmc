/*
 *  Copyright (C) 2006-2007, Jon Gettler
 *  http://www.mvpmc.org/
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

#ifndef TIWLAN_H
#define TIWLAN_H

typedef struct tiwlan_ssid {
	char name[32];
	int strength;
} tiwlan_ssid_t;

extern int tiwlan_probe(tiwlan_ssid_t *ssid, int max);
extern int tiwlan_enable(char *ssid, int with_wep);
extern int tiwlan_disable(void);
extern int tiwlan_add_wep(tiwlan_ssid_t *ssid, char *wep);
extern int tiwlan_signal(void);

#endif /* TIWLAN_H */
