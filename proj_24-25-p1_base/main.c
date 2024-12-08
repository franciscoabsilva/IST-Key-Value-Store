#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <dirent.h>
#include <fcntl.h> //???? soa bue suspeito, é mm?

#include "constants.h"
#include "parser.h"
#include "operations.h"
#include "pthread.h"
#include <semaphore.h>
#define PATH_MAX 4096

sem_t sem;

int main(int argc, char *argv[]) {

  // check if the correct number of arguments was passed
  if(argc != 3) {
    fprintf(stderr, "Wrong number of arguments\n");
    return 1;
  }
  const char *directory_path = argv[1];

  unsigned int maxBackup = (unsigned int)strtoul(argv[2], NULL, 10);

  // initialize semaphore
  if (sem_init(&sem, 0, maxBackup) != 0) {
    perror("Erro ao inicializar o semáforo");
    exit(1);
  }
 
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

  while ((entry = readdir(dir)) != NULL) {

    char keys[MAX_WRITE_SIZE][MAX_STRING_SIZE] = {0};
    char values[MAX_WRITE_SIZE][MAX_STRING_SIZE] = {0};
    unsigned int delay;
    size_t num_pairs;

    printf("> ");
    fflush(stdout);

    size_t len = strlen(entry->d_name);
    if (len < 4 || strcmp(entry->d_name + (len - 4), ".job")) continue;

    // get the full path of the file
    char file_path[PATH_MAX];
    snprintf(file_path, sizeof(file_path), "%s/%s", directory_path, entry->d_name);

    int fd = open(file_path, O_RDONLY);
    
    if (fd < 0){
      fprintf(stderr, "Failed to open file\n");
      continue;
    }
    
    // get the name of the output file (len + null character)
    char outName[len + 1];
    // copy the name and write the .out extension
    strncpy(outName, entry->d_name, len - 4);
    strcpy(outName + (len - 4), ".out");

    char out_path[PATH_MAX];
    snprintf(out_path, sizeof(out_path), "%s/%s", directory_path, outName);
    
    // opens or creates the output file
    int fdOut = open(out_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fdOut == -1) {
      fprintf(stderr, "Failed to open output file: %s\n", out_path);
    continue;
    }
    int totalBck = 0;
  
    // flag to exit the file processing loop
    int exitFile = 0; 
    while(!exitFile){
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
          pid_t pid = fork();

          if(pid < 0){
            fprintf(stderr, "Failed to create backup\n");
          }

          // child process
          else if(pid == 0){

            sem_wait(&sem);

            char bckName[len + 1000]; // METI MAIS 1000 PORQUE AQUI CABE TUDO AQUILO QUE NOS APETECER, MAS VER SE ARRANJAMOS ALGO MELHOR
            snprintf(bckName, sizeof(bckName), "%.*s-%d.bck", (int)(len - 4), entry->d_name, totalBck + 1);

            char bck_path[PATH_MAX];
            snprintf(bck_path, sizeof(bck_path), "%s/%s", directory_path, bckName);

            int fdBck = open(bck_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (kvs_backup(fdBck)) {  
              fprintf(stderr, "Failed to perform backup.\n");
            }
            sem_post(&sem);
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
              "  BACKUP\n" // Not implemented
              "  HELP\n"
          );

          break;
          
        case CMD_EMPTY:
          break;

        case EOC:
          close(fd);
          close(fdOut);
          exitFile = 1;
          break;
      }
    }
  }
  // terminates after processing all files
  kvs_terminate();
  closedir(dir);
  return 0;
}