/*
 *
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

#ifndef __cli_h_
#define __cli_h_

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define MAX_PARMS       20  /* Max number of user parameters */
#define MAX_USRCMDS     40  /* Max number of user commands */
#define MAX_CMDLINE_SZ 200  /* Max number oc chars in cmd line */

#define STDIN_FD  0
#define STDOUT_FD 1

typedef struct cmdb 
{
   char *name;
   int (*proc)(int, char **);
   int minargc;
   int maxargc;
   char *help;
} cmdb_t;

#define USAGE(usage_str) { \
   if ( (argc == 2) && (strncmp(argv[1], "-h", 2) == 0) ) { \
      printf("Usage:  %-10s ", argv[0]); \
      printf("%s\n", usage_str); \
      return(0); \
   } \
}
 
extern int start_cli(cmdb_t cmd_list[], char *prompt, char *welcome_msg);

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* __cli_h_ */
