#include <sys/types.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <ctype.h>
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

char ** snag (char* message, int* size, int size_limit);
short command_index (char* message, char** command_list, int cmd_size);
void * tcp_client_enter (void* args);
int isAlnumString (char* str);
int contains (char** lst, int size, char* wrd);
void logout (int user_index);
int find(char** lst, int size, char* wrd);
void who (char** buffer, int* bytes);
void broadcast (char buffer[], char* username);

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

  printf ("Listening for TCP connections on port: %d\n", port);

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

  printf ("Listening for UDP datagrams on port: %d\n", ntohs( udp_server.sin_port ));

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
  char* alloc_buffer = calloc (BUFFER_SIZE, sizeof(char));
  int n;
  int logged_in = 0; // false
  char* login_username;
  int bytes_;

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

      if (n > 0) buffer[n-1] = '\0';
      else buffer[n] = '\0';
      printf ("CHILD %lu: recieve -> %s\n",
        (unsigned long) pthread_self(), buffer);
      // Check what message was sent.
      short cmd_index = command_index (buffer, tcp_cmd_lst, 5);
      if (cmd_index == -1) {
        char error_msg[] = "ERROR invalid command\n\0";
        send (client_sd, (void *) error_msg, 23, 0);
      }

      else {
        // Application protocol handeling
        #ifdef DEBUG
        printf ("User Command: %s\n", tcp_cmd_lst[cmd_index]);
        #endif

        // (1) LOGIN function
        if (strcmp(tcp_cmd_lst[cmd_index], "LOGIN") == 0) {

          // get the username
          if (strlen(buffer) > 7) {

            char* username = buffer+6;
            int username_len = strlen(username);

            #ifdef DEBUG
            printf ("username: %s (len%d)\n", username, username_len);
            #endif

            pthread_mutex_lock (&user_db_mutex);

            if ( username_len < 4 || username_len > 16 || !isAlnumString(username) ) {
              char error_msg[] = "ERROR Invalid userid\n\0";
              send (client_sd, (void *) error_msg, 22, 0);
            }

            else if ( contains(user_database, user_db_index, username) ) {
              char error_msg[] = "ERROR Already Connected\n\0";
              send (client_sd, (void *) error_msg, 25, 0);
            }

            else {
              // user can login

              logged_in = 1;

              if (user_db_index == user_db_size) {
                user_db_size += 10;
                user_database = (char**) realloc (user_database, sizeof(char*) * user_db_size);
                user_fds = (int*) realloc (user_fds, sizeof(int) * user_db_size);
              }

              //user_database[user_db_index] = username; // check if this is actually correct ...
              user_database[user_db_index] = calloc (strlen(username)+1, sizeof(char));
              strcpy (user_database[user_db_index], username);
              user_fds[user_db_index] = client_sd;

              login_username = user_database[user_db_index];

              ++ user_db_index;

              char success_msg[] = "OK!\n";
              send (client_sd, (void *) success_msg, 4, 0);
            }

            pthread_mutex_unlock (&user_db_mutex);
          }
        }
        // (2) WHO function
        else if (strcmp(tcp_cmd_lst[cmd_index], "WHO") == 0) {
          // return all of the people on the server...
          printf ("About to check who()...\n");
          who(&alloc_buffer, &bytes_);
          send (client_sd, (void *) alloc_buffer, bytes_, 0);
        }

        // (3) LOGOUT function
        else if (strcmp(tcp_cmd_lst[cmd_index], "LOGOUT") == 0) {
          if ( !logged_in ) {
            // give error of not logged in...
            char error_msg[] = "ERROR not logged in.\n";
            send (client_sd, (void *) error_msg, 21, 0);
          }
          else {
            pthread_mutex_lock(&user_db_mutex);
            int login_index = find(user_database, user_db_index, login_username);
            printf ("Currently logged in as (%s)...\n", user_database[login_index]);
            logout (login_index);
            pthread_mutex_unlock(&user_db_mutex);

            char success_msg[] = "OK!\n";
            send (client_sd, (void *) success_msg, 4, 0);
          }
        }
        // (4) SEND function
        else if (strcmp(tcp_cmd_lst[cmd_index], "SEND") == 0) {
          // char ** snag (char* message, int* size, int size_limit)
          int query_size;
          char** query = snag(buffer+5, &query_size, 2);

          #ifdef DEBUG
          printf ("Size of Query: %d\n", query_size);
          for ( int i = 0; i < query_size; ++i ) {
            printf ("\t[%s:%ld]\n", *(query+i), strlen(*(query+i)));
          }
          #endif

          int valid_request = 1;
          int msg_len = atoi (*(query+1));
          int recipient_index;
          // check message validity
          // int find(char** lst, int size, char* wrd)
          if ( msg_len < 1 || msg_len > 990 ) {
            char err_msg[] = "ERROR Invalid msglen\n";
            send (client_sd, (void *) err_msg, 21, 0);
            valid_request = 0;
          }

          if ( !logged_in ) {
            char err_msg[] = "ERROR not logged in\n";
            send (client_sd, (void *) err_msg, 20, 0);
            valid_request = 0;
          }

          pthread_mutex_lock (&user_db_mutex);
          recipient_index = find(user_database, user_db_index, *(query));
          if ( recipient_index == -1 ) {
            char err_msg[] = "ERROR Unknown userid\n";
            send (client_sd, (void *) err_msg, 21, 0);
            valid_request = 0;
          }

          if (valid_request) {
            // send the ok to the sender first...
            char success_msg[] = "OK!\n";
            send (client_sd, (void *) success_msg, 4, 0);
            // send the message to the recipient
            #ifdef DEBUG
            printf ("message unpacking sizes...:\n\t[FROM ] = 5\n");
            printf ("\t[%s ] = %ld\n",login_username, strlen(login_username) + 1);
            printf ("\t[%s ] = %ld\n",*(query+1), strlen(*(query+1)) + 1);
          #endif
            int amount = snprintf (alloc_buffer, BUFFER_SIZE, "FROM %s %d %s\n",
              login_username, msg_len, buffer+5+strlen(login_username)+strlen(*(query+1))+2);
            alloc_buffer[amount] = '\0';

            send (user_fds[recipient_index], (void *) alloc_buffer, strlen(alloc_buffer), 0);

          }
          pthread_mutex_unlock (&user_db_mutex);

          // free the query
          for ( int i = 0; i < query_size; ++i )
            free (*(query+i));
          free (query);
        }
        // (5) BROADCAST function
        else if (strcmp(tcp_cmd_lst[cmd_index], "BROADCAST") == 0 && logged_in) {
          // int query_size;
          // char ** query;
          //
          // query = snag(buffer+10, &query_size, 1);
          // #ifdef DEBUG
          // printf ("BROADCAST Query: (size -> %d)\n", query_size);
          // printf ("msglen: %s\n", *query);
          // #endif
          //
          // pthread_mutex_lock(&user_db_mutex);
          // int broadcast_len = atoi(*query);
          // printf ("TEST: %s\n", buffer+10+strlen(*query)+1);
          // int amount = snprintf (alloc_buffer, BUFFER_SIZE, "FROM %s %d %s\n",
          //   login_username, broadcast_len, buffer+10+strlen(*query)+1);
          // alloc_buffer[amount] = '\0';
          // for ( int i = 0; i < user_db_index; ++i ) {
          //   // send to all
          //   send (user_fds[i], (void *) alloc_buffer, strlen(alloc_buffer), 0);
          // }
          // pthread_mutex_unlock(&user_db_mutex);

          broadcast (buffer, login_username);
        }
      }

    }

  }
  // free
  free (alloc_buffer);
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
    // printf ("comparing %s to  %s\n", cmd, *(command_list+i));
    if ( strcmp(cmd, *(command_list+i)) == 0 ) return i;
  }
  return -1;
}

char ** snag (char* message, int* size, int size_limit) {
  // given a message, snag the words one by one and return it
  // in a char* arr. The size of the char* arr is returned
  // size_limit of 0 for no size limit

  // ASSUME: message must end with a '\0'
  char** msg_wrds;
  int words_size = 10;
  msg_wrds = calloc (words_size, sizeof(char*));
  int _size = 0;

  int msg_start = 0;
  int msg_end = 0;
  while (size_limit == 0 || _size < size_limit) {
    if ( *(message+msg_end) == ' ' || *(message+msg_end) == '\0' || *(message+msg_end) == '\n') {
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

  msg_wrds = (char**) realloc (msg_wrds, _size * sizeof(char*));

  #ifdef DEBUG
  for ( int i = 0; i < _size; ++i ) {
    printf ("Word %d: %s (strlen : %ld) => ", i+1, *(msg_wrds+i), strlen(*(msg_wrds+i)));
    int j = 0;
    while ( *(*(msg_wrds+i)+j) != '\0' ) {
      printf ("[%c:%d] ", *(*(msg_wrds+i)+j),(int) *(*(msg_wrds+i)+j));
      ++j;
    }
    printf ("\n");
  }
  #endif

  if (size != 0) *size = _size;
  return msg_wrds;
}

int contains (char** lst, int size, char* wrd) {
  // check if a word exiss in the list
  for ( int i = 0; i < size; ++i ) {
    if ( strcmp(*(lst+i), wrd) == 0 ) return 1;
  }
  return 0;
}

// isalnum (int c)
int isAlnumString (char* str) {
  int i = 0;
  while ( i < 2000 && !*(str + i) == '\0') {
    if ( !isalnum( *(str+i) ) ) return 0;
    ++i;
  }
  return 1;
}

void logout (int user_index) {

  if ( user_index < user_db_size ) {

    free (*(user_database+user_index));
    if ( user_index + 1 == user_db_index ) {

      #ifdef DEBUG
      printf ("Case 1\n");
      #endif

      // remove normally, since I was the last to be added to the list
      *(user_database+user_index) = 0;
      *(user_fds+user_index) = 0;
    }

    else { // move the last added user to the spot where this user is logging out...

      #ifdef DEBUG
      printf ("Case 2\n");
      #endif
      *(user_database+user_index) = *(user_database + user_db_index - 1);
      *(user_fds + user_index) = *(user_fds + user_db_index - 1);

      *(user_database + user_db_index - 1) = 0;
      *(user_fds + user_db_index - 1) = 0;
    }
    --user_db_index;
  }
}


int find(char** lst, int size, char* wrd) {
  // given a list and size of the list, determine where wrd exists in lst (which index).
  // if not found, return -1
  for ( int i = 0; i < size; ++i ) {
    if ( strcmp(*(lst+i), wrd) == 0 ) return i;
  }
  return -1;
}

void who (char** buffer, int* bytes) {
  strcpy (*buffer, "OK!\n");
  *bytes = 4;

  for ( int i = 0; i < user_db_index; ++i ) {
    strcpy (*buffer+*bytes, user_database[i]);
    *bytes += strlen(user_database[i]);
    (*buffer)[*bytes] = '\n';
    ++(*bytes);
  }
  buffer[*bytes] = '\0';
}

void broadcast (char buffer[], char* username) {
  int query_size;
  char ** query;
  char send_buffer[BUFFER_SIZE];

  query = snag(buffer+10, &query_size, 1);
  #ifdef DEBUG
  printf ("BROADCAST Query: (size -> %d)\n", query_size);
  printf ("msglen: %s\n", *query);
  #endif

  pthread_mutex_lock(&user_db_mutex);
  int broadcast_len = atoi(*query);
  int amount = snprintf (send_buffer, BUFFER_SIZE, "FROM %s %d %s\n",
    username, broadcast_len, buffer+10+strlen(*query)+1);
  send_buffer[amount] = '\0';
  for ( int i = 0; i < user_db_index; ++i ) {
    // send to all
    send (user_fds[i], (void *) send_buffer, strlen(send_buffer), 0);
  }
  pthread_mutex_unlock(&user_db_mutex);
}
