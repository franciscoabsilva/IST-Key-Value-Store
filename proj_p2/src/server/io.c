#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>


void write_str(int fd, const char *str) {
  size_t len = strlen(str);
  const char *ptr = str;

  while (len > 0) {
    ssize_t written = write(fd, ptr, len);

    if (written < 0) {
      perror("Error writing string");
      break;
    }

    ptr += written;
    len -= (size_t)written;
  }
}

void write_uint(int fd, int value) {
  char buffer[16];
  size_t i = 16;

  for (; value > 0; value /= 10) {
    buffer[--i] = '0' + (char)(value % 10);
  }

  if (i == 16) {
    buffer[--i] = '0';
  }

  while (i < 16) {
    i += (size_t)write(fd, buffer + i, 16 - i);
  }
}

size_t strn_memcpy(char* dest, const char* src, size_t n) {
    // strnlen is async signal safe in recent versions of POSIX
    size_t bytes_to_copy = strnlen(src, n);
    memcpy(dest, src, bytes_to_copy);
    return bytes_to_copy;
}

char* concatenate_write_registry(char* message, const char* req_pipe_path,
                                 const char* resp_pipe_path,
                                 const char* notif_pipe_path) {
  message[0] = 1;
  strn_memcpy(message + 1, req_pipe_path, 40);
  strn_memcpy(message + 41, resp_pipe_path, 40);
  strn_memcpy(message + 81, notif_pipe_path, 40);
  return message;
}