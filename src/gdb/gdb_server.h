#include <sys/socket.h>
#include "../sparcv8/CPU.h"

int create_server_socket(int port);
void handle_gdb_client(int client_fd, CPU& cpu); 
