#include <winsock2.h>
#include <windows.h>
#include "logmsg.h"
#include "error.h"
#include "common.h"
#include "server.h"


static HANDLE stop_event;

BOOL WINAPI ConsoleCtrlHandler
(
 DWORD signal
)
{
	switch ( signal )
	{
		case CTRL_C_EVENT:
			return SetEvent(stop_event);
		default:
			return 0;
	}
}

int main
(
 int argc,
 char * * argv
)
{
	int rv = 0;
	char parameter = 0;
	struct lappenchat_server_options lcso = { 0 };
	
	logout = stderr;
	
	for ( char * * arg_cur = argv, * * const argv_end = argv+argc ; arg_cur != argv_end ; ++arg_cur )
	{
		char * const arg = *arg_cur;
		if ( parameter )
		{
			switch ( parameter )
			{
				/* FIXME: strtol parses a sign ('+' or '-') as well.
				 * This should actually be prohibited here, as a "negative
				 * port" or a "negative number of threads" don't make
				 * sense. */
				case 'p':
					/* Here, we should also check that the number
					 * provided doesn't exceed the maximum port #. */
					lcso.port = (u_short)strtol(arg, NULL, 10);
					break;
				case 't':
					lcso.threads = strtol(arg, NULL, 10);
					break;
			}
			parameter = 0;
		}
		else
		if ( *arg == '-' )
			parameter = arg[1];
	}
	
	if ( stop_event = CreateEvent(NULL, TRUE, FALSE, NULL) )
	{
		if ( SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE) )
		{
			if ( !lcso.port )
				lcso.port = 3144;
			
			if ( !lcso.threads )
				lcso.threads = get_proc_n();
			
			rv = start_server(lcso, stop_event);
		}
		else
			winapi_perror("couldn't set console control handler");
		
		CloseHandle(stop_event);
	}
	else
		winapi_perror("couldn't create stop event object");
	
	return !rv;
}
