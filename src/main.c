#include "dispatch.h"
#include "util-malloc.h"
#include "util-ctype.h"

struct telnet_service
{
    int data_port;
};

/***************************************************************************
 ***************************************************************************/
void
telnet_receive(struct dispatcher_connection *conn, void *userdata, const unsigned char *buf, size_t length)
{
    const char *peeraddr;
    const char *peerport;
    const char *hostaddr;
    const char *hostport;
    dispatcher_get_addrs(conn, &peeraddr, &peerport, &hostaddr, &hostport);
    
    while (length && ISSPACE(buf[length-1]))
        length--;
    
    printf("    [%s]:%s -> [%s]:%s: %.*s\n", peeraddr, peerport, hostaddr, hostport, (unsigned)length, buf);
}

/***************************************************************************
 ***************************************************************************/
void
telnet_event(struct dispatcher_connection *conn, void **userdata, int event, void *eventdata)
{
    struct dispatcher_new_connection *n =(struct dispatcher_new_connection *)eventdata;
    switch (event) {
        case DISPATCH_NEW_CONNECTION:
            printf("[+] [%s]:%s -> [%s]:%s incoming connection\n", n->peeraddr, n->peerport, n->hostaddr, n->hostport);
            *userdata = CALLOC(1, sizeof(struct telnet_service));
            break;
            
        case DISPATCH_END_CONNECTION:
            printf("[+] [%s]:%s -> [%s]:%s closed connection\n", n->peeraddr, n->peerport, n->hostaddr, n->hostport);
            FREE(*userdata);
            break;
            
        case DISPATCH_TIMEOUT_INACTIVITY:
            printf("timeout\n");
            break;
    }
}


int main(int argc, char *argv[])
{
    struct dispatcher *d;
    int err;
    struct telnet_service telnet;
    
    telnet.data_port = 20;
    
    d = dispatcher_create();
    
    err = dispatcher_add_listener(d, &telnet, 2323, telnet_receive, 0, telnet_event);

    for (;;) {
        err = dispatcher_dispatch(d, 100);
        if (err < 0)
            break;
    }
}
