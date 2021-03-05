#include "util-socket.h"

#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h> /* getaddrinfo() et al. */
#include <fcntl.h> /* fcntl(), F_GETFL, O_NONBLOCK */


/**
 * Configures the socket for non-blocking status
 */
int socket_set_nonblocking(int fd)
{
    int flag;
    flag = fcntl(fd, F_GETFL, 0);
    flag |= O_NONBLOCK;
    flag = fcntl(fd, F_SETFL,  flag);
    if (flag == -1)
        return -1;
    else
        return 0;
}

