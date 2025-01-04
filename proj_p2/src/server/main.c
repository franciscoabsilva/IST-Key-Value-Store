#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/wait.h>
#include <sys/stat.h>

#include "constants.h"
#include "operations.h"
#include "parser.h"
#include "src/common/io.h"
#include "src/common/constants.h"
#include "src/common/protocol.h"


pthread_mutex_t backupCounterMutex;
pthread_mutex_t dirMutex;
pthread_rwlock_t globalHashLock;

struct ThreadArgs {
  DIR *dir;
  char *directory_path;
  unsigned int *backupCounter;
};

void *process_thread(void *arg) {
  struct ThreadArgs *arg_struct = (struct ThreadArgs *)arg;
  DIR *dir = arg_struct->dir;
  char *directory_path = arg_struct->directory_path;
  unsigned int *backupCounter = arg_struct->backupCounter;

  struct dirent *entry;

  // CRITICAL SECTION DIR
  if (pthread_mutex_lock(&dirMutex)) {
    fprintf(stderr, "Failed to lock mutex\n");
  }
  while ((entry = readdir(dir)) != NULL) {

    // check if the file has the .job extension
    size_t len = strlen(entry->d_name);
    if (len < 4 || strcmp(entry->d_name + (len - 4), ".job"))
      continue;

    char filePath[MAX_JOB_FILE_NAME_SIZE];
    
    int lenFilePath = snprintf(filePath, sizeof(filePath), "%s/%s", directory_path,
                       entry->d_name);
    
    if(lenFilePath < 0 || lenFilePath >=  MAX_JOB_FILE_NAME_SIZE) {
      fprintf(stderr, "Failed to create file path\n");
      continue;
    }

    if (pthread_mutex_unlock(&dirMutex)) {
      fprintf(stderr, "Failed to unlock mutex\n");
    }
    // END OF CRITICAL SECTION DIR

    int fd = open(filePath, O_RDONLY);
    if (fd < 0) {
      fprintf(stderr, "Failed to open file\n");
      return NULL;
    }

    // copy the path without the .job extension
    char basePath[MAX_JOB_FILE_NAME_SIZE] = "";
    strncpy(basePath, filePath, strlen(filePath) - 4);

    // creates the output file path with .out extension
    char outPath[MAX_JOB_FILE_NAME_SIZE] = "";
    snprintf(outPath, sizeof(basePath) + 4, "%s.out", basePath);

    int fdOut = open(outPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fdOut == -1) {
      fprintf(stderr, "Failed to open output file");
      return NULL;
    }

    char keys[MAX_WRITE_SIZE][MAX_STRING_SIZE] = {0};
    char values[MAX_WRITE_SIZE][MAX_STRING_SIZE] = {0};
    unsigned int delay;
    size_t num_pairs;

    // count the backups already made on this file
    unsigned int fileBackups = 1;

    int eocFlag = 0;
    while (!eocFlag) {
      switch (get_next(fd)) {
      case CMD_WRITE:
        num_pairs =
            parse_write(fd, keys, values, MAX_WRITE_SIZE, MAX_STRING_SIZE);
        if (num_pairs == 0) {
          fprintf(stderr, "Invalid command. See HELP for usage\n");
          continue;
        }
        if (pthread_rwlock_rdlock(&globalHashLock) ||
            kvs_write(num_pairs, keys, values) ||
            pthread_rwlock_unlock(&globalHashLock)) {
          fprintf(stderr, "Failed to write pair\n");
        }
        break;

      case CMD_READ:
        num_pairs =
            parse_read_delete(fd, keys, MAX_WRITE_SIZE, MAX_STRING_SIZE);

        if (num_pairs == 0) {
          fprintf(stderr, "Invalid command. See HELP for usage\n");
          continue;
        }

        if (pthread_rwlock_rdlock(&globalHashLock) ||
            kvs_read(num_pairs, keys, fdOut) ||
            pthread_rwlock_unlock(&globalHashLock)) {
          fprintf(stderr, "Failed to read pair\n");
        }
        break;

      case CMD_DELETE:
        num_pairs =
            parse_read_delete(fd, keys, MAX_WRITE_SIZE, MAX_STRING_SIZE);

        if (num_pairs == 0) {
          fprintf(stderr, "Invalid command. See HELP for usage\n");
          continue;
        }
        if (pthread_rwlock_rdlock(&globalHashLock) ||
            kvs_delete(num_pairs, keys, fdOut) ||
            pthread_rwlock_unlock(&globalHashLock)) {
          fprintf(stderr, "Failed to delete pair\n");
        }
        break;

      case CMD_SHOW:
        if (pthread_rwlock_rdlock(&globalHashLock) ||
            kvs_show(fdOut) ||
            pthread_rwlock_unlock(&globalHashLock)) {
          fprintf(stderr, "Failed to show pairs\n");
        }
        break;

      case CMD_WAIT:
        if (parse_wait(fd, &delay, NULL) == -1) {
          fprintf(stderr, "Invalid command. See HELP for usage\n");
          continue;
        }

        if (delay > 0) {
          write(fdOut, "Waiting...\n", 11);
          kvs_wait(delay);
        }
        break;

      case CMD_BACKUP:

        // CRITIAL SECTION BACKUPCOUNTER
        if (pthread_mutex_lock(&backupCounterMutex)) {
          fprintf(stderr, "Failed to lock mutex\n");
        }

        // backup limit hasn't been reached yet
        if ((*backupCounter) > 0) {
          (*backupCounter)--;
        }
        // backup limit has been reached
        else {
          // wait for a child process to terminate
          pid_t terminated_pid;
          do {
            terminated_pid = wait(NULL);
          } while (terminated_pid == -1); 
        }

        if (pthread_mutex_unlock(&backupCounterMutex)) {
          fprintf(stderr, "Failed to unlock mutex\n");
        }
        // END OF BACKUPCOUNTER CRITICAL SECTION

        // CRITICAL SECTION HASHTABLE
        // (there cant be any type of access that might change the hashtable
        //  or allocate memory while we are creating a backup)
        if (pthread_rwlock_wrlock(&globalHashLock)) {
          fprintf(stderr, "Failed to lock global hash lock\n");
        }

        // create file path for backup
        char bckPath[MAX_JOB_FILE_NAME_SIZE];
        snprintf(bckPath, sizeof(bckPath), "%.*s-%d.bck", 
                 (int)(strlen(filePath) - 4), filePath, fileBackups);

        pid_t pid = fork();

        if (pid < 0) {
          fprintf(stderr, "Failed to create backup\n");
        }

        if (pthread_rwlock_unlock(&globalHashLock)) {
          fprintf(stderr, "Failed to unlock global hash lock\n");
        }
        // END OF CRITICAL SECTION HASHTABLE


        // child process
        else if (pid == 0) {
          // functions used here have to be async signal safe, since this
          // fork happens in a multi thread context (see man fork)
          int fdBck = open(bckPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
          kvs_backup(fdBck);


          // terminate child
          pthread_rwlock_destroy(&globalHashLock);
          kvs_terminate();
          close(fd);
          close(fdOut);
          closedir(dir);
          _exit(0);
        }

        // father process
        else {
          fileBackups++;
        }
        break;

      case CMD_INVALID:
        fprintf(stderr, "Invalid command. See HELP for usage\n");
        break;

      case CMD_HELP:
        printf("Available commands:\n"
               "  WRITE [(key,value)(key2,value2),...]\n"
               "  READ [key,key2,...]\n"
               "  DELETE [key,key2,...]\n"
               "  SHOW\n"
               "  WAIT <delay_ms>\n"
               "  BACKUP\n"
               "  HELP\n");
        break;

      case CMD_EMPTY:
        break;

      case EOC:
        if (close(fd) < 0 || close(fdOut) < 0) {
          fprintf(stderr, "Failed to close file\n");
        }
        if (pthread_mutex_lock(&dirMutex)) {
          fprintf(stderr, "Failed to lock mutex\n");
        }
        eocFlag = 1;
        break;
      }
    }
  }
  if (pthread_mutex_unlock(&dirMutex)) {
    fprintf(stderr, "Failed to unlock mutex\n");
  }
  return NULL;
}

int read_connect_message(int fdServerPipe, int* opcode, char* req_pipe, char* resp_pipe, char* notif_pipe){
  int reading_error = 0;
   
  //FIX ME check why do we care about EOF (return of the read_all)
  read_all(fdServerPipe, opcode, 1, &reading_error);
  if(reading_error){
    fprintf(stderr, "Failed to read OP Code from server pipe\n");
  }

  if(*opcode != 1){
    fprintf(stderr, "OP Code %d does not correspond to the connect opcode\n", *opcode);
    return 1;
  }

  read_all(fdServerPipe, req_pipe, MAX_PIPE_PATH_LENGTH, &reading_error);
  if(reading_error == -1){
    fprintf(stderr, "Failed to read requests pipe path from server pipe\n");
  }
 
  read_all(fdServerPipe, resp_pipe, MAX_PIPE_PATH_LENGTH, &reading_error);
  if(reading_error){
    fprintf(stderr, "Failed to read responses pipe path from server pipe\n");
  }

  read_all(fdServerPipe, notif_pipe, MAX_PIPE_PATH_LENGTH, &reading_error);
  if(reading_error){
    fprintf(stderr, "Failed to read notifications pipe path from server pipe\n");
  }
  return 0;
}

int connect_to_client(int *fdServerPipe, int *fdReqPipe, int *fdRespPipe, int *fdNotifPipe){
  int opCode = 0; //FIX ME (precisa de +1 para o \0?)
  char req_pipe[MAX_PIPE_PATH_LENGTH];
  char resp_pipe[MAX_PIPE_PATH_LENGTH];
  char notif_pipe[MAX_PIPE_PATH_LENGTH];

  if(read_connect_message(*fdServerPipe, &opCode, req_pipe, resp_pipe, notif_pipe)){
    fprintf(stderr, "Failed to read connect message\n");
    return 1;
  }

  *fdNotifPipe = open(notif_pipe, O_WRONLY);
  if(*fdNotifPipe < 0){
    fprintf(stderr, "Failed to open notifications pipe\n");
    return 1;
  }

  *fdReqPipe = open(req_pipe, O_RDONLY);
  if(*fdReqPipe < 0){
    fprintf(stderr, "Failed to open requests pipe\n");
    if(close(*fdNotifPipe)){
      fprintf(stderr, "Failed to close notifications pipe\n");
    }
    return 1;
  }

  *fdRespPipe = open(resp_pipe, O_WRONLY);
  if(*fdRespPipe < 0){
    fprintf(stderr, "Failed to open responses pipe\n");
    if(close(*fdNotifPipe) || close(*fdReqPipe)){
      fprintf(stderr, "Failed to notifications and requests pipes\n");
    }
    return 1;
  }
  return 0;
}

int wait_for_request(int fdReqPipe, int fdNotifPipe, int fdRespPipe, int* status){
  int reading_error = 0;
  int* opcode = 0;

  while(reading_error == 0){
    read_all(fdReqPipe, opcode, 1, &reading_error);
    if(reading_error == -1){
      fprintf(stderr, "Failed to read OP Code from request pipe\n");
      return 1;
    }
  }

  switch (*opcode) {
    case OP_CODE_DISCONNECT:
      // clean keys
      // delete pipes
      // new use for thread
      /* code */
      break;

    case OP_CODE_SUBSCRIBE:
      char key[KEY_MESSAGE_SIZE];
      read_all(fdReqPipe, key, KEY_MESSAGE_SIZE, &reading_error);
      if(kvs_subscribe(key, fdNotifPipe, fdRespPipe)){
        fprintf(stderr, "Failed to subscribe client\n");
        return 1;
      }
      // add to list of subscribers
      break;
    case OP_CODE_UNSUBSCRIBE:
      // remove from list of subscribers
      *status = CLIENT_TERMINATED;
      break;

    default:
      break;
    }
  
  return 0;
}

void *process_host_thread(void *arg) {
  const char *fifo_path = (const char *)arg;
  if (mkfifo(fifo_path, 0666)) { // FIXME???? devia ser 0640????
    fprintf(stderr, "Failed to create server pipe\n");
  }

  int fdServerPipe = open(fifo_path, O_RDONLY);
  if (fdServerPipe < 0) {
    fprintf(stderr, "Failed to open server pipe\n");
  }

  int fdReqPipe, fdRespPipe, fdNotifPipe; 
  if (connect_to_client(&fdServerPipe, &fdReqPipe, &fdRespPipe, &fdNotifPipe)) {
    fprintf(stderr, "Failed to connect to client\n");
    return NULL;
  }

  int* status = 0;
  while(*status != CLIENT_TERMINATED){
    wait_for_request(fdReqPipe, fdNotifPipe, fdRespPipe, status);
  }
  
  return NULL;
}

int main(int argc, char *argv[]) {

  if (!(argc == 5)) {
    fprintf(stderr, "Usage: %s <dir_jobs> <max_threads> <backups_max> [name_registry_FIFO]\n", argv[0]);
    return 1;
  }

  char* directory_path = argv[1];
  unsigned int backupCounter = (unsigned int)strtoul(argv[2], NULL, 10);
  unsigned int MAX_THREADS = (unsigned int)strtoul(argv[3], NULL, 10);
  const char* fifo_path = argv[4];

  // TODO: create master thread to deal with the registry FIFO 
  // (open fifo in master thread? hm maybe idk)
  // is master thread this running main? (probably, ups)

  DIR *dir = opendir(directory_path);

  if (dir == NULL) {
    fprintf(stderr, "Failed to open directory\n");
    return 1;
  }

  if (kvs_init()) {
    if (closedir(dir)) {
      fprintf(stderr, "Failed to close directory\n");
    }
    fprintf(stderr, "Failed to initialize KVS\n");
    return 1;
  }

  if (pthread_mutex_init(&backupCounterMutex, NULL) ||
      pthread_mutex_init(&dirMutex, NULL) ||
      pthread_rwlock_init(&globalHashLock, NULL)) {
    fprintf(stderr, "Failed to initialize lock\n");
  }


  pthread_t thread[MAX_THREADS];
  struct ThreadArgs args = {dir, directory_path, &backupCounter};
  for (unsigned int i = 0; i < MAX_THREADS; i++) {
    if (pthread_create(&thread[i], NULL, process_thread, (void *)&args)) {
      fprintf(stderr, "Failed to create thread\n");
    }
  }
  pthread_t host_thread;
  if (pthread_create(&host_thread, NULL, process_host_thread, (void *)fifo_path)) {
    fprintf(stderr, "Failed to create thread\n");
  }
  
  for (unsigned int i = 0; i < MAX_THREADS; i++) {
    if (pthread_join(thread[i], NULL)) {
      fprintf(stderr, "Failed to join job thread\n");
    }
  }
  if(pthread_join(host_thread, NULL)){
    fprintf(stderr, "Failed to join host thread\n");
  }
  // wait for all child processes to terminate
  if (pthread_mutex_lock(&backupCounterMutex)) {
    fprintf(stderr, "Failed to lock mutex\n");
  }
  
  while (wait(NULL) > 0);

  if (pthread_mutex_unlock(&backupCounterMutex) || closedir(dir) ||
      pthread_mutex_destroy(&backupCounterMutex) ||
      pthread_mutex_destroy(&dirMutex) ||
      pthread_rwlock_destroy(&globalHashLock) || kvs_terminate()) {
    fprintf(stderr, "Failed to close resources\n");
  }

  return 0;
}