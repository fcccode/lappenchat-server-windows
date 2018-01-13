#include "server.h"

#include <assert.h>
#include <stdint.h>
#include <inttypes.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include "logmsg.h"
#include "error.h"

#define SERVER_SOCKETS 2
#define max_clients 62


static void
close_socket
(
 SOCKET s,
 const char * const error_msg
)
{
	if ( closesocket(s) == SOCKET_ERROR )
		wsa_perror(error_msg);
}

enum Phase {
	phase_getting_nickname_length,
	phase_getting_nickname,
	phase_getting_message_length,
	phase_getting_message
};

/* This structure contains information about
 * a single client. It is thus to be associated
 * to the client handle when it (the client handle)
 * gets attached to the completion port (with
 * CreateIoCompletionPort). */
typedef struct {
	char used;
	SOCKET socket;
	enum Phase phase;
	char nickname[32];
	unsigned char nickname_length;
} ClientData;

static size_t
broadcast_message
(
 ClientData * cur,
 const char * buffer,
 const int size
)
{
	size_t clients_sent = 0;
	for ( ClientData * const end = cur + max_clients ; cur != end ; ++cur )
	{
		if ( cur->used )
		{
			int rv;
			size_t sent = 0;
send_more:
			if ( (rv = send(cur->socket, buffer, size, 0)) != SOCKET_ERROR )
			{
				sent += rv;
				if ( sent == size )
					++clients_sent;
				else
				{
					assert(sent < size);
					goto send_more;
				}
			}
			else
				wsa_perror("couldn't send message to client");
		}
	}
	return clients_sent;
}

static SOCKET
get_ipv4_socket
(
 u_short port
)
{
	SOCKET ss_ipv4 = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
	if ( ss_ipv4 != INVALID_SOCKET )
	{
		logmsg("successfully created IPv4 socket");
		
		struct sockaddr_in localhost_ipv4 = {
			.sin_family = AF_INET,
			.sin_addr.s_addr = INADDR_ANY,
			.sin_port = htons(port)
		};
		if ( bind(ss_ipv4, (struct sockaddr *)&localhost_ipv4, sizeof(localhost_ipv4)) != SOCKET_ERROR )
			logmsg("successfully bound IPv4 socket to the local addresses");
		else
		{
			wsa_perror("couldn't bind IPv4 socket");
			closesocket(ss_ipv4);
			ss_ipv4 = INVALID_SOCKET;
		}
	}
	else
		wsa_perror("couldn't create IPv4 socket");
	
	return ss_ipv4;
}

static SOCKET
get_ipv6_socket
(
 u_short port
)
{
	SOCKET ss_ipv6 = WSASocketW(AF_INET6, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
	if ( ss_ipv6 != INVALID_SOCKET )
	{
		logmsg("successfully created IPv6 socket");
		
		/* By disabling the IPV6_V6ONLY option, it would be possible
		 * to handle both IPv4 and IPv6 with the same socket. */
		//setsockopt(ss_ipv6, IPPROTO_IPV6, IPV6_V6ONLY, (const char *)(DWORD[]){0}, sizeof(DWORD));
		
		struct sockaddr_in6 localhost_ipv6 = {
			.sin6_family = AF_INET6,
			.sin6_addr = in6addr_any,
			.sin6_port = htons(port),
			.sin6_scope_id = 0
		};
		if ( bind(ss_ipv6, (struct sockaddr *)&localhost_ipv6, sizeof(localhost_ipv6)) != SOCKET_ERROR )
			logmsg("successfully bound IPv6 socket to the local addresses");
		else
		{
			wsa_perror("couldn't bind IPv6 socket");
			closesocket(ss_ipv6);
			ss_ipv6 = INVALID_SOCKET;
		}
	}
	else
		wsa_perror("couldn't create IPv6 socket");
	
	return ss_ipv6;
}

static int
setup_listen_events
(
 SOCKET * server_sockets,
 size_t server_sockets_n,
 WSAEVENT * event_handles_1,
 WSAEVENT * event_handles_2
)
{
	SOCKET * const list_end = server_sockets + server_sockets_n;
	for ( ; server_sockets != list_end ; ++server_sockets )
	{
		WSAEVENT event_handle = WSACreateEvent();
		if ( event_handle != WSA_INVALID_EVENT )
		{
			if ( WSAEventSelect(*server_sockets, event_handle, FD_ACCEPT) == 0 )
			{
				*event_handles_1++ = event_handle;
				*event_handles_2++ = event_handle;
			}
			else
				return 0;
		}
		else
			return 0;
	}
	
	return 1;
}

static int
lappenchat_server_inner_event
(
 HANDLE stop_event,
 SOCKET * server_sockets,
 DWORD server_sockets_n,
 struct sockaddr * * client_addr,
 int * client_addr_sizes
)
{
	int rv;
	
	static_assert(SERVER_SOCKETS + max_clients <= WSA_MAXIMUM_WAIT_EVENTS, "the sum of the maximum number of sockets used by the server and the maximum number of client sockets exceeds the maximum supported by Winsock");
	WSAEVENT event_handles_1[max_clients + SERVER_SOCKETS];
	WSAEVENT event_handles_2[max_clients + SERVER_SOCKETS];
	WSAEVENT * event_handles_fore;
	WSAEVENT * event_handles_back;
	
	rv = 0;
	event_handles_fore = event_handles_1;
	event_handles_back = event_handles_2;
	
	if ( setup_listen_events(server_sockets, server_sockets_n, event_handles_1, event_handles_2) )
	{
		SOCKET client_sockets_1[max_clients];
		SOCKET client_sockets_2[max_clients];
		SOCKET * client_sockets_fore;
		SOCKET * client_sockets_back;
		DWORD clients_n;
		
		client_sockets_fore = client_sockets_1;
		client_sockets_back = client_sockets_2;
		clients_n = 0;
		
		while ( WaitForSingleObject(stop_event, 0) != WAIT_OBJECT_0 )
		{
			DWORD poll_code;
			WSAEVENT * client_event_handles_fore;
			WSAEVENT * client_event_handles_back;
			
			client_event_handles_fore = event_handles_fore + server_sockets_n;
			client_event_handles_back = event_handles_back + server_sockets_n;
			
			poll_code = WSAWaitForMultipleEvents(server_sockets_n + clients_n, event_handles_fore, FALSE, 3000, FALSE);
			if ( poll_code >= WSA_WAIT_EVENT_0 && poll_code <= WSA_WAIT_EVENT_0+server_sockets_n+clients_n+1 )
			{
				WSAEVENT * client_event_handles_end;
				const size_t event_index = poll_code - WSA_WAIT_EVENT_0;
				
				client_event_handles_end = client_event_handles_fore + clients_n;
				
				if ( event_index < server_sockets_n )
				{
					/* A new client wants to connect through one of the
					 * server sockets. Let's check them and handle the
					 * request. */
					
					SOCKET * server_socket;
					WSAEVENT * event_handle;
					for ( event_handle = event_handles_fore, server_socket = server_sockets ; event_handle != client_event_handles_fore ; ++server_socket, ++event_handle )
					{
						WSANETWORKEVENTS wsa_events;
						
						WSAEnumNetworkEvents(*server_socket, *event_handle, &wsa_events);
						WSAResetEvent(*event_handle);
						
						if ( wsa_events.lNetworkEvents & FD_ACCEPT )
						{
							logmsg("connection request received");
							
							SOCKET client_socket = accept(*server_socket, client_addr[event_index], &client_addr_sizes[event_index]);
							if ( client_socket != INVALID_SOCKET )
							{
								WSAEVENT client_event = WSACreateEvent();
								if ( client_event != WSA_INVALID_EVENT )
								{
									if ( WSAEventSelect(client_socket, client_event, FD_READ | FD_CLOSE) == 0 )
									{
										client_sockets_fore[clients_n] = client_socket;
										client_event_handles_fore[clients_n] = client_event;
										
										++client_event_handles_end;
										++clients_n;
										
										logmsg("client connected");
									}
									else
									{
										wsa_perror("WSAEventSelect failed");
										
										WSACloseEvent(client_event);
										closesocket(client_socket);
									}
								}
								else
								{
									wsa_perror("WSACreateSelect failed");
									
									closesocket(client_socket);
								}
							}
							else
							{
								wsa_perror("couldn't accept connection request");
							}
						}
					}
				}
				
				{
					WSAEVENT * out_event;
					SOCKET * out_socket;
					WSAEVENT * cur;
					SOCKET * cur_socket;
					
					out_event = client_event_handles_back;
					out_socket = client_sockets_back;
					
					for ( cur = client_event_handles_fore, cur_socket = client_sockets_fore ; cur != client_event_handles_end ; ++cur, ++cur_socket )
					{
						WSANETWORKEVENTS wsa_events;
						WSAEVENT client_event_handle;
						SOCKET client_socket;
						
						client_event_handle = *cur;
						client_socket = *cur_socket;
						
						WSAEnumNetworkEvents(client_socket, client_event_handle, &wsa_events);
						WSAResetEvent(client_event_handle);
						
						if ( wsa_events.lNetworkEvents & FD_READ )
						{
							char buffer[512];
							int size = 512;
							int recv_size = recv(client_socket, buffer, size, 0);
							if ( recv_size != SOCKET_ERROR )
							{
								/*
								size_t message_length = *buffer;
								message_length <<= 8;
								message_length += buffer[1];
								*/
								
								buffer[recv_size] = '\0';
								logmsg(buffer);
								//broadcast_message(client_sockets_fore, clients_n, buffer, size);
							}
							else
							{
								wsa_perror("couldn't receive packet from client");
							}
							/*
							char c;
							int recv_size = recv(client_socket, &c, 1, 0);
							if ( recv_size != SOCKET_ERROR )
							{
								char msg[] = "Received:  ";
								msg[10] = c;
								logmsg(msg);
							}
							else
							{
								wsa_perror("couldn't receive packet from client");
							}
							*/
						}
						
						if ( wsa_events.lNetworkEvents & FD_CLOSE )
						{
							close_socket(client_socket, "couldn't close client socket");
							WSACloseEvent(client_event_handle);
							
							--clients_n;
							
							logmsg("client disconnected");
						}
						else
						{
							*out_event++ = client_event_handle;
							*out_socket++ = client_socket;
						}
					}
				}
				
				/* Swap fore- and back-pointers */
				{
					WSAEVENT * temp_e;
					SOCKET * temp_s;
					
					temp_e = event_handles_fore;
					temp_s = client_sockets_fore;
					
					event_handles_fore = event_handles_back;
					client_sockets_fore = client_sockets_back;
					
					event_handles_back = temp_e;
					client_sockets_back = temp_s;
				}
			}
		}
		
		logmsg("exited server loop");
		
		{
			SOCKET * cur;
			SOCKET * const end = client_sockets_fore + clients_n;
			for ( cur = client_sockets_fore ; cur != end ; ++cur )
				closesocket(*cur);
		}
		
		logmsg("closed remaining sockets");
		logmsg("ended remaining connections");
		
		rv = 1;
	}
	
	{
		WSAEVENT * cur;
		WSAEVENT * const end = event_handles_fore + server_sockets_n + max_clients;
		for ( cur = event_handles_fore ; cur != end ; ++cur )
		{
			if ( *cur != WSA_INVALID_EVENT )
				WSACloseEvent(*cur);
		}
	}
	
	logmsg("freed remaining event objects");
	
	return rv;
}

/* An object of this structure is passed to
 * send and receive routines and got back with
 * GetQueuedCompletionPortStatus. It contains
 * information pertaining to an I/O operation. */
typedef struct {
	/* This member must be the first one - either
	 * that, or use CONTAINING_RECORD */
	WSAOVERLAPPED wsa_overlapped;
	char message_length;
	unsigned char received;
	char buffer[290];
} OperationData;

/* An object of this type shall be shared
 * by the main thread and the worker threads. */
typedef struct {
	HANDLE completion_port;
	HANDLE client_pool_mutex;
	ClientData clients[max_clients];
} SharedStructures;

DWORD WINAPI
worker_thread
(
 LPVOID data
)
{
	SharedStructures * shared = (SharedStructures *)data;
	DWORD thread_id = GetCurrentThreadId();
	DWORD flags = 0;
	
	logmsgf("worker thread #%"PRIuLEAST32": ready\n", thread_id);
	
	for ( ; ; )
	{
		DWORD size;
		ClientData * client_data;
		OperationData * operation_data;
		if ( GetQueuedCompletionStatus(shared->completion_port, &size, (PULONG_PTR)&client_data, (LPOVERLAPPED *)&operation_data, INFINITE) )
		{
			logmsgf("worker thread #%"PRIuLEAST32": completion notification dequeued successfully\n", thread_id);
			if ( size )
			{
				switch ( client_data->phase )
				{
					case phase_getting_nickname_length:
					{
						assert(size == 1);
						
						logmsgf("new client's nickname length: %zu\n", client_data->nickname_length);
						
						client_data->phase = phase_getting_nickname;
						
						operation_data->received = 0;
						
						/* Queue another recv to get the part of the nickname
						 * that we are still missing */
						WSABUF buffer_info = {
							.buf = client_data->nickname,
							.len = client_data->nickname_length
						};
						if ( WSARecv(client_data->socket, &buffer_info, 1, NULL, &flags, &(operation_data->wsa_overlapped), NULL) == SOCKET_ERROR )
						{
							int error_code = WSAGetLastError();
							if ( error_code != WSA_IO_PENDING )
							{
								win_perror("couldn't queue next recv", error_code);
								/* FIXME: disconnect client. We couldn't queue another
								 * recv operation, so it's not like we'll be getting
								 * any further message from this client. */
							}
						}
						
						break;
					}
					
					case phase_getting_nickname:
					{
						operation_data->received += (char)size;
						
						logmsgf("received %zu additional bytes of nickname, until now: %.*s\n", operation_data->received, operation_data->received, client_data->nickname);
						
						if ( operation_data->received == client_data->nickname_length )
						{
							logmsgf("new client connected: %.*s\n", client_data->nickname_length, client_data->nickname);
							
							client_data->phase = phase_getting_message_length;
							
							/* Queue a recv for the client's first message */
							WSABUF buffer_info = {
								.buf = &(operation_data->message_length),
								.len = 1
							};
							if ( WSARecv(client_data->socket, &buffer_info, 1, NULL, &flags, &(operation_data->wsa_overlapped), NULL) == SOCKET_ERROR )
							{
								int error_code = WSAGetLastError();
								if ( error_code != WSA_IO_PENDING )
								{
									win_perror("couldn't queue next recv", error_code);
									/* FIXME: disconnect client. We couldn't queue another
									 * recv operation, so it's not like we'll be getting
									 * any further message from this client. */
								}
							}
						}
						else
						{
							assert(operation_data->received < client_data->nickname_length);
							
							/* Queue another recv to get the part of the nickname
							 * that we are still missing */
							WSABUF buffer_info = {
								.buf = client_data->nickname + operation_data->received,
								.len = client_data->nickname_length - operation_data->received
							};
							int error_code = WSARecv(client_data->socket, &buffer_info, 1, NULL, &flags, &(operation_data->wsa_overlapped), NULL);
							assert(error_code == SOCKET_ERROR);
							error_code = WSAGetLastError();
							if ( error_code != WSA_IO_PENDING )
							{
								win_perror("couldn't queue next recv", error_code);
								/* FIXME: disconnect client. We couldn't queue another
								 * recv operation, so it's not like we'll be getting
								 * any further message from this client. */
							}
						}
						
						break;
					}
					
					case phase_getting_message_length:
					{
						logmsgf("worker thread #%"PRIuLEAST32": %.*s reports having sent a message %i bytes long\n", thread_id, client_data->nickname_length, client_data->nickname, operation_data->message_length);
						
						/* Reset received for the next (first) message */
						operation_data->received = 0;
						
						client_data->phase = phase_getting_message;
						
						WSABUF buffer_info = {
							.buf = operation_data->buffer + sizeof(client_data->nickname) + 2,
							.len = operation_data->message_length
						};
						if ( WSARecv(client_data->socket, &buffer_info, 1, NULL, &flags, &(operation_data->wsa_overlapped), NULL) == SOCKET_ERROR )
						{
							int error_code = WSAGetLastError();
							if ( error_code != WSA_IO_PENDING )
							{
								win_perror("couldn't queue next recv", error_code);
								/* FIXME: disconnect client. We couldn't queue another
								 * recv operation, so it's not like we'll be getting
								 * any further message from this client. */
							}
						}
						
						break;
					}
					
					case phase_getting_message:
					{
						operation_data->received += size;
						
						if ( operation_data->received == operation_data->message_length )
						{
							logmsgf("worker thread #%"PRIuLEAST32": got complete message from %.*s (%u bytes long): %.*s\n", thread_id, client_data->nickname_length, client_data->nickname, operation_data->message_length, operation_data->message_length, operation_data->buffer+1+sizeof(client_data->nickname)+1);
							
							*(operation_data->buffer + sizeof(client_data->nickname) - client_data->nickname_length) = client_data->nickname_length;
							memcpy(operation_data->buffer + sizeof(client_data->nickname) - client_data->nickname_length + 1, client_data->nickname, client_data->nickname_length);
							*(operation_data->buffer + sizeof(client_data->nickname) + 1) = operation_data->message_length;
							
							if ( WaitForSingleObject(shared->client_pool_mutex, INFINITE) == WAIT_OBJECT_0 )
							{
								size_t clients_sent = broadcast_message(shared->clients, operation_data->buffer + sizeof(client_data->nickname) - client_data->nickname_length, 1 + client_data->nickname_length + 1 + operation_data->message_length);
								
								ReleaseMutex(shared->client_pool_mutex);
								
								if ( clients_sent )
									logmsgf("worker thread #%"PRIuLEAST32": message sent to %zu clients\n", thread_id, clients_sent);
								else
									logmsgf("worker thread #%"PRIuLEAST32": couldn't send message to any client\n", thread_id);
							}
							else
								winapi_perror("couldn't lock client pool mutex");
							
							client_data->phase = phase_getting_message_length;
							
							/* Queue a recv for the client's next message */
							WSABUF buffer_info = {
								.buf = &(operation_data->message_length),
								.len = 1
							};
							if ( WSARecv(client_data->socket, &buffer_info, 1, NULL, &flags, &(operation_data->wsa_overlapped), NULL) == SOCKET_ERROR )
							{
								int error_code = WSAGetLastError();
								if ( error_code != WSA_IO_PENDING )
								{
									win_perror("couldn't queue next recv", error_code);
									/* FIXME: disconnect client. We couldn't queue another
									 * recv operation, so it's not like we'll be getting
									 * any further message from this client. */
								}
							}
						}
						else
						{
							assert(operation_data->received < operation_data->message_length);
							
							logmsgf("worker thread #%"PRIuLEAST32": getting message from %.*s: got %i bytes until now\n", thread_id, client_data->nickname_length, client_data->nickname, operation_data->received);
							
							WSABUF buffer_info = {
								.buf = operation_data->buffer + sizeof(client_data->nickname) + 2 + operation_data->received,
								.len = operation_data->message_length - operation_data->received
							};
							if ( WSARecv(client_data->socket, &buffer_info, 1, NULL, &flags, &(operation_data->wsa_overlapped), NULL) == SOCKET_ERROR )
							{
								int error_code = WSAGetLastError();
								if ( error_code != WSA_IO_PENDING )
								{
									win_perror("couldn't queue next recv", error_code);
									/* FIXME: disconnect client. We couldn't queue another
									 * recv operation, so it's not like we'll be getting
									 * any further message from this client. */
								}
							}
						}
						
						break;
					}
				}
			}
			else
			{
				logmsg("client disconnected");
				if ( WaitForSingleObject(shared->client_pool_mutex, INFINITE) == WAIT_OBJECT_0 )
				{
					client_data->used = 0;
					closesocket(client_data->socket);
					
					ReleaseMutex(shared->client_pool_mutex);
					
					free(operation_data);
					
					logmsg("client object released");
				}
			}
		}
		else
		{
			DWORD error_code = GetLastError();
			switch ( error_code )
			{
				case ERROR_ABANDONED_WAIT_0:
					logmsgf("worker thread #%"PRIuLEAST32": received request to shut down\n", thread_id);
					return EXIT_SUCCESS;
				case ERROR_NETNAME_DELETED:
					logmsg("client disconnected");
					if ( WaitForSingleObject(shared->client_pool_mutex, INFINITE) == WAIT_OBJECT_0 )
					{
						client_data->used = 0;
						client_data->nickname_length = 0;
						closesocket(client_data->socket);
						
						ReleaseMutex(shared->client_pool_mutex);
						
						free(operation_data);
						
						logmsg("client object released");
					}
					break;
				default:
					win_perror("couldn't retrieve completion packet from the completion port queue", error_code);
			}
		}
	}
}

static ClientData *
find_free_slot
(
 ClientData * cur
)
{
	for ( ClientData * const end = cur + max_clients ; cur != end ; ++cur )
		if ( !cur->used )
			return cur;
	
	return NULL;
}

static int
lappenchat_server_inner_completionport
(
 HANDLE stop_event,
 SOCKET * server_sockets,
 DWORD server_sockets_n,
 size_t threads
)
{
	int rv = 1;
	WSAEVENT event_handles[SERVER_SOCKETS + 1];
	SharedStructures shared = {0};
	SOCKET * const sockets_end = server_sockets + server_sockets_n;
	WSAEVENT * const server_event_handles = event_handles + 1;
	WSAEVENT * const event_handles_end = event_handles + server_sockets_n + 1;
	
	{
		WSAEVENT * event_handles_ptr = server_event_handles;
		for ( SOCKET * sockets_cur = server_sockets ; sockets_cur != sockets_end ; ++sockets_cur )
		{
			WSAEVENT event_handle = WSACreateEvent();
			if ( event_handle != WSA_INVALID_EVENT )
			{
				if ( WSAEventSelect(*sockets_cur, event_handle, FD_ACCEPT) == 0 )
					*event_handles_ptr++ = event_handle;
				else
					rv = 0;
			}
			else
			{
				winapi_perror("couldn't create event for one of the server sockets");
				rv = 0;
			}
		}
	}
	
	if ( shared.completion_port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0) )
	{
		size_t created = 0;
		/* We create threads-1 threads because our main thread does do something
		 * and shall thus count as well. What it does is handle incoming connection
		 * requests and accept them. This we could handle in the worker threads too
		 * if we used AcceptEx. */
		for ( size_t i = 0, threads_to_create = threads-1 ; i < threads_to_create ; ++i )
		{
			HANDLE thread;
			if ( thread = CreateThread(NULL, 0, worker_thread, &shared, 0, NULL) )
			{
				++created;
				CloseHandle(thread);
			}
			else
				winapi_perror("couldn't create worker thread");
		}
		
		/* Don't fail if at least one thread could be created */
		if ( created )
			logmsgf("successfully spawned %zu threads\n", created);
		else
		{
			logmsg("error: couldn't create any worker threads");
			rv = 0;
		}
	}
	else
	{
		winapi_perror("couldn't create completion port");
		rv = 0;
	}
	
	if ( !(shared.client_pool_mutex = CreateMutex(NULL, FALSE, NULL)) )
	{
		winapi_perror("couldn't create mutex for the client pool");
		rv = 0;
	}
	
	if ( rv )
	{
		/* Set the sockets in listening state */
		{
			size_t count = 0;
			for ( SOCKET * cur = server_sockets ; cur != sockets_end ; ++cur )
			{
				if ( listen(*cur, SOMAXCONN) != SOCKET_ERROR )
					++count;
				else
					wsa_perror("couldn't put server socket to listen");
			}
			if ( count == server_sockets_n )
				logmsgf("all %zu server sockets listening\n", count);
			else if ( count > 0 )
				logmsgf("%zu server sockets listening\n", count);
			else
			{
				assert(count == 0);
				logmsg("error: couldn't put any of the server sockets to listen");
				rv = 0;
			}
		}
		
		if ( rv )
		{
			DWORD events_n = server_sockets_n + 1;
			DWORD max_to_compare = WSA_WAIT_EVENT_0 + events_n + 1;
			DWORD flags = 0; // FIXME !!!
			
			*event_handles = stop_event;
			
			/* Perhaps we should report SERVICE_RUNNING now */
			
			for ( ; ; )
			{
				assert(event_handles[0] == stop_event);
				DWORD poll_code = WSAWaitForMultipleEvents(events_n, event_handles, FALSE, WSA_INFINITE, FALSE);
				if ( poll_code >= WSA_WAIT_EVENT_0 && poll_code <= max_to_compare )
				{
					const size_t event_index = poll_code - WSA_WAIT_EVENT_0;
					if ( event_index == 0 )
					{
						logmsg("server shutdown event set");
						break;
					}
					else
					{
						SOCKET * server_socket;
						WSAEVENT * event_handle;
						for ( event_handle = server_event_handles, server_socket = server_sockets ; event_handle != event_handles_end ; ++server_socket, ++event_handle )
						{
							WSANETWORKEVENTS wsa_events;
							
							WSAEnumNetworkEvents(*server_socket, *event_handle, &wsa_events);
							WSAResetEvent(*event_handle);
							
							if ( wsa_events.lNetworkEvents & FD_ACCEPT )
							{
								logmsg("received connection request");
								
								if ( WaitForSingleObject(shared.client_pool_mutex, INFINITE) == WAIT_OBJECT_0 )
								{
									ClientData * const client_data = find_free_slot(shared.clients);
									if ( client_data )
									{
										client_data->phase = phase_getting_nickname_length;
										client_data->socket = accept(*server_socket, NULL, NULL);
										if ( client_data->socket != INVALID_SOCKET )
										{
											/* FIXME: log connector address ("accepted connection attempt from x.x.x.x / y:y:y: ...") */
											logmsg("accepted connection request");
											
											if ( CreateIoCompletionPort((HANDLE)client_data->socket, shared.completion_port, (ULONG_PTR)client_data, 0) )
											{
												/* calloc ensures that the memory chunk will be zero-filled */
												OperationData * const operation_data = calloc(1, sizeof(*operation_data));
												if ( operation_data )
												{
													logmsg("new client attached to the completion port");
													
													WSABUF buffer_info = {
														.buf = &(client_data->nickname_length),
														.len = 1
													};
													
													operation_data->received = 0;
													
													switch ( WSARecv(client_data->socket, &buffer_info, 1, NULL, &flags, &(operation_data->wsa_overlapped), NULL) )
													{
														case SOCKET_ERROR:
															{
																int error_code = WSAGetLastError();
																if ( error_code == WSA_IO_PENDING )
																{
														case 0:
																	client_data->used = 1;
																}
																else
																{
																	win_perror("couldn't queue initial recv", error_code);
																	closesocket(client_data->socket);
																	free(operation_data);
																}
															}
													}
												}
												else
												{
													logmsg("couldn't allocate memory for initial recv's data");
													closesocket(client_data->socket);
												}
											}
											else
											{
												winapi_perror("couldn't attach client connection socket to the completion port");
												closesocket(client_data->socket);
											}
										}
										else
										{
											wsa_perror("couldn't accept connection request");
										}
									}
									else
									{
										logmsg("no free slot for new client's data");
									}
									
									ReleaseMutex(shared.client_pool_mutex);
								}
							}
						}
					}
				}
			}
			
			logmsg("main server loop exited");
		}
	}
	
	if ( shared.client_pool_mutex )
	{
		if ( !CloseHandle(shared.client_pool_mutex) )
			winapi_perror("couldn't dispose of client pool mutex");
	}
	
	if ( shared.completion_port )
	{
		if ( CloseHandle(shared.completion_port) )
			logmsg("successfully disposed of completion port");
		else
			winapi_perror("couldn't dispose of completion port");
	}
	
	for ( WSAEVENT * cur = server_event_handles ; cur != event_handles_end ; ++cur )
	{
		if ( *cur != WSA_INVALID_EVENT )
		{
			if ( WSACloseEvent(*cur) == FALSE )
				wsa_perror("couldn't close server event object");
		}
	}
	
	return rv;
}

int lappenchat_server
(
 struct lappenchat_server_options lcso,
 HANDLE stop_event
)
{
	int rv = 0;
	if ( LOBYTE(lcso.wsa_data.wVersion) == 2 && HIBYTE(lcso.wsa_data.wVersion) == 2 )
	{
		/* The server handles connections over a variety of protocols,
		 * each of which is handled with a dedicated socket. */
		SOCKET server_sockets[SERVER_SOCKETS];
		SOCKET * server_sockets_entry;
		
		server_sockets_entry = server_sockets;
		
		SOCKET ss_ipv4 = get_ipv4_socket(lcso.port);
		if ( ss_ipv4 != INVALID_SOCKET )
			*server_sockets_entry++ = ss_ipv4;
		
		SOCKET ss_ipv6 = get_ipv6_socket(lcso.port);
		if ( ss_ipv6 != INVALID_SOCKET )
			*server_sockets_entry++ = ss_ipv6;
		
		if ( server_sockets_entry != server_sockets )
		{
			/* At least one socket has been set up successfully */
			
			rv = lappenchat_server_inner_completionport(stop_event, server_sockets, (DWORD)(server_sockets_entry-server_sockets), lcso.threads);
			
			if ( ss_ipv4 != INVALID_SOCKET )
			{
				if ( closesocket(ss_ipv4) != SOCKET_ERROR )
					logmsg("successfully closed IPv4 socket");
				else
					wsa_perror("couldn't close IPv4 socket");
			}
			if ( ss_ipv6 != INVALID_SOCKET )
			{
				if ( closesocket(ss_ipv6) != SOCKET_ERROR )
					logmsg("successfully closed IPv6 socket");
				else
					wsa_perror("couldn't close IPv6 socket");
			}
		}
		else
			logmsg("couldn't establish any socket for the server");
	}
	else
		logmsg("unsuitable Winsock version");
	
	return rv;
}
