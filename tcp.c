#include <sys/types.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>

#define BUFFER_SIZE 1024

// select example -> https://submitty.cs.rpi.edu/u19/csci4210/display_file?dir=course_materials&path=%2Fvar%2Flocal%2Fsubmitty%2Fcourses%2Fu19%2Fcsci4210%2Fuploads%2Fcourse_materials%2Flectures%2Flec-7-29%2Fserver-select.c

int main () {

  // TCP SERVER

  // (1) create socket for listening
  int sd = socket (PF_INET, SOCK_STREAM, 0);

  if ( sd == -1 ) {
    perror ("socket() failed");
    return EXIT_FAILURE;
  }

  // (2) bind socket to port
  struct sockaddr_in server;
  server.sin_family = PF_INET;
  server.sin_addr.s_addr = htonl (INADDR_ANY);

  unsigned short port = 8123;
  server.sin_port = htons( port );
  int len = sizeof(server);

  if ( bind (sd, (struct sockaddr *)&server, len) == -1 ) {
    perror ("bind() failed");
    return EXIT_FAILURE;
  }

  // (3) Set the port as TCP listener port
  if ( listen(sd, 5) == -1 ) {
    perror ("listen() failed");
    return EXIT_FAILURE;
  }

  printf ("SERVER: TCP listener socket bound to port %d\n", port);

  // (4) manage client connections
  struct sockaddr_in client;
  int fromlen = sizeof(client);

  int n;
  char buffer[BUFFER_SIZE];

  while (1) {
    // waits until a client connects to the server
    printf ("SERVER: Blocked on accept ()\n");
    int newsd = accept (sd, (struct sockaddr *)&client, (socklen_t *)&fromlen);

    printf ("SERVER: Accepted new client connection on newsd %d\n", newsd);

    do {
      printf ("SERVER: Blocked on recv()\n");
      n = recv (newsd, buffer, BUFFER_SIZE, 0);

      if ( n == -1 ){
        perror ("recv() failed");
        return EXIT_FAILURE;
      }

      else if ( n == 0 ) {printf ("SERVER: Rcvd 0 from recv(); closing socket...\n");}
      else {
          buffer[n] = '\0';
          printf ("SERVER: Rcvd message from %s: %s\n",
            inet_ntoa((struct in_addr) client.sin_addr), buffer);

          n = send (newsd, "ACK\n", 4, 0);
          if ( n != 4 ) {
            perror ("send() failed");
            return EXIT_FAILURE;
          }
      }

    } while (n > 0);
    close (newsd);
  }

  return EXIT_SUCCESS;
}
