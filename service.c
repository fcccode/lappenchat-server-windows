#include <winsock2.h>
#include "service.h"

#include <windows.h>
#include "logmsg.h"
#include "error.h"
#include "common.h"
#include "server.h"


typedef struct
{
	SERVICE_STATUS status;
	SERVICE_STATUS_HANDLE status_handle;
	HANDLE stop_event;
} ServiceStuff;

DWORD WINAPI ServiceCtrlHandler
(
 DWORD request,
 DWORD event_type,
 LPVOID event_data,
 LPVOID data
)
{
	ServiceStuff * const service_stuff = (ServiceStuff *)data;
	
	switch ( request )
	{
		case SERVICE_CONTROL_STOP:
		case SERVICE_CONTROL_SHUTDOWN:
			service_stuff->status.dwCurrentState = SERVICE_STOP_PENDING;
			SetServiceStatus(service_stuff->status_handle, &service_stuff->status);
			
			logmsg("received stop request from the Service Control Manager");
			
			if ( SetEvent(service_stuff->stop_event) )
			{
				logmsg("stop event object triggered successfully");
				return NO_ERROR;
			}
			else
			{
				winapi_perror("couldn't trigger the stop event object");
				
				logmsg("couldn't shut down gracefully");
				
				service_stuff->status.dwCurrentState = SERVICE_STOPPED;
				SetServiceStatus(service_stuff->status_handle, &service_stuff->status);
				
				return NO_ERROR;
			}
		case SERVICE_CONTROL_INTERROGATE:
			return NO_ERROR;
		default:
			return ERROR_CALL_NOT_IMPLEMENTED;
	}
}

VOID WINAPI
ServiceMain
(
 int argc,
 char * * argv
)
{
	ServiceStuff service_stuff = {
		.status = {
			.dwServiceType = SERVICE_WIN32,
			.dwCurrentState = SERVICE_START_PENDING,
			.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN,
			.dwWin32ExitCode = 1,
			.dwServiceSpecificExitCode = 0,
			.dwCheckPoint = 0,
			.dwWaitHint = 0
		}
	};
	
	if ( service_stuff.status_handle = RegisterServiceCtrlHandlerEx("lappenchat-server", (LPHANDLER_FUNCTION_EX)ServiceCtrlHandler, &service_stuff) )
	{
		SetServiceStatus(service_stuff.status_handle, &service_stuff.status);
		
		service_stuff.stop_event = CreateEvent(NULL, TRUE, FALSE, NULL);
		if ( service_stuff.stop_event )
		{
			char parameter = 0;
			char * log_path = NULL;
			struct lappenchat_server_options lcso = { 0 };
			
			for ( char * * arg_cur = argv, * * const argv_end = argv+argc ; arg_cur != argv_end ; ++arg_cur )
			{
				char * const arg = *arg_cur;
				if ( parameter )
				{
					switch ( parameter )
					{
						case 'l':
							log_path = arg;
							break;
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
			
			if ( !lcso.port )
				lcso.port = 3144;
			
			if ( !lcso.threads )
				lcso.threads = get_proc_n();
			
			/* FIXME: we should report SERVICE_RUNNING only when (if) everything
			 * has been set up properly. The problem is that there is still setup
			 * work to do on the server side, so it would have to be reported from
			 * there, and that would make it dependent on being a Windows service.
			 * A way to avoid that would be to pass a callback, but that doesn't
			 * look pretty... */
			service_stuff.status.dwCurrentState = SERVICE_RUNNING;
			SetServiceStatus(service_stuff.status_handle, &service_stuff.status);
			
			int rv;
			if ( log_path )
			{
				logout = fopen(log_path, "wb");
				if ( logout )
				{
					logmsg("successfully opened log file");
					
					rv = start_server(lcso, service_stuff.stop_event);
					
					fclose(logout);
				}
				else
					rv = 0;
			}
			else
				rv = start_server(lcso, service_stuff.stop_event);
			
			CloseHandle(service_stuff.stop_event);
			
			service_stuff.status.dwWin32ExitCode = !rv;
		}
		else
			winapi_perror("couldn't create stop event object");
	}
	else
		winapi_perror("couldn't register service control handler");
	
	service_stuff.status.dwCurrentState = SERVICE_STOPPED;
	SetServiceStatus(service_stuff.status_handle, &service_stuff.status);
}

int main(void)
{
	SERVICE_TABLE_ENTRY service_table[] = {
		{
			.lpServiceName = "lappenchat-server",
			.lpServiceProc = (LPSERVICE_MAIN_FUNCTION)ServiceMain
		},
		{
			NULL,
			NULL
		}
	};
	
	/* Use stderr until the log file is opened */
	logout = stderr;
	
	if ( StartServiceCtrlDispatcher(service_table) )
		return EXIT_SUCCESS;
	else
	{
		winapi_perror("error: couldn't connect main thread to the service control manager");
		logmsg("hint: launch the server as a service instead of as a mere program");
		logmsg("hint: create the service with SC.EXE");
		logmsg("hint: start the service using NET.EXE");
		return EXIT_FAILURE;
	}
}
