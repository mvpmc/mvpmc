/*
 *  Copyright (C) 2004-2006, John Honeycutt
 *  http://www.mvpmc.org/
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

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/errno.h>
#include <signal.h>
#include "cli.h"

extern char *readline_lite(char *prompt);
extern void  cli_restore_terminal(void);

#define CMDSTR_MAXSZ 120
#define whitespace(c) (((c) == ' ') || ((c) == '\t'))

static int      cmdmax = 0;
static int      done   = 0;
static int      cmd_pipe[2];

static void catch_signals(int sig)
{
   sig=sig;
   cli_restore_terminal();
   exit(0);
}


//Strip whitespace from the start and end of STRING.  Return a pointer
//into STRING.
//Set size to the string size including the ending null.
static char *stripwhite (char  *string, int *size)
{
   register char *s, *t;
   
   *size = 0;
   for (s = string; whitespace (*s); s++);
    
   if (*s == 0)
      return (s);
   
   t = s + strlen(s) - 1;
   while (t > s && whitespace (*t)) {
      t--;
   }
   *++t = '\0';
   *size = t-s+1;
   return s;
}

static void tolowerStr(char *str)
{
   while(*str) {
      *str=tolower(*str);
      str++;
   }
}

static void processLibRc (int rc)
{
   if (!rc) {
      printf("rc=0 AOK...\n");
   }
   else {
      printf("***RtnCode: %d=>%s\n",rc, strerror(abs(rc)));
   } 
   return;
}

static int doCmdHelp (cmdb_t cmd_list[])
{
    int icmd;
    printf("\nSHELL COMMANDS:\n");
    for (icmd = 0; icmd < cmdmax; icmd++) {
       printf("   %-10s :    %s\n", cmd_list[icmd].name, cmd_list[icmd].help);
    }
    printf("   %-10s :    %s\n\n", "help", "display this help");
    printf("   >help <command> provides detailed help for the command.\n\n");
    return 0;
}

static int processCliCmd(cmdb_t cmd_list[], char *cmdLine)
{
   char *pch;
   int  icmd, rc, st;
   char *argv[MAX_PARMS+1];
   int  argc;

   if (cmdLine[0] == '\n') { 
      return 0; 
   }
   
   // Parse cmdline
   //
   argc = 0;
   st = 0;
   pch = cmdLine;
   while (*pch) {
      if ((st == 0) && (!isspace(*pch))) {
         st = 1;
         argv[argc++] = pch;
      } 
      else if (st && isspace(*pch)) {
         *pch = '\0';
         if (argc == MAX_PARMS) {
            printf("Error: Cmdline MAX PARMS exceeded\n");
            return -1;
         }
         st = 0;
      }
      pch++;
   }
   argv[argc] = NULL;
   tolowerStr(argv[0]);
   

   if ( (argc > 0 ) &&  (strncmp( argv[0], "help", 4) == 0) ) {
      if ( argc == 1 ) {
         doCmdHelp(cmd_list);
         return(0);
      }
      else {
         argv[0] = argv[1];
         argv[1] = "-h";
         argc    = 2;
      }
   }

//#define DBGDBG
#ifdef DBGDBG
   printf("argc=%d  Tokens:\n", argc);
   for (x=0; x < argc; x++) {
      printf("%d: %s\n", x, argv[x]);
   }
   printf("\n");
#endif

   // Call the cmd fxn
   //
   for (icmd = 0; icmd < cmdmax; icmd++) {
      if (strcmp(cmd_list[icmd].name, argv[0]) == 0) {
         if (cmd_list[icmd].maxargc) {
            if ((argc < cmd_list[icmd].minargc) ||
                (argc > cmd_list[icmd].maxargc))  {
               printf("\n Number of Args Mismatch: %d-%d.\n",
                      cmd_list[icmd].minargc, cmd_list[icmd].maxargc);
               break;
            }
         }
         if ( (rc = cmd_list[icmd].proc(argc, argv)) ) {
            processLibRc(rc);
         }            
         break;
      }
   }
   
   if (icmd == cmdmax) {
      printf("\n Command: \"%s\" - not found.\n", argv[0]);
   }
   return 0;
}

static void ci_input_thread(char *prompt)
{
   char            *line, *cmdStr;
   int              size;
   struct sigaction new_action, old_action;
 
    //
    // Set up signal handling
    //
    new_action.sa_handler = catch_signals;
    sigemptyset(&new_action.sa_mask);
    new_action.sa_flags = 0;
    if ((sigaction(SIGINT, &new_action, &old_action) != 0) ||
        (sigaction(SIGHUP, &new_action, &old_action) != 0) ||
        (sigaction(SIGTERM, &new_action, &old_action) != 0))  {
        printf("***ERROR: Child ci_input_thread signal catching failed.\n");
        exit(-1);
    }    

   // Loop reading and executing lines until the user quits.
   while ( !(done) )
   {
      if ( (line = readline_lite("")) == NULL ) {
         cli_restore_terminal();
         break;;
      }
           
      // Remove leading and trailing whitespace from the line.
      // Then, if there is anything left, add it to the history list
      // and execute it.
      // Note: 'size' includes the terminating '\0'
      cmdStr = stripwhite(line, &size);
      if (*cmdStr)
      {
         //Echo command to output
         write(cmd_pipe[1], cmdStr, size);
      }
      else {
         printf("%s", prompt);
         fflush(stdout);
      }
   }
}

static void ci_output_thread(cmdb_t cmd_list[], char *hello_msg, char *prompt)
{
   char dataStr[CMDSTR_MAXSZ];
   int  dataSz;
   int  rc, save_errno;

   int    commFD, cliFD;
   int    maxFDp1;   //Used in main loop select stmt
   fd_set fdsSetup;  //File descriptors for main loop select stmt
   

   cliFD  = cmd_pipe[0];
   commFD = -1;

   //
   // Set up the fdset
   //
   FD_ZERO(&fdsSetup);
   FD_SET(cliFD, &fdsSetup);

   if ( commFD != -1 ) {
      FD_SET(commFD, &fdsSetup);
      
      if ( cliFD > commFD ) {
         maxFDp1 = cliFD + 1;  
      }
      else {
         maxFDp1 = commFD + 1;        
      }
   }
   else {
      maxFDp1 = cliFD + 1;
   }

   for ( cmdmax=0; cmdmax < MAX_USRCMDS; cmdmax++ ) {
      if (cmd_list[cmdmax].proc == NULL) {
         break;
      }
   }

   printf("*************************************\n");
   printf("  %s\n", hello_msg);
   printf("  %d subcommands bound\n", cmdmax);
   printf("  Enter \"help\" for help\n");
   printf("  Enter \"<command> -h\" for detailed command help\n");
   printf("  <CNTL>-\\ or <CNTL>-C Exits\n");
   printf("*************************************\n\n");
   printf("%s", prompt);
   fflush(stdout);

   while (1) {
      fd_set rfds;
      
      rfds = fdsSetup;
      rc   = select(maxFDp1, &rfds, NULL, NULL, NULL);        
      switch (rc)
      {
         case 0:
         {
            // Timeout
            printf("select stmt: unexpected timeout\n");
            break;
         }
         
         case -1:
         {
            // Error
            save_errno = errno;
            printf("Exiting:select stmt error: %d=>%s\n",save_errno, strerror(save_errno));
            return;
         }
         default:
         {
            // Process the FD needing attention

            if ( FD_ISSET(cliFD, &rfds) ) {
               dataSz = read(cliFD, dataStr, CMDSTR_MAXSZ);
               if ( dataSz == 0 ) {
                  printf("output thread exit\n");
                  return;
               }

               // Process the command
               //
               processCliCmd(cmd_list, dataStr);
               printf("%s", prompt);
               fflush(stdout);
            }
            else if ( commFD != -1 ) { 
               if ( FD_ISSET(commFD, &rfds) ) { 
                  dataSz = read(commFD, dataStr, CMDSTR_MAXSZ);
               }
            }
            else {
               printf("select rc>0 w/o ISSET: %d", rc);
            }

            break;
         }
      } //switch
   } //while
}


int start_cli(cmdb_t cmd_list[], char *prompt, char *welcome_msg)
{
   pid_t  child_pid = -1;
   pid_t  parent_pid, fork_result;
   
   parent_pid = getpid();

   if ( pipe(cmd_pipe) == 0 ) {
      if ( (fork_result = fork()) == -1 ) {
         fprintf(stderr, "Fork Failure\n");
         return(0);
      }

      // Fork a child for output processing
      // If either the parent or child fxns return
      // then signal the other process and exit.
      //
      if ( fork_result == 0 ) {
         //Child
         ci_input_thread(prompt);         
         kill(parent_pid, SIGINT);
      }
      else {
         //parent
         child_pid = fork_result;
         ci_output_thread(cmd_list, welcome_msg, prompt);
         if ( child_pid != -1 ) {
            kill(child_pid, SIGINT);
         }
      }
   }
   return(0);
}
