#include "api.h"
#include "src/common/constants.h"
#include "src/common/protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <pthread.h>

void* process_notif_thread(void* arg) {
  const int fdNotificationPipe = (int *) arg;
  while(true){
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
}

int kvs_connect(char const* req_pipe_path, char const* resp_pipe_path,
                char const* server_pipe_path, char const* notif_pipe_path,
                int* fdNotificationPipe) {

  // connect to Server
  // check if the Registry Pipe exists and if it is writable
  if(!access(server_pipe_path, W_OK)) {
    fprintf(stderr, "Client could no access registry pipe.\n");
    return 1;
  }

  int fdRegistryPipe =  open(server_pipe_path, O_WRONLY);
  if(fdRegistryPipe < 0) {
    fprintf(stderr, "Client could not open registry pipe.\n");
    return 1;
  }

  // create Client Pipes
  // FIXME check if the permissons needs to be 0666
  if(!mkfifo(req_pipe_path, 0666)){ 
    fprintf(stderr, "Client could not create request pipe.\n");
    return 1;
  }
  if(!mkfifo(resp_pipe_path, 0666)){
    fprintf(stderr, "Client could not create response pipe.\n");
    return 1;
  }
  if(!mkfifo(notif_pipe_path, 0666)){
    fprintf(stderr, "Client could not create notification pipe.\n");
    return 1;
  }  

  fdNotificationPipe = open(notif_pipe_path, O_RDONLY);
  if(fdNotificationPipe < 0) {
    fprintf(stderr, "Client could not open notification pipe.\n");
    return 1;
  }

  pthread_t notificationsThread;
  if(!pthread_create(&notificationsThread, NULL, process_notif_thread, (void *)fdNotificationPipe)) {
    fprintf(stderr, "Failed to create thread\n");
    return 1;
  }

  // FIXME CREATE THREAD FOR NOTIFICATIONS

  // FIXME send connect message to server!!! 

  // TODO NO "io.c" concatenate_write_registry()-->
    // (OP_CODE=1 |
    // (char[40]) nome do pipe do cliente (para pedidos) |
    // (char[40]) nome do pipe do cliente (para respostas) | 
    // (char[40]) nome do pipe do cliente (para notificações)
  char connectMessage[SIZE_REGISTRY_PIPE];
  // concatenate_write_registry(connectMessage, req_pipe_path, resp_pipe_path, notif_pipe_path);
  write_all(fdRegistryPipe, connectMessage, SIZE_REGISTRY_PIPE);

  // TODO read from notif_pipe_path to check if the connection was accepted

  return 0;
}
 
int kvs_disconnect(void) {
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


