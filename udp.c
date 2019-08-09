#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

//#define MAXBUFFER 8192
#define MAXBUFFER 12
#define ADDRBUFFER 128

int main () {

  // udp server
  int sd;

  // (1) Setup Socket
  sd = socket (AF_INET, SOCK_DGRAM, 0);
    // AF_INET      ->  IPv4 Internet Protocol
    // SOCK_DGRAM   ->  datagram socket (connectionless)
    // SOCK_STREAM  ->  TCP socket
  if ( sd == -1 ) {
    perror ("socket() failed");
    return EXIT_FAILURE;
  }

  printf ("Server socket created on fd: %d\n", sd);

  // (2) Bind socket to a port
  struct sockaddr_in server;
  int length;

  server.sin_family = AF_INET;
  server.sin_addr.s_addr = htonl (INADDR_ANY);
    // INADDR_ANY   ->  allow any remote ip to connect to server

  server.sin_port = htons (0);
    // 0 -> let the kernel assign us a port # to listen on

  if ( bind(sd, (struct sockaddr *) &server, sizeof( server )) < 0 ) {
    perror ("bind() failed");
    return EXIT_FAILURE;
  }

  length = sizeof (server);

  // (3) get the port number that was assigned by bind ()
  if ( getsockname(sd, (struct sockaddr *) &server, (socklen_t *) &length) < 0 ) {
    perror("getsockname() failed.");
    return EXIT_FAILURE;
  }

  printf ("UDP server on port # %d\n", ntohs( server.sin_port ));

  // Application Protocol
  int n;
  char buffer [ MAXBUFFER ];
  char addr_buffer [ ADDRBUFFER ];
  struct sockaddr_in client;
  int len = sizeof(client);
  int backup_len = len;

  while (1) {

    printf ("recieving ...\n");
    n = recvfrom (sd, buffer, MAXBUFFER, 0, (struct sockaddr *) &client, (socklen_t *) &len);
    printf ("sizeof msg: %d\n", n);

    if ( n == -1 ) {
      perror ("recvfrom() failed");
    }
    else {
      printf ("Rcvd datagram from %s port %d\n",
        inet_ntop(AF_INET, &client.sin_addr, addr_buffer, ADDRBUFFER),
        ntohs(client.sin_port));

      printf ("RCVD %d bytes\n", n);
      buffer[n] = '\0';
      printf ("RCVD: [%s]\n", buffer);

      // TODO: send back to client...
      if (sendto (sd, buffer, n, 0, (struct sockaddr *) &client, len) == -1) {
        perror ("sendto() failed");
      }
    }

  }

  printf ("Server shutdown normally.\n");
  return EXIT_SUCCESS;
}
