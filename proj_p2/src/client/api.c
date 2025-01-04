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

/*void* process_notif_thread(void* arg) {
  const int fdNotificationPipe = (const int*) arg;
  while(1){
    int readingError;

    // FIXME ???? as leituras deviam ser feitas as duas de seguida nao?
    char buffer[SIZE_READ_NOTIF_PIPE];
    parselist(fdNotificationPipe, buffer, MAX_NUMBER_SUB, MAX_STRING_SIZE);

    if(readingError){
      printf("Error reading key from notification pipe\n");
    }


    // FIXME process notifications

  }
  // FIXME process notifications
  return NULL;
}*/

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
  int opcode = OP_CODE_CONNECT;
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
  /*pthread_t notificationsThread;
  if(!pthread_create(&notificationsThread, NULL, process_notif_thread, (void *)fdNotificationPipe)) {
    fprintf(stderr, "Failed to create thread\n");
    return 1;
  }*/
   
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

  //if (write_all(fdRegistryPipe, connectMessage, SIZE_CONNECT_MESSAGE) != 1) {
    //fprintf(stderr, "Failed to write to registry pipe\n");
    //return 1;
  //}

  return 0;
}

int kvs_disconnect(int fdRequestPipe, const char* req_pipe_path,
                   int fdResponsePipe, const char* resp_pipe_path,
                   int fdNotification, const char* notif_pipe_path,
                   int fdServerPipe) {
  // FIX ME SHOULD CLOSE NOTIF FIFO
                    
  // send disconnect message to server
  int opcode = OP_CODE_DISCONNECT;                
  if(write_all(fdServerPipe, &opcode, 1) == -1){
    fprintf(stderr, "Error writing disconnect OP Code on the server pipe\n");
  }
  // should read for response pipe

  // close pipes and unlink pipe files
  if(close(fdRequestPipe) < 0){
    fprintf(stderr, "Client failed to close requests pipe.\n");
  }
  
  if(unlink(req_pipe_path)){
    fprintf(stderr, "Client failed to unlink requests pipe.\n");
  }

  if(close(fdResponsePipe) < 0){
    fprintf(stderr, "Client failed to close responses pipe.\n");
  }

  if(unlink(resp_pipe_path)){
    fprintf(stderr, "Client failed to unlink responses pipe.\n");
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
  return 0;
}

int kvs_subscribe(int fdRequestPipe, int fdResponsePipe, const char* key) {

  // write subscription message on requests pipe
  int opcode = OP_CODE_SUBSCRIBE;
  if(write_all(fdRequestPipe, &opcode, 1) == -1){
    fprintf(stderr, "Error writing subscribe OP Code on requests pipe\n");
    return 0;
  }

  if(write_correct_size(fdRequestPipe, key, KEY_MESSAGE_SIZE) == -1){
    fprintf(stderr, "Error writing key on requests pipe\n");
    return 0;
  }

  // should read from response pipe

  return 1;
}

int kvs_unsubscribe(int fdResquestPipe, int fdResponsePipe, const char* key) {
  // send unsubscribe message to request pipe and wait for response in response pipe
  int opcode = OP_CODE_UNSUBSCRIBE;
  if(write_all(fdResquestPipe, &opcode, 1) == -1){
    fprintf(stderr, "Error writing unsubscribe OP Code on requests pipe\n");
    return 1;
  }
  if(write_correct_size(fdResquestPipe, key, KEY_MESSAGE_SIZE) == -1){
    fprintf(stderr, "Error writing key on requests pipe\n");
    return 1;
  }

  // should read from response pipe
  
  return 0;
}


