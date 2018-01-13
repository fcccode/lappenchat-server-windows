#include <stddef.h> // size_t
#include <winsock2.h>
#include <windows.h> // HANDLE
#include "server.h" // struct lappenchat_server_options


int start_server
(
 struct lappenchat_server_options,
 HANDLE
);

size_t get_proc_n
(void);
