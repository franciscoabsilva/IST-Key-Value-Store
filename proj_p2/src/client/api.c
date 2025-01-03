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


int write_correct_size(int fd, const char* message, int size){
  char buffer[size];
  fill_with_nulls(buffer, message, MAX_PIPE_PATH_LENGTH);
  return write_all(fd, buffer, MAX_PIPE_PATH_LENGTH);
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

  /*pthread_t notificationsThread;
  if(!pthread_create(&notificationsThread, NULL, process_notif_thread, (void *)fdNotificationPipe)) {
    fprintf(stderr, "Failed to create thread\n");
    return 1;
  }*/
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

  *fdNotificationPipe = open(notif_pipe_path, O_RDONLY);
  if(*fdNotificationPipe < 0) {
    fprintf(stderr, "Client could not open notification pipe.\n");
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

  //build_connect_message(connectMessage, req_pipe_path, resp_pipe_path, notif_pipe_path);

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
  // close pipes and unlink pipe files
  return 0;
}

int kvs_subscribe(const char* key) {
  // send subscribe message to request pipe and wait for response in response pipe
  return 0;
}

int kvs_unsubscribe(const char* key) {
    // send unsubscribe message to request pipe and wait for response in response pipe
  return 0;
}


