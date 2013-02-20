#include <sys/socket.h>
#include <sys/un.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

int
main ()
{
    const char * socket_dir;
    int debug_client;
    struct sockaddr_un debug_addr;
    int n_read;

    debug_client = socket (AF_UNIX, SOCK_STREAM, 0);
    if (debug_client < 0) {
        perror ("socket");
        return EXIT_FAILURE;
    }

    socket_dir = getenv ("LXK_RUNTIME_DIR");
    if (! socket_dir)
        socket_dir = ".";

    memset (&debug_addr, 0, sizeof (debug_addr));
    debug_addr.sun_family = AF_UNIX;
    snprintf (debug_addr.sun_path, sizeof (debug_addr.sun_path),
              "%s/membroker.debug", socket_dir);

    if (0 > connect (debug_client, (struct sockaddr *) &debug_addr, sizeof (debug_addr))) {
        perror ("connect");
        return EXIT_FAILURE;
    }

    do {
        char buf[1024];
        do {
            n_read = read (debug_client, buf, sizeof (buf));
        } while (n_read != -1 && errno == EINTR);
        fwrite (buf, n_read, 1, stdout);
    } while (n_read > 0);

    close (debug_client);

    return EXIT_SUCCESS;
}
