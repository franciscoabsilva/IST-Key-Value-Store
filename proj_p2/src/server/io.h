#ifndef KVS_IO_H
#define KVS_IO_H

#include <unistd.h>

/// Writes a string to the given file descriptor.
/// @param fd The file descriptor to write to.
/// @param str The string to write.
void write_str(int fd, const char *str);

/// Writes an unsigned integer to the given file descriptor.
/// @param fd The file descriptor to write to.
/// @param value The value to write.
void write_uint(int fd, int value);

/// @brief Copies bytes from src to dest, not including the '\0'
/// @param dest 
/// @param src 
/// @param n Maximum number of bytes to copy from src to dest
/// @return Number of bytes copied
size_t strn_memcpy(char *dest, const char *src, size_t n);

/// @brief Read a connect message from the server pipe
/// @param fdServerPipe File descriptor of the server pipe
/// @param opcode The operation code
/// @param req_pipe The request pipe path
/// @param resp_pipe The response pipe path
/// @param notif_pipe The notification pipe path
/// @return 0 on success, 1 on error
int read_connect_message(int fdServerPipe, char *opcode, char *req_pipe, char *resp_pipe, char *notif_pipe);

/// @brief Write a response message to the client	
/// @param fdRespPipe File descriptor of the response pipe
/// @param opcode The operation code
/// @param result The result of the operation
/// @return 0 on success, 1 on error
int write_to_resp_pipe (int fdRespPipe, const char opcode, const char result);

#endif  // KVS_IO_H
