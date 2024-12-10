#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <dirent.h>
#include <fcntl.h> // isto significa file control option, deve ser do fd
#include <pthread.h> // IMPORTEI ESTES DOIS AQUI COM <> EM VEZ DE ""
#include <sys/wait.h>


#include "constants.h"
#include "parser.h"
#include "operations.h"

#define PATH_MAX 4096

int main(int argc, char *argv[]) {

  // check if the correct number of arguments was passed
  if(argc != 4) {
    fprintf(stderr, "Wrong number of arguments\n");
    return 1;
  }
  const char *directory_path = argv[1];
  unsigned int backupCounter = (unsigned int)strtoul(argv[2], NULL, 10);
  //unsigned int MAX_THREADS = (unsigned int)strtoul(argv[3], NULL, 10);

  pthread_mutex_t backupCounterMutex = PTHREAD_MUTEX_INITIALIZER;

  DIR *dir = opendir(directory_path);
  struct dirent *entry;
  
  // check if the directory exists
  if(dir == NULL){
    fprintf(stderr, "Failed to open directory\n");
    return 1;
  }

  if (kvs_init()) {
    closedir(dir);
    fprintf(stderr, "Failed to initialize KVS\n");
    return 1;
  }

  void *process_file(void *arg){
    char keys[MAX_WRITE_SIZE][MAX_STRING_SIZE] = {0};
    char values[MAX_WRITE_SIZE][MAX_STRING_SIZE] = {0};
    unsigned int delay;
    size_t num_pairs;

    char *filePath = (char *)arg;

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

    // count the backups already made on this file
    unsigned int totalBck = 1;

    while(1){
      switch (get_next(fd)) {
        case CMD_WRITE:
          num_pairs = parse_write(fd, keys, values, MAX_WRITE_SIZE, MAX_STRING_SIZE);
          if (num_pairs == 0) {
            fprintf(stderr, "Invalid command. See HELP for usage\n");
            continue;
          }

          if (kvs_write(num_pairs, keys, values)) {
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
          if (kvs_delete(num_pairs, keys, fdOut)) {
            fprintf(stderr, "Failed to delete pair\n");
          }
          break;

        case CMD_SHOW:
          kvs_show(fdOut);
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

          pthread_mutex_lock(&backupCounterMutex);
          
          if(backupCounter <= 0){
            pid_t terminated_pid = wait(NULL); // wait for a child process to terminate
            if (terminated_pid == -1) {
              fprintf(stderr, "Failed to wait for child process\n");
            }
            else{
              backupCounter++;
            }
          }

          pid_t pid = fork();

          if(pid < 0){
            fprintf(stderr, "Failed to create backup\n");
          }

          // child process
          else if(pid == 0){
            pthread_mutex_unlock(&backupCounterMutex);
            // create file path for backup
            char bckPath[PATH_MAX];
            snprintf(bckPath, sizeof(bckPath), "%.*s-%d.bck", (int)(strlen(filePath) - 4), filePath, totalBck);

            // create and open the backup file
            int fdBck = open(bckPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if(fdBck == -1){
              fprintf(stderr, "Failed to open backup file\n");
              exit(1);
            }

            // perform kvs backup
            if (kvs_backup(fdBck)) {  
              fprintf(stderr, "Failed to perform backup.\n");
            }
  
            // terminate child
            exit(0);
          }

          // father process
          else{
            backupCounter--;
            totalBck++;
            pthread_mutex_unlock(&backupCounterMutex);       
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
              "  BACKUP\n" // Not implemented
              "  HELP\n"
          );
          break;
          
        case CMD_EMPTY:
          break;

        case EOC:
          close(fd);
          close(fdOut);
          return NULL;
        }
      }
    }
    

  while ((entry = readdir(dir)) != NULL) {

    // check if the file has the .job extension
    size_t len = strlen(entry->d_name);
    if (len < 4 || strcmp(entry->d_name + (len - 4), ".job")) continue;

    // get the full path of the file
    char file_path[PATH_MAX];
    snprintf(file_path, sizeof(file_path), "%s/%s", directory_path, entry->d_name);
    process_file((void *)file_path);
    
    /*
    pthread_t thread;
    if(pthread_create(&thread, NULL, process_file, (void *)file_path)){
      fprintf(stderr, "Failed to create thread\n");
    };
  */

    }
    // terminates after processing all files
    while(wait(NULL) > 0);
    kvs_terminate();
    closedir(dir);
    return 0;
  }