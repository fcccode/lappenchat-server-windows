#include "error.h"

#include <winsock2.h>
#include "logmsg.h"


void win_perror
(
 const char * const msg,
 int error_code
)
{
	LPTSTR formatted_error_code;
	
	if ( FormatMessage(
			FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL,
			error_code,
			MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL),
			(LPTSTR)&formatted_error_code,
			0,
			NULL
		) == 0 )
		logmsgf("%s (%i)\n", msg, error_code);
	else
	{
		logmsgf("%s: %i: %s", msg, error_code, formatted_error_code);
		LocalFree(formatted_error_code);
	}
}

void wsa_perror
(
 const char * const msg
)
{
	win_perror(msg, WSAGetLastError());
}

void winapi_perror
(
 const char * const msg
)
{
	win_perror(msg, GetLastError());
}
