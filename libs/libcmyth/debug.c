/*
 * debug.c - functions to produce and control debug output from
 *           libcmyth routines.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <cmyth.h>
#include <cmyth_local.h>

static int debug_level = CMYTH_DBG_NONE;

/*
 * cmyth_dbg_level(int l)
 * 
 * Scope: PUBLIC
 *
 * Description
 *
 * Set the current debug level to the absolute setting 'l'
 * permitting all debug messages with a debug level less
 * than or equal to 'l' to be displayed.
 *
 * Return Value:
 *
 * None.
 */
void
cmyth_dbg_level(int l)
{
	debug_level = l;
}

/*
 * cmyth_dbg_all()
 * 
 * Scope: PUBLIC
 * 
 * Description
 *
 * Set the current debug level so that all debug messages are displayed.
 *
 * Return Value:
 *
 * None.
 */
void
cmyth_dbg_all()
{
	debug_level = CMYTH_DBG_ALL;
}

/*
 * cmyth_dbg_none()
 * 
 * Scope: PUBLIC
 * 
 * Description
 *
 * Set the current debug level so that no debug messages are displayed.
 *
 * Return Value:
 *
 * None.
 */
void
cmyth_dbg_none()
{
	debug_level = CMYTH_DBG_NONE;
}

/*
 * cmyth_dbg()
 * 
 * Scope: PRIVATE (mapped to __cmyth_dbg)
 * 
 * Description
 *
 * Print a debug message of level 'level' on 'stderr' provided that
 * the current debug level allows messages of level 'level' to be
 * printed.
 *
 * Return Value:
 *
 * None.
 */
void
cmyth_dbg(int level, char *fmt, ...)
{
	va_list ap;

	if (level <= debug_level) {
		va_start(ap, fmt);
		vfprintf(stderr, fmt, ap);
		va_end(ap);
	}
}
