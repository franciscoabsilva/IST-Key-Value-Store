#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include "src/common/io.h"
#include "src/common/constants.h"
#include "src/common/protocol.h"

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
		len -= (size_t) written;
	}
}

void write_uint(int fd, int value) {
	char buffer[16];
	size_t i = 16;

	for (; value > 0; value /= 10) {
		buffer[--i] = '0' + (char) (value % 10);
	}

	if (i == 16) {
		buffer[--i] = '0';
	}

	while (i < 16) {
		i += (size_t) write(fd, buffer + i, 16 - i);
	}
}

size_t strn_memcpy(char *dest, const char *src, size_t n) {
	// strnlen is async signal safe in recent versions of POSIX
	size_t bytes_to_copy = strnlen(src, n);
	memcpy(dest, src, bytes_to_copy);
	return bytes_to_copy;
}

char *concatenate_write_registry(char *message, const char *req_pipe_path,
								 const char *resp_pipe_path,
								 const char *notif_pipe_path) {
	message[0] = 1;
	strn_memcpy(message + 1, req_pipe_path, 40);
	strn_memcpy(message + 41, resp_pipe_path, 40);
	strn_memcpy(message + 81, notif_pipe_path, 40);
	return message;
}

int read_connect_message(int fdServerPipe, char *opcode, char *req_pipe, char *resp_pipe, char *notif_pipe) {
	int reading_error = 0;

	//FIX ME check why do we care about EOF (return of the read_all)
	read_all(fdServerPipe, opcode, 1, &reading_error);
	if (reading_error) {
		fprintf(stderr, "Failed to read OP Code from server pipe\n");
		return -1;
	}

	if (*opcode != OP_CODE_CONNECT) {
		fprintf(stderr, "OP Code %c does not correspond to the connect opcode\n", *opcode);
		return -1;
	}

	read_all(fdServerPipe, req_pipe, MAX_PIPE_PATH_LENGTH, &reading_error);
	if (reading_error == -1) {
		fprintf(stderr, "Failed to read requests pipe path from server pipe\n");
		return -1;
	}
	printf("Req Pipe %s.\n", req_pipe); // ????clientapi.c

	read_all(fdServerPipe, resp_pipe, MAX_PIPE_PATH_LENGTH, &reading_error);
	if (reading_error) {
		fprintf(stderr, "Failed to read responses pipe path from server pipe\n");
		return -1;
	}
	printf("Resp Pipe %s.\n", resp_pipe); // ????

	read_all(fdServerPipe, notif_pipe, MAX_PIPE_PATH_LENGTH, &reading_error);
	if (reading_error) {
		fprintf(stderr, "Failed to read notifications pipe path from server pipe\n");
		return -1;
	}
	printf("Notif Pipe %s.\n", notif_pipe); // ????

	return 0;
}

int write_to_resp_pipe (int fdRespPipe, const char opcode, const char result) {
	if (write_all(fdRespPipe, &opcode, 1) == -1) {
		fprintf(stderr, "Failed to write OP Code %c to the responses pipe.\n", opcode);
		return 1;
	}
	if (write_all(fdRespPipe, &result, 1) == -1) {
		fprintf(stderr, "Failed to write result %c to the responses pipe.\n", result);
		return 1;
	}
	return 0;
}