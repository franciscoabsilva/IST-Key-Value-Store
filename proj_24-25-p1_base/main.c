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
#define PATH_MAX 4096 //???? provalvemente nao é preciso

int main(int argc, char *argv[]) {

  // check if the correct number of arguments was passed
  if(argc != 3) return 1;
  
  const char *directory_path = argv[1];
  //int backup_counter = atoi(argv[2]);

  DIR *dir = opendir(directory_path);
  struct dirent *entry;
  
  // check if the directory exists
  // ???? TODO ver se o dir é null se nao existir ou se isto é inutil
  if(dir == NULL){
    closedir(dir);
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

    // check to see if the file name is a .job
    // ???? TODO verificar bem
    if (!strstr(entry->d_name, ".job")) continue; 

    // get the full path of the file
    char file_path[PATH_MAX];
    snprintf(file_path, sizeof(file_path), "%s/%s", directory_path, entry->d_name);

    int fd = open(file_path, O_RDONLY);
    if (fd == -1){
      fprintf(stderr, "Failed to open file\n");
      continue;
    }

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

          if (kvs_read(num_pairs, keys)) {
            fprintf(stderr, "Failed to read pair\n");
          }
          break;

        case CMD_DELETE:
          num_pairs = parse_read_delete(fd, keys, MAX_WRITE_SIZE, MAX_STRING_SIZE);

          if (num_pairs == 0) {
            fprintf(stderr, "Invalid command. See HELP for usage\n");
            continue;
          }

          if (kvs_delete(num_pairs, keys)) {
            fprintf(stderr, "Failed to delete pair\n");
          }
          break;

        case CMD_SHOW:

          kvs_show();
          break;

        case CMD_WAIT:
          if (parse_wait(fd, &delay, NULL) == -1) {
            fprintf(stderr, "Invalid command. See HELP for usage\n");
            continue;
          }

          if (delay > 0) {
            printf("Waiting...\n");
            kvs_wait(delay);
          }
          break;

        case CMD_BACKUP:

          if (kvs_backup()) {
            fprintf(stderr, "Failed to perform backup.\n");
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