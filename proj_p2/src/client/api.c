#include "api.h"
#include "parser.h"
#include "src/common/io.h"
#include "src/common/constants.h"
#include "src/common/protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <pthread.h>
#include <sys/stat.h>
pthread_t notificationsThread;


/// @brief Writes a given message to a file descriptor, 
///        filling the rest of the buffer with nulls.
/// @param fd File to be written to.
/// @param message Message to be written on fd.
/// @param size Size of the message.
/// @return on success, returns 1, on error, returns -1
int write_correct_size(int fd, const char* message, size_t size){
  char buffer[size];
  fill_with_nulls(buffer, message, size);
  return write_all(fd, buffer, size);
}

int read_server_response(int fdResponsePipe, const char expected_OP_Code, char *result){
  char opcode;
  int readingError = 0;

  if(read_all(fdResponsePipe, &opcode, 1, &readingError) == -1){
    fprintf(stderr, "Failed to read opcode from responses pipe.\n");
    return 1;
  }

  if(opcode != expected_OP_Code){
    fprintf(stderr, "OP Code %c does not correspond to the expected OPCode.\n", opcode);
    return 1;
  }

  if(read_all(fdResponsePipe, result, 1, &readingError) == -1){
    fprintf(stderr, "Failed to read disconnect result from responses pipe.\n");
    return 1;
  }

  char* opName;

  switch(expected_OP_Code){
    case OP_CODE_CONNECT: 
      opName = "connect";
      break;
    case OP_CODE_DISCONNECT:
     opName = "disconnect";
     break;
    case OP_CODE_SUBSCRIBE: 
      opName = "subscribe";
      break;
    case OP_CODE_UNSUBSCRIBE:
      opName = "unsubscribe";
      break;
    default:
      opName = "unknown";
      break;
  }
  fprintf(stdout, "Server returned %c for operation: %s\n", *result, opName);
  return 0;
}

void *process_notif_thread(void *arg) {
  const int *fdNotificationPipe = (const int *)arg;
  while(1){
    int readError;

    // FIXME ???? as leituras deviam ser feitas as duas de seguida nao?
    char key[KEY_MESSAGE_SIZE];
    char value[KEY_MESSAGE_SIZE];
    while(1){
      if(read_all(*fdNotificationPipe, key, KEY_MESSAGE_SIZE, &readError) == -1
         || readError == -1){
        return NULL;
      }
      printf("key %d\n", readError);
      if(read_all(*fdNotificationPipe, value, KEY_MESSAGE_SIZE, &readError) == -1
        || readError == -1){
        return NULL;
      }
      fprintf(stdout, "(%s,%s)\n", key, value);
    }
 }
  return NULL;
}

int kvs_connect(char const* req_pipe_path, char const* resp_pipe_path,
                char const* server_pipe_path, char const* notif_pipe_path,
                int* fdNotificationPipe, int* fdRequestPipe, int* fdResponsePipe,
                int* fdServerPipe) {

  // connect to Server
  // check if the Registry Pipe exists and if it is writable
  if(access(server_pipe_path, W_OK)) {
    perror("Client could no access registry pipe.\n");
    return 1;
  }

  // open server pipe
  *fdServerPipe =  open(server_pipe_path, O_WRONLY);
  if(*fdServerPipe < 0) {
    fprintf(stderr, "Client could not open registry pipe.\n");
    return 1;
  }

  // create Client Pipes
  // FIXME check if the permissons needs to be 0666
  if(mkfifo(req_pipe_path, 0666)){ 
    fprintf(stderr, "Client could not create request pipe.\n");
    return 1;
  }
  if(mkfifo(resp_pipe_path, 0666)){
    fprintf(stderr, "Client could not create response pipe.\n");
    return 1;
  }
  if(mkfifo(notif_pipe_path, 0666)){
    fprintf(stderr, "Client could not create notification pipe.\n");
    return 1;
  }  

  // send connect message to server
  // FIX ME MUTEX NESTA CENA
  const char opcode = OP_CODE_CONNECT;
  if(write_all(*fdServerPipe, &opcode, 1) == -1){
    fprintf(stderr, "Error writing connect OP Code on the server pipe\n");
  }

  if(write_correct_size(*fdServerPipe, req_pipe_path, MAX_PIPE_PATH_LENGTH) == -1){
    fprintf(stderr, "Error writing requests pipe path on server pipe\n");
  }

  if(write_correct_size(*fdServerPipe, resp_pipe_path, MAX_PIPE_PATH_LENGTH) == -1){
    fprintf(stderr, "Error writing responses pipe path on server pipe\n");
  }

  if(write_correct_size(*fdServerPipe, notif_pipe_path, MAX_PIPE_PATH_LENGTH) == -1){
    fprintf(stderr, "Error writing notifications pipe path on server pipe\n");
  }
  //BYEBYE MUTEX
  // FIX ME, DISCONNECT IF OPEN RETURNED 1 

  *fdNotificationPipe = open(notif_pipe_path, O_RDONLY);
  if(*fdNotificationPipe < 0) {
    fprintf(stderr, "Client could not open notification pipe.\n");
    return 1;
  }

   if(pthread_create(&notificationsThread, NULL, process_notif_thread, (void *)(fdNotificationPipe))) {
    fprintf(stderr, "Failed to create thread\n");
    return 1;
  }

  *fdRequestPipe = open(req_pipe_path, O_WRONLY);
  if(*fdRequestPipe < 0) {
    fprintf(stderr, "Client could not open request pipe.\n");
    return 1;
  }

  *fdResponsePipe = open(resp_pipe_path, O_RDONLY);
  if(*fdResponsePipe < 0) {
    fprintf(stderr, "Client could not open response pipe.\n");
    return 1;
  }


  // read response from server
  // FIX ME disconnect in case of an error
  const char expected_OP_Code = OP_CODE_CONNECT;
  char result;
  printf("connect response: \n");
  if(read_server_response(*fdResponsePipe, expected_OP_Code, &result) == 1){
    fprintf(stderr, "Failed to read connect message from response pipe.\n");
  }
  return 0;
}

int kvs_disconnect(int fdRequestPipe, const char* req_pipe_path,
                   int fdResponsePipe, const char* resp_pipe_path,
                   int fdNotification, const char* notif_pipe_path,
                   int fdServerPipe) {
                    
  // send disconnect message to server
  const char opcode = OP_CODE_DISCONNECT;                
  if(write_all(fdRequestPipe, &opcode, 1) == -1){
    fprintf(stderr, "Error writing disconnect OP Code on the server pipe\n");
  }
  // read for response from server
  char result;
    printf("disconnect response: \n");

  if(read_server_response(fdResponsePipe, OP_CODE_DISCONNECT, &result) == 1){
    fprintf(stderr, "Failed to read disconnect response from server.\n");
  }


  // close pipes and unlink pipe files
  if(close(fdRequestPipe) < 0){
    fprintf(stderr, "Client failed to close requests pipe.\n");
  }
  
  if(unlink(req_pipe_path)){
    fprintf(stderr, "Client failed to unlink requests pipe.\n");
  }

  if(close(fdNotification) < 0){
    fprintf(stderr, "Client failed to close notifications pipe.\n");
  }

  if(unlink(notif_pipe_path)){
    fprintf(stderr, "Client failed to unlink notifications pipe.\n");
  }

  if(close(fdServerPipe) < 0){
    fprintf(stderr, "Client failed to close server pipe.\n");
  }

  if(close(fdResponsePipe) < 0){
    fprintf(stderr, "Client failed to close responses pipe.\n");
  }

  if(unlink(resp_pipe_path)){
    fprintf(stderr, "Client failed to unlink responses pipe.\n");
  }

  if(pthread_join(notificationsThread, NULL)){
    fprintf(stderr, "Failed to join host thread\n");
  }

  return 0;
}

int kvs_subscribe(int fdRequestPipe, int fdResponsePipe, const char* key) {

  // write subscription message on requests pipe
  const char opcode = OP_CODE_SUBSCRIBE;
  if(write_all(fdRequestPipe, &opcode, 1) == -1){
    fprintf(stderr, "Error writing subscribe OP Code on requests pipe\n");
    return 0;
  }

  if(write_correct_size(fdRequestPipe, key, KEY_MESSAGE_SIZE) == -1){
    fprintf(stderr, "Error writing key on requests pipe\n");
    return 0;
  }

  // should read from response pipe
  char result;
  printf("subscribe response: \n");

  if(read_server_response(fdResponsePipe, OP_CODE_SUBSCRIBE, &result) == 1){
    fprintf(stderr, "Failed to read subscribe response from server.\n");
  }
  return 1;
}

int kvs_unsubscribe(int fdResquestPipe, int fdResponsePipe, const char* key) {
  // send unsubscribe message to request pipe and wait for response in response pipe
  const char opcode = OP_CODE_UNSUBSCRIBE;
  if(write_all(fdResquestPipe, &opcode, 1) == -1){
    fprintf(stderr, "Error writing unsubscribe OP Code on requests pipe\n");
    return 1;
  }
  if(write_correct_size(fdResquestPipe, key, KEY_MESSAGE_SIZE) == -1){
    fprintf(stderr, "Error writing key on requests pipe\n");
    return 1;
  }

  // should read from response pipe
  char result;
    printf("unsubscribe response: \n");

  if(read_server_response(fdResponsePipe, OP_CODE_UNSUBSCRIBE, &result) == 1){
    fprintf(stderr, "Failed to read unsubscribe response from server.\n");
  }
  return 0;
}


