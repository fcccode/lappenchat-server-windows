#include "logmsg.h"

#include <stdarg.h>
#include <stdio.h>


FILE * logout;

void logmsg
(
 const char * const msg
)
{
	fputs(msg, logout);
	fputc('\n', logout);
	fflush(logout);
}

void logmsgf
(
 const char * const fmt,
 ...
)
{
	va_list arguments;
	va_start(arguments, fmt);
	vfprintf(logout, fmt, arguments);
	fflush(logout);
	va_end(arguments);
}
