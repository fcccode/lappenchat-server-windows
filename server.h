#ifndef SERVER_H
#define SERVER_H

#include <stddef.h> // size_t
#include <winsock2.h> // WSADATA, u_short
#include <windows.h> // HANDLE


struct lappenchat_server_options {
	WSADATA wsa_data;
	u_short port;
	size_t threads;
};

int lappenchat_server
(
 struct lappenchat_server_options,
 HANDLE stop_event
);

#endif
