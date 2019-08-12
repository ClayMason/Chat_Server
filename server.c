#include <sys/types.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <string.h>

/* APPLICATION PROTOCOL
 *  UDP & TCP Commands: WHO, BROADCAST
 *  TCP Only Commands: LOGIN, LOGOUT, SEND,
*/
#define CLIENT_CONNECTIONS 100
#define BUFFER_SIZE 1024
#define BACKLOG 10
#define ADDRBUFFER 128

char** user_database;
int* user_fds;
int user_db_index;
int user_db_size;
pthread_mutex_t user_db_mutex = PTHREAD_MUTEX_INITIALIZER;
// pthread_mutex_lock(&user_db_mutex) & pthread_mutex_unlock(&user_db_mutex)
char* udp_cmd_lst[2];
char* tcp_cmd_lst[5];

char ** snag (char* message, int* size);
short command_index (char* message, char** command_list, int cmd_size);
void * tcp_client_enter (void* args);

int main (int argc, char** argv) {
  // parse arguments
  if ( argc < 2 ) {
    fprintf (stderr, "MAIN: ERROR invalid number of arguments.\n");
    return EXIT_FAILURE;
  }

  int port;
  port = atoi(*(argv+1));
  printf ("Setting up server on port: %d\n", port);

  // setup commands list
  user_db_index = 0;
  user_db_size = 4;

  user_database = (char**) calloc(user_db_size, sizeof(char*));
  user_fds = (int*) calloc(user_db_size, sizeof(int));

  char login_cmd[] = "LOGIN";
  char who_cmd[] = "WHO";
  char logout_cmd[] = "LOGOUT";
  char send_cmd[] = "SEND";
  char broadcast_cmd[] = "BROADCAST";

  udp_cmd_lst[0] = broadcast_cmd;
  udp_cmd_lst[1] = who_cmd;

  tcp_cmd_lst[0] = login_cmd;
  tcp_cmd_lst[1] = who_cmd;
  tcp_cmd_lst[2] = logout_cmd;
  tcp_cmd_lst[3] = send_cmd;
  tcp_cmd_lst[4] = broadcast_cmd;

  // setup the server sockets
  int tcp_sd = socket (AF_INET, SOCK_STREAM, 0);
  int udp_sd = socket (AF_INET, SOCK_DGRAM, 0);

  if ( tcp_sd == -1 ) {
    perror ("socket() failed");
    return EXIT_FAILURE;
  }
  if ( udp_sd == -1 ) {
    perror ("socket() failed");
    return EXIT_FAILURE;
  }

  // bind the sockets to a port
  // udp bind first
  struct sockaddr_in udp_server;
  int udp_length;

  udp_server.sin_family = AF_INET;
  udp_server.sin_addr.s_addr = htonl (INADDR_ANY);
  udp_server.sin_port = htons (0); // TODO: set the port to the port given in the args
  if ( bind(udp_sd, (struct sockaddr *) &udp_server, sizeof(udp_server)) < 0 ) {
    perror ("bind() failed");
    return EXIT_FAILURE;
  }

  // TODO: remove getsockname() after i change to specified port since we know which port it
  // will be on.
  udp_length = sizeof (udp_server);

  if ( getsockname(udp_sd, (struct sockaddr *) &udp_server, (socklen_t *) &udp_length) < 0 ) {
    perror ("getsockname() failed");
    return EXIT_FAILURE;
  }

  printf ("UDP server successfuly setup on port: %d\n", ntohs( udp_server.sin_port ));


  // tcp bind next
  struct sockaddr_in tcp_server;
  int tcp_length;

  tcp_server.sin_family = AF_INET;
  tcp_server.sin_addr.s_addr = htonl (INADDR_ANY);
  tcp_server.sin_port = htons ( port );
  tcp_length = sizeof (tcp_server);

  if ( bind (tcp_sd, (struct sockaddr *) &tcp_server, tcp_length) < 0 ) {
    perror ("bind() failed");
    return EXIT_FAILURE;
  }

  if ( listen(tcp_sd, BACKLOG) == -1 ) {
    perror ("listen() failed");
    return EXIT_FAILURE;
  }

  printf ("TCP server successfully setup on port: %d\n", port);

  // setup the client connections
  fd_set readfds;

  // setup client
  char buffer[BUFFER_SIZE];
  char addr_buffer [ADDRBUFFER];
  struct sockaddr_in client;
  int fromlen = sizeof (client);
  int n;

  while (1) {
    // set the readfds to include the tcp socket discriptor and all the client socket descriptors
    // from udp.
    FD_ZERO( &readfds );
    FD_SET (tcp_sd, &readfds);
    FD_SET (udp_sd, &readfds);

    // select from any of the socket descriptors that are ready
    int ready = select(FD_SETSIZE, &readfds, 0, 0, 0);

    if ( ready == -1 ) {
      perror ("select() failed");
      return EXIT_FAILURE;
    }
    printf ("%d descriptors are ready.\n", ready);
    // check if activity on the tcp listener descriptor
    if ( FD_ISSET(tcp_sd, &readfds)) {

      // make a new thread at this point...
      int client_sd = accept (tcp_sd,
        (struct sockaddr*) &client,
        (socklen_t *)&fromlen);

      printf ("MAIN: Rcvd incoming TCP connection from: %s\n", inet_ntoa((struct in_addr) client.sin_addr));
      // -> pass to thread now ...
      pthread_t thread_id;
      pthread_create (&thread_id, NULL,
        tcp_client_enter, (void*) &client_sd);

      // the client should now be in the new thread
    }

    if ( FD_ISSET(udp_sd, &readfds)) {
      // udp datagram recieved
      // call recieve ...
      n = recvfrom (udp_sd, buffer, BUFFER_SIZE, 0, (struct sockaddr *) &client, (socklen_t *)&fromlen);
      printf ("MAIN: Rcvd incoming UDP datagram from: %s:%d\n",
      inet_ntop(AF_INET, &client.sin_addr, addr_buffer, ADDRBUFFER),
      ntohs(client.sin_port));
    }

  }

  // free and close everything
  free (user_database);
  free (user_fds);

  return EXIT_SUCCESS;
}

void * tcp_client_enter (void* args) {
  int client_sd = *( (int*) args );
  printf ("CHILD %lu: Connected to client (sd -> %d).\n", (unsigned long) pthread_self(), client_sd);

  char buffer[BUFFER_SIZE];
  int n;

  while (1) {
    n = recv (client_sd, buffer, BUFFER_SIZE, 0);

    if ( n == -1 ) {
      perror ("recv() failed");
      pthread_exit(0);
    }

    else if ( n == 0 ) {
      // the client has disconnected.
      printf ("CHILD %lu: Child disconnected\n", (unsigned long) pthread_self());
      pthread_exit (0);
    }
    else {
      buffer[n] = '\0';
      printf ("CHILD %lu: recieve -> %s\n",
        (unsigned long) pthread_self(), buffer);
      // x
    }

  }
}


// given a message, return the index of the command in the command list
//    if a command is found, return the index
//    if the index is not found, return -1
//    message syntax "COMMAND_NAME extra info"
short command_index (char* message, char** command_list, int cmd_size) {

  int end_index = 0;

  while ( *(message+end_index) != ' ' && *(message+end_index) != '\0' )
    ++end_index;

  char cmd[end_index + 1];
  cmd[end_index] = '\0';
  strncpy (cmd, message, end_index);
  for ( int i = 0; i < cmd_size; ++i ) {
    if ( strcmp(cmd, *(command_list+i)) == 0 ) return i;
  }
  return -1;
}

char ** snag (char* message, int* size) {
  // given a message, snag the words one by one and return it
  // in a char* arr. The size of the char* arr is returned

  // ASSUME: message must end with a '\0'
  char** msg_wrds;
  int words_size = 10;
  msg_wrds = calloc (words_size, sizeof(char*));
  int _size = 0;

  int msg_start = 0;
  int msg_end = 0;
  while (1) {
    if ( *(message+msg_end) == ' ' || *(message+msg_end) == '\0') {
      // add word to msg_words and increase size.
      if ( _size >= words_size) {
        // realloc more space
        words_size += 10;
        msg_wrds = (char**) realloc (msg_wrds, words_size * sizeof(char*));
      }

      // add the word to the list.
      int word_length = msg_end-msg_start;
      if ( word_length > 0 ) {
        *(msg_wrds+_size) = (char*) calloc (word_length + 1, sizeof(char));

        strncpy (*(msg_wrds+_size), message+msg_start, word_length );
        msg_start = msg_end + 1;

        ++ _size;
      }

      if (*(message+msg_end) == '\0') break;
    }

    ++ msg_end;
  }

  if (size != 0) *size = _size;
  return msg_wrds;
}
