# Chat Server
This is a chat server written in C. A client can connect to the server using UDP or TCP protocol.

# Build and Run
Compile server.c with gcc.
To run, enter the program into the terminal with one argument specifying the port number of the in the format:
./<program.o> <port #>

# Documentation
After connecting to the server, the client can connect to other clients via the server.

## TCP Client
**LOGIN** <username>
  *Logs the user into the server with the specified username.*
  
**WHO**
  *Returns a list of all the users logged into the chat server.*
  
**SEND** <recipient_id> <msg_len>\n<message_contents>
  *Sends the message to the recipient if they exist on the server.*
  *Otherwise, the sender client is prompted that the user does not exist on the server.*

**BROADCASE** <msg_len>\n<message_contents>
  *Sends a message to all the users logged into the server.*
  
**LOGOUT**
  *Logs the user out of the server.*
  
## UDP Client
A UDP client only has access to **WHO** functionality and **BROADCASE** functionality as it is connectionless.
