#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <dirent.h>
#include <fcntl.h> // isto significa file control option, deve ser do fd
#include <pthread.h> // IMPORTEI ESTES DOIS AQUI COM <> EM VEZ DE ""
#include <sys/wait.h>
#include <errno.h>

#include "constants.h"
#include "parser.h"
#include "operations.h"

#define PATH_MAX 4096
pthread_mutex_t backupCounterMutex;
pthread_mutex_t threadMutex;
pthread_rwlock_t globalHashLock;

struct ThreadArgs {
  DIR *dir;
  char *directory_path;
  unsigned int *backupCounter;
};

void *process_thread(void *arg){
  struct ThreadArgs *arg_struct = (struct ThreadArgs *)arg;
  DIR *dir = arg_struct->dir;
  char *directory_path = arg_struct->directory_path;
  unsigned int *backupCounter = arg_struct->backupCounter;


  if (pthread_mutex_lock(&threadMutex)) {
    fprintf(stderr, "Failed to lock mutex: %s\n", strerror(errno));
  }
  
  struct dirent *entry;

  while ((entry = readdir(dir)) != NULL) {

    // check if the file has the .job extension
    size_t len = strlen(entry->d_name);
    if (len < 4 || strcmp(entry->d_name + (len - 4), ".job"))continue;

    // get the full path of the file
    char filePath[PATH_MAX];
    snprintf(filePath, sizeof(filePath), "%s/%s", directory_path, entry->d_name);
    if (pthread_mutex_unlock(&threadMutex)) {
      fprintf(stderr, "Failed to unlock mutex: %s\n", strerror(errno));
    }

    // open the file
    int fd = open(filePath, O_RDONLY);
    if (fd < 0){
      fprintf(stderr, "Failed to open file\n");
      return NULL;
    }

    // copy the path without the .job extension
    char basePath[PATH_MAX];
    strncpy(basePath, filePath, strlen(filePath) - 4);  
    
    // creates the output file path with .out extension
    char outPath[PATH_MAX];
    snprintf(outPath, sizeof(basePath) + 4, "%s.out", basePath); 

    // opens or creates the output file
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
    unsigned int totalBck = 1;

    int eocFlag = 0;
    while(!eocFlag){
      switch (get_next(fd)) {
        case CMD_WRITE:
          num_pairs = parse_write(fd, keys, values, MAX_WRITE_SIZE, MAX_STRING_SIZE);
          if (num_pairs == 0) {
            fprintf(stderr, "Invalid command. See HELP for usage\n");
            continue;
          }
          if (pthread_rwlock_rdlock(&globalHashLock)||
              kvs_write(num_pairs, keys, values) ||
              pthread_rwlock_unlock(&globalHashLock)) {
            fprintf(stderr, "Failed to write pair\n");
          }
          break;

        case CMD_READ:
          num_pairs = parse_read_delete(fd, keys, MAX_WRITE_SIZE, MAX_STRING_SIZE);

          if (num_pairs == 0) {
            fprintf(stderr, "Invalid command. See HELP for usage\n");
            continue;
          }

          if (kvs_read(num_pairs, keys, fdOut)) {
            fprintf(stderr, "Failed to read pair\n");
          }
          break;

        case CMD_DELETE:
          num_pairs = parse_read_delete(fd, keys, MAX_WRITE_SIZE, MAX_STRING_SIZE);

          if (num_pairs == 0) {
            fprintf(stderr, "Invalid command. See HELP for usage\n");
            continue;
          }
          if (pthread_rwlock_rdlock(&globalHashLock)||
              kvs_delete(num_pairs, keys, fdOut)||
              pthread_rwlock_unlock(&globalHashLock)) {
            fprintf(stderr, "Failed to delete pair\n");
          }
          break;

        case CMD_SHOW:
          if (kvs_show(fdOut)) {
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
          // backup limit hasnt been reached yet
          if((*backupCounter) > 0){
            (*backupCounter)--;
          }

          // backup limit has been reached
          else{
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
          // there cant be writes or deletes on the hashtable while the fork is being made
          if(pthread_rwlock_wrlock(&globalHashLock)){
            fprintf(stderr, "Failed to lock global hash lock\n");
          }

          pid_t pid = fork();

          if(pthread_rwlock_unlock(&globalHashLock)){
            fprintf(stderr, "Failed to unlock global hash lock\n");
          }

          // END OF CRITICAL SECTION HASHTABLE

          if(pid < 0){
            fprintf(stderr, "Failed to create backup\n");
          }

          // child process
          else if(pid == 0){
            // create file path for backup
            char bckPath[PATH_MAX];
            snprintf(bckPath, sizeof(bckPath), "%.*s-%d.bck", (int)(strlen(filePath) - 4), filePath, totalBck);
            // create and open the backup file
            int fdBck = open(bckPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if(fdBck == -1){
              fprintf(stderr, "Failed to open backup file\n");

                pthread_mutex_destroy(&backupCounterMutex); // TODO estes locks nao podem estar no if porque nao temos
                pthread_mutex_destroy(&threadMutex);       // maneira de saber se estao locked ou nao antes de tentarmos destruilos
                pthread_rwlock_destroy(&globalHashLock);
                if (kvs_terminate()||
                  close(fd) < 0 ||
                  close(fdOut) < 0 ||
                  closedir(dir) < 0) {
                fprintf(stderr, "Failed to close resources: %s\n", strerror(errno));
              }
              exit(1);
            }

            // perform kvs backup
            if (kvs_backup(fdBck)) {  
              fprintf(stderr, "Failed to perform backup.\n");
            }
            
            // terminate child
            
            pthread_mutex_destroy(&backupCounterMutex); // TODO estes locks nao podem estar no if porque nao temos
            pthread_mutex_destroy(&threadMutex);       // maneira de saber se estao locked ou nao antes de tentarmos destruilos
            pthread_rwlock_destroy(&globalHashLock);
            if (kvs_terminate()||
                close(fd) < 0 ||
                close(fdOut) < 0 ||
                closedir(dir) < 0) {
              fprintf(stderr, "Failed to close resources: %s\n", strerror(errno));
            }
            exit(0);
          }

          // father process
          else{
            totalBck++;
          }
          break;

        case CMD_INVALID:
          fprintf(stderr, "Invalid command. See HELP for usage\n");
          break;

        case CMD_HELP:
          printf( 
              "Available commands:\n"
              "  WRITE [(key,value)(key2,value2),...]\n"
              "  READ [key,key2,...]\n"
              "  DELETE [key,key2,...]\n"
              "  SHOW\n"
              "  WAIT <delay_ms>\n"
              "  BACKUP\n"
              "  HELP\n"
          );
          break;
          
        case CMD_EMPTY:
          break;

        case EOC:
          if (close(fd) < 0||
              close(fdOut) < 0) {
            fprintf(stderr, "Failed to close file: %s\n", strerror(errno));
          }
          if (pthread_mutex_lock(&threadMutex)) {
            fprintf(stderr, "Failed to lock mutex\n");
          }
          eocFlag = 1;
          break;
      }
    }
  }
  if (pthread_mutex_unlock(&threadMutex)) {
    fprintf(stderr, "Failed to unlock mutex\n");
  } 
  return NULL;
}


int main(int argc, char *argv[]) {

  // check if the correct number of arguments was passed
  if(argc != 4) {
    fprintf(stderr, "Wrong number of arguments\n");
    return 1;
  }

  // parse input arguments  
  char *directory_path = argv[1];
  unsigned int backupCounter = (unsigned int)strtoul(argv[2], NULL, 10);
  unsigned int MAX_THREADS = (unsigned int)strtoul(argv[3], NULL, 10);


  DIR *dir = opendir(directory_path);
  // check if the directory exists
  if(dir == NULL){
    fprintf(stderr, "Failed to open directory\n");
    return 1;
  }

  // initialize the key-value store
  if (kvs_init()) {
    if (closedir(dir)) {
      fprintf(stderr, "Failed to close directory\n");
    }
    fprintf(stderr, "Failed to initialize KVS\n");
    return 1;
  }

  if (pthread_mutex_init(&backupCounterMutex, NULL)|| 
      pthread_mutex_init(&threadMutex, NULL)|| 
      pthread_rwlock_init(&globalHashLock, NULL)) {
      fprintf(stderr, "Failed to initialize lock: %s\n", strerror(errno));
  }

  pthread_t thread[MAX_THREADS];
  struct ThreadArgs args = {dir, directory_path, &backupCounter};
  for(unsigned int i = 0; i < MAX_THREADS; i++){
    if (pthread_create(&thread[i], NULL, process_thread, (void *)&args)) {
      fprintf(stderr, "Failed to create thread\n");
    }
  }

  // terminates after processing all files
  for(unsigned int i = 0; i < MAX_THREADS; i++){
    if (pthread_join(thread[i], NULL)) {
      fprintf(stderr, "Failed to join thread\n");
    }
  }

  // wait for all child processes to terminate
  if(pthread_mutex_lock(&backupCounterMutex)){
    fprintf(stderr, "Failed to lock mutex\n");
  }

  while(wait(NULL) > 0);

  if (pthread_mutex_unlock(&backupCounterMutex)||
      closedir(dir)||
      pthread_mutex_destroy(&backupCounterMutex)||
      pthread_mutex_destroy(&threadMutex)||
      pthread_rwlock_destroy(&globalHashLock)||
      kvs_terminate()) {
    fprintf(stderr, "Failed to close resources: %s\n", strerror(errno));
  }

  return 0;
  }