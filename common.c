#include "common.h"

#include <winsock2.h>
#include <windows.h>
#include "logmsg.h"
#include "error.h"
#include "server.h"


int start_server
(
 struct lappenchat_server_options lcso,
 HANDLE stop_event
)
{
	int rv;
	if ( WSAStartup(MAKEWORD(2,2), &lcso.wsa_data) == 0 )
	{
		logmsgf("successfully initialized Winsock\nWinsock version offered: %u.%u\nWinsock implementation description: %s\n", LOBYTE(lcso.wsa_data.wVersion), HIBYTE(lcso.wsa_data.wVersion), lcso.wsa_data.szDescription);
		
		rv = lappenchat_server(lcso, stop_event);
		
		if ( WSACleanup() == 0 )
			logmsg("successfully terminated use of Winsock");
		else
			wsa_perror("couldn't terminate use of Winsock");
	}
	else
	{
		logmsg("couldn't initialize Winsock");
		rv = 0;
	}
	
	return rv;
}

size_t
get_proc_n
( void )
{
	SYSTEM_INFO system_info;
	GetSystemInfo(&system_info);
	return system_info.dwNumberOfProcessors;
}
