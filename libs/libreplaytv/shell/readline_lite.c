 /*
 *  $Id:
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

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <string.h>
#include <sys/types.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/errno.h>

#define CHR_CTRL_C  0x03
#define CHR_CTRL_D  0x04
#define CHR_BS      0x08
#define CHR_LF      0x0a
#define CHR_CR      0x0d
#define ESCAPE      0x1b
#define ESC_RTB     0x5b
#define ARROW_UP    0x41
#define ARROW_DOWN  0x42
#define ARROW_RIGHT 0x43
#define ARROW_LEFT  0x44
#define CHR_BSK     0x7f

#define COMMAND_LENGTH      256
#define MAX_HIST_NUM        32

typedef enum 
{
   ESC_NO_ACTION,
   ESC_HIST_PREV,
   ESC_HIST_NEXT,
   ESC_CSR_RIGHT,
   ESC_CSR_LEFT,
   ESC_BACKSPACE
} ESCAPE_ACTION_T;

typedef struct 
{
   ESCAPE_ACTION_T action;
   char            seq[10];
} ESC_TBL_T;

typedef struct _CMD_HISTORY
{
    char* cmdList[MAX_HIST_NUM];  /* History list of pointers to commands. */
    int   cmdHead;                /* Index to next available history list element. */
    int   arrowHead;              /* Index to current "arrowed" position in the history list. */
} CMD_HISTORY;

typedef enum _HISTORY_RESULT
{
    HISTORY_SUCCESS,
    HISTORY_AT_TOP,
    HISTORY_AT_BOTTOM,
    HISTORY_ERROR
} HISTORY_RESULT;


static int    lib_initialized = 0;
static char   cmdStr[256];
static char  *cmdStrHead;
static char  *cmdStrTail; 
static char  *cursorPos;

static struct termios initial_settings; 

static CMD_HISTORY CmdHistory; /* The interpreter Command History Object */

static ESC_TBL_T escapeTable[] =
{
   { ESC_HIST_PREV, {ESC_RTB, ARROW_UP  ,  0} }, //UpArrow
   { ESC_HIST_NEXT, {ESC_RTB, ARROW_DOWN,  0} }, //DownArrow
   { ESC_BACKSPACE, {ESC_RTB, 0x33, 0x7E,  0} }, //DeleteKey
   { ESC_CSR_RIGHT, {ESC_RTB, ARROW_RIGHT, 0} }, //RightArrow
   { ESC_CSR_LEFT,  {ESC_RTB, ARROW_LEFT,  0} }, //LeftArrow
};
#define ESC_TBL_SIZE (sizeof(escapeTable) / sizeof(ESC_TBL_T))

char* readline_lite(char *prompt);
void  cli_restore_terminal(void);

/*-----------------------------------------------------------------------------------------
 * Funciton:    interpHistoryInit
 * Description: Zero's out the entire Command History Object.
 * Parameters:  None.
 * Returns:     Nothing.
 *----------------------------------------------------------------------------------------*/
static void interpHistoryInit(void)
{
   int          *cmdHist = (int*)&CmdHistory;
   unsigned int  i;
   
   for (i = 0; i < sizeof(CMD_HISTORY)/4; i++)
      cmdHist[i] = 0;
}

/*-----------------------------------------------------------------------------------------
 * Funciton:    interpInit
 * Description: Initializes the interpreter command/function table. 
 * Parameters:  prompt - Pointer to string for prompt initialization.
 * Returns:     Nothing.
 *----------------------------------------------------------------------------------------*/
static void interpInit(void)
{
   
   /* Initialize command line string. */
   cmdStrHead = cmdStr;
   cmdStrTail = cmdStr;
   cursorPos  = cmdStr;
   
   interpHistoryInit();
}

/*-----------------------------------------------------------------------------------------
 * Funciton:     interpHistoryInc
 * Description:  Performs a module increment of a command history index.
 * Parameters:  x - A command history index.
 * Returns:     The modulo incremented command history index.
 *----------------------------------------------------------------------------------------*/
static int interpHistoryInc( int x )
{
   return  (x + 1) % MAX_HIST_NUM;
}


/*-----------------------------------------------------------------------------------------
 * Funciton:     interpHistoryDec
 * Description:  Performs a module decrement of a command history index.
 * Parameters:  x - A command history index.
 * Returns:     The modulo decremented command history index.
 *----------------------------------------------------------------------------------------*/
static int interpHistoryDec( int x )
{
   if ( x )
      return x - 1;
   else
      return MAX_HIST_NUM - 1;
}

/*-----------------------------------------------------------------------------------------
 * Funciton:    interpHistPushCmd
 * Description: Saves a command (cmd) string into the command history buffer.
 * Parameters:  cmd - A user (ASCII) command.
 * Returns:     A History Success code (this command can never fail).
 *----------------------------------------------------------------------------------------*/
static HISTORY_RESULT interpHistPushCmd(char* cmd)
{
   /* If the next history slot is virgin, initate it. */
   if ( !CmdHistory.cmdList[CmdHistory.cmdHead] )
      CmdHistory.cmdList[CmdHistory.cmdHead] = (char*)malloc(256);
   
   strcpy( CmdHistory.cmdList[CmdHistory.cmdHead], cmd );
   CmdHistory.cmdHead = interpHistoryInc(CmdHistory.cmdHead);
   CmdHistory.arrowHead = CmdHistory.cmdHead;
   
   /* The command history push can never fail. */
   return HISTORY_SUCCESS; 
}

/*-----------------------------------------------------------------------------------------
 * Funciton:    interpHistPopCmd
 * Description: Retrieves a command from the command history buffer.
                The input (arrow) must contain an up or down arrow char and
                specifies which direction in the history buffer to move
                prior to retrieval. A retrieved command is placed into the
                global command string buffer used by the interpreter. The
                global command string pointers are updated accordingly.
 * Parameters:  arrow - Value indicating keyboard arrow direction - up or down.
 * Returns:     History Result code indicating the type of action taken by this routine. 
 *----------------------------------------------------------------------------------------*/
static HISTORY_RESULT interpHistPopCmd( ESCAPE_ACTION_T direction )
{    
   if (direction == ESC_HIST_NEXT) { /* Move from old to new. */
      /* If already at the present, do not pop a command. */
      if (CmdHistory.arrowHead == CmdHistory.cmdHead) 
         return HISTORY_AT_TOP;
      
      CmdHistory.arrowHead = interpHistoryInc(CmdHistory.arrowHead);
      
      if (CmdHistory.arrowHead == CmdHistory.cmdHead) {
         /* We have arrowed our way back up to present. The display  */
         /* should show no commands. So, erase the last retieved     */
         /* command from the display. */  
         while (cmdStrTail > cmdStrHead) {
            cmdStrTail--;
            printf("%c %c", CHR_BS, CHR_BS);
         }
         
         return HISTORY_AT_TOP;
      }            
   }
   else if (direction == ESC_HIST_PREV) { /* Move from new to old. */
      CmdHistory.arrowHead = interpHistoryDec(CmdHistory.arrowHead);
      
      /* If we are at the begining of time (or no history yet exists), */
      /* do not pop a command. */
      if ( (!CmdHistory.cmdList[CmdHistory.arrowHead]) || 
           (CmdHistory.arrowHead == CmdHistory.cmdHead) ) {
         /* Since no pop, put arrow head back. */
         CmdHistory.arrowHead = interpHistoryInc(CmdHistory.arrowHead);   
         return HISTORY_AT_BOTTOM;
      }    
   }
   else
      return HISTORY_ERROR;
   
   /* OK, a valid command has been found, so remove a possible existing */
   /* command string from the display. */
   while (cmdStrTail > cmdStrHead) {
      cmdStrTail--;
      printf("%c %c", CHR_BS, CHR_BS);
   }

   /* Place the retieved command into the interpreter global command string buffer.  */
   strcpy(cmdStrHead, CmdHistory.cmdList[CmdHistory.arrowHead]);
   cmdStrTail += strlen(cmdStrHead);  /* Adjust the global tail to fit the new command. */
   cursorPos = cmdStrTail;
   return HISTORY_SUCCESS;
}

/*-----------------------------------------------------------------------------------------
 * Funciton: getEscapeAction   
 * Description:
 * Parameters:  None
 * Returns: 
 *----------------------------------------------------------------------------------------*/
static ESCAPE_ACTION_T getEscapeAction( void )
{
   unsigned int idx  = 0;
   unsigned int done = 0;
   unsigned int tblPos;
   signed char  c;
   int          possibleMatch[ESC_TBL_SIZE];

   memset(possibleMatch,1,sizeof(possibleMatch));

   while ( !(done) ) {
      done = 1;
      c = fgetc(stdin);
      for ( tblPos=0; tblPos < ESC_TBL_SIZE; tblPos++ ) {
         if ( possibleMatch[tblPos] ) {
            if ( escapeTable[tblPos].seq[idx] == c ) {
               if ( escapeTable[tblPos].seq[idx+1] == 0 ) {
                  return(escapeTable[tblPos].action);
               } 
               possibleMatch[tblPos] = 1;
               done = 0;
            }
            else {
               possibleMatch[tblPos] = 0;
            }
         }
      }
      idx++;
   }
   return(ESC_NO_ACTION);
}

/*-----------------------------------------------------------------------------------------
 * Funciton: deleteCmdChar 
 * Description:
 * Parameters:  None
 * Returns: 
 *----------------------------------------------------------------------------------------*/
static void deleteCmdChar(void) 
{
   char *pos;
   if ( cursorPos == cmdStrTail ) {
      cmdStrTail--;cursorPos--;
      printf("%c %c", CHR_BS, CHR_BS);
   }
   else if ( cursorPos != cmdStrHead ) {
      
      /* update the cmd string & terminal */
      printf("%c", CHR_BS);
      for ( pos = cursorPos; pos < cmdStrTail; pos++ ) {
         *(pos-1) = *pos;
         printf("%c", *(pos-1));         
      }
      printf(" ");
      cmdStrTail--;
      cursorPos--;
      
      pos = cmdStrTail+1;
      while (pos > cursorPos) {
         printf("%c", CHR_BS);
         pos--;
      }
   }
}

/*-----------------------------------------------------------------------------------------
 * Funciton: addCmdChar 
 * Description:
 * Parameters:  None
 * Returns: 
 *----------------------------------------------------------------------------------------*/
static void addCmdChar(signed char c) 
{
   char *pos;

   if ( cursorPos == cmdStrTail ) {
      *cmdStrTail = c;
      printf("%c", *cmdStrTail);
      cmdStrTail++;cursorPos++;
   }
   else {
      for ( pos = cmdStrTail; pos > cursorPos; pos-- ) {
         *(pos) = *(pos-1);
      }
      *cursorPos = c;
      cmdStrTail++;
      *cmdStrTail = '\0';
      printf("%s",cursorPos);
      cursorPos++;
      pos = cmdStrTail;
      while (pos > cursorPos) {
         printf("%c", CHR_BS);
         pos--;
      }      
   }
}

/*-----------------------------------------------------------------------------------------
 * Funciton:    interpBuildCmd
 * Description: Called from character rx fxn to assemble a user command line one char at
 *              at time. This routine places the received chars into
 *              the global command line buffer.
 * Parameters:  cmdChar - A char newly received.
 * Returns:     1 if the received char is a CR (signifying a command has been built).
 *              0 otherwise (command still being assembled).
 *----------------------------------------------------------------------------------------*/
static unsigned int interpBuildCmd( char cmdChar )
{
   ESCAPE_ACTION_T escapeAction;

   //printf(" %02X ", cmdChar);
   if ( cmdChar != CHR_LF ) { /* If not a carriage return. */
      if ( cmdChar == ESCAPE ) {
         escapeAction = getEscapeAction();
         //printf("\naction=%d\n", escapeAction);
         
         switch(escapeAction) {
         case ESC_HIST_PREV:
            if (interpHistPopCmd(ESC_HIST_PREV) == HISTORY_SUCCESS)  /* If command was popped, */
               printf(cmdStrHead);                                   /* display it. */
            break;

         case ESC_HIST_NEXT:
           if (interpHistPopCmd(ESC_HIST_NEXT) == HISTORY_SUCCESS)  /* If command was popped, */
               printf(cmdStrHead);                                  /* display it. */
           break;

         case ESC_CSR_RIGHT:
            if ( cursorPos < cmdStrTail ) {
               printf("%c", *cursorPos);
               cursorPos++;
            }            
            break;

         case ESC_CSR_LEFT:
            if ( cursorPos > cmdStrHead ) {
               printf("%c", CHR_BS);
               cursorPos--;
            }
            break;
            
         case ESC_BACKSPACE:
            if (cmdStrTail != cmdStrHead) {
               /* Erase character if backspace pressed. */
               deleteCmdChar();
            }
            break;

         case ESC_NO_ACTION:
         default:
            break;
         }
      }
      else if ( (cmdChar == CHR_CTRL_D) ) {
         cmdStr[0] = EOF;
         cmdStr[1] = '\0';
         return 1;
      }
      else {
         if ( (cmdChar == CHR_BSK) ) {
            if (cmdStrTail != cmdStrHead) {
               /* Erase character if backspace pressed. */
               deleteCmdChar();
            }
         }
         else { /* Alphanumeric character entered. */
            addCmdChar(cmdChar);
         }
      }
      return 0;
   }
   else { /* Carriage return pressed. */
      *cmdStrTail = '\0';
      printf("\n");
      return 1;
   }
}

/*-----------------------------------------------------------------------------------------
 * Funciton:    cli_restore_terminal
 * Description: Call this on exit to make sure the terminal is restored.
 *----------------------------------------------------------------------------------------*/
void cli_restore_terminal(void)
{
   tcsetattr(fileno(stdin), TCSANOW, &initial_settings);
}

/*-----------------------------------------------------------------------------------------
 * Funciton:    readline_lite
 * Description: The purpose of this file
 * Parameters:  char *prompt: command line prompt
 * Returns:     NULL is EOF dettected.
 *              Otherwise, returns pointer to the command string.
 *----------------------------------------------------------------------------------------*/
char* readline_lite(char *prompt)
{
   signed char   c;
   struct termios new_settings;

   if ( !(lib_initialized) ) {
      interpInit();
      lib_initialized = 1;
   }

   //Set up terminal
   //
   tcgetattr(fileno(stdin), &initial_settings);
   new_settings = initial_settings;
   new_settings.c_lflag &= ~ICANON;
   new_settings.c_lflag &= ~ECHO;
   new_settings.c_cc[VMIN]  = 1;
   new_settings.c_cc[VTIME] = 0;
   if ( tcsetattr(fileno(stdin), TCSANOW, &new_settings) != 0 ) {
      fprintf(stderr, "Unable to open /dev/tty\n");
      return(NULL);
   }

   if ( prompt != NULL ) {
      printf("%s", prompt);
   }
   else {
      printf(">");
   }
   //fflush(stdout);

   // Build the command line
   //
   cmdStrHead = cmdStr;
   cmdStrTail = cmdStr;
   cursorPos  = cmdStr;
   while ( (c = fgetc(stdin)) != EOF ) {
      if ( interpBuildCmd(c) ) {
         break;
      }
   } 

   //Restore terminal
   //
   tcsetattr(fileno(stdin), TCSANOW, &initial_settings);

      
   if (( c == EOF ) || (cmdStr[0] == EOF) ){
      printf("GOT EOF...\n");
      return(NULL);
   }
   if ( cmdStr[0] != '\0' ) { 
      interpHistPushCmd(cmdStr);
   }
   return(cmdStr);
}
