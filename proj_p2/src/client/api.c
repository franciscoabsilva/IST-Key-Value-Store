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
#include <errno.h>
#include <sys/stat.h>


/// @brief Writes a given message to a file descriptor, 
///        filling the rest of the buffer with nulls.
/// @param fd File to be written to.
/// @param message Message to be written on fd.
/// @param size Size of the message.
/// @return on success, returns 1, on error, returns -1
int write_correct_size(int fd, const char *message, size_t size) {
	char buffer[size];
	fill_with_nulls(buffer, message, size);
	return write_all(fd, buffer, size);
}

/// @brief Writes a connect message to the server pipe.
/// @param fdServerPipe File descriptor of the server pipe.
/// @param req_pipe_path Path to the request pipe.
/// @param resp_pipe_path Path to the response pipe.
/// @param notif_pipe_path Path to the notification pipe.
/// @return on success, returns 1, on error, returns -1
int write_connect_message(int fdServerPipe,
						 const char *req_pipe_path,
						 const char *resp_pipe_path,
						 const char *notif_pipe_path) {

	char connectMessage[1 + 3 * MAX_PIPE_PATH_LENGTH];
	connectMessage[0] = OP_CODE_CONNECT;
	fill_with_nulls(connectMessage + 1, req_pipe_path, MAX_PIPE_PATH_LENGTH);
	fill_with_nulls(connectMessage + 1 + MAX_PIPE_PATH_LENGTH, resp_pipe_path, MAX_PIPE_PATH_LENGTH);
	fill_with_nulls(connectMessage + 1 + 2 * MAX_PIPE_PATH_LENGTH, notif_pipe_path, MAX_PIPE_PATH_LENGTH);
	// write connect message on server pipe
	return write_all(fdServerPipe, connectMessage, 1 + 3 * MAX_PIPE_PATH_LENGTH);
}

/// @brief Reads a response from the server pipe.
/// @param fdResponsePipe File descriptor of the response pipe.
/// @param expected_OP_Code Expected OP Code.
/// @param result Result of the operation.
/// @return 0 if the operation was successful, 1 otherwise.
int read_server_response(int fdResponsePipe, const char expected_OP_Code, char *result) {
	char opcode;
	int readingError = 0;

	if (read_all(fdResponsePipe, &opcode, 1, &readingError) <= 0
		|| readingError == -1) {
		fprintf(stderr, "Failed to read opcode from responses pipe.\n");
		return 1;
	}

	if (opcode != expected_OP_Code) {
		fprintf(stderr, "OP Code %c does not correspond to the expected OPCode.\n", opcode);
		return 1;
	}

	if (read_all(fdResponsePipe, result, 1, &readingError) <= 0
		|| readingError == -1) {
		fprintf(stderr, "Failed to read disconnect result from responses pipe.\n");
		return 1;
	}

	char *opName;

	switch (expected_OP_Code) {
		case OP_CODE_CONNECT:
			opName = "connect";
			break;
		case OP_CODE_DISCONNECT:
			opName = "disconnect";
			break;
		case OP_CODE_SUBSCRIBE:
			opName = "subscribe";
			break;
		case OP_CODE_UNSUBSCRIBE:
			opName = "unsubscribe";
			break;
		default:
			opName = "unknown";
			break;
	}
	fprintf(stdout, "Server returned %c for operation: %s\n", *result, opName);
	return 0;
}


int kvs_connect(char const *req_pipe_path, char const *resp_pipe_path,
				char const *server_pipe_path, char const *notif_pipe_path,
				int *fdNotificationPipe, int *fdRequestPipe, int *fdResponsePipe,
				int *fdServerPipe) {

	// connect to Server
	// check if the Registry Pipe exists and if it is writable
	if (access(server_pipe_path, W_OK)) {
		perror("Client could no access registry pipe.\n");
		return 1;
	}

	// open server pipe
	*fdServerPipe = open(server_pipe_path, O_WRONLY);
	if (*fdServerPipe < 0) {
		fprintf(stderr, "Client could not open registry pipe.\n");
		return 1;
	}

	// create Client Pipes
	if (unlink(req_pipe_path) && errno != ENOENT) {
		fprintf(stderr, "Client could not unlink request pipe.\n");
		return 1;
	}
	if (unlink(resp_pipe_path) && errno != ENOENT) {
		fprintf(stderr, "Client could not unlink response pipe.\n");
		return 1;
	}
	if (unlink(notif_pipe_path) && errno != ENOENT) {
		fprintf(stderr, "Client could not unlink notification pipe.\n");
		return 1;
	}

	if (mkfifo(req_pipe_path, 0666)) {
		fprintf(stderr, "Client could not create request pipe.\n");
		return 1;
	}
	if (mkfifo(resp_pipe_path, 0666)) {
		fprintf(stderr, "Client could not create response pipe.\n");
		return 1;
	}
	if (mkfifo(notif_pipe_path, 0666)) {
		fprintf(stderr, "Client could not create notification pipe.\n");
		return 1;
	}

	// send connect message to server
	if(write_connect_message(*fdServerPipe, req_pipe_path, resp_pipe_path, notif_pipe_path) == -1) {
		fprintf(stderr, "Failed to write connect message on server pipe.\n");
		return 1;
	}

	if(close(*fdServerPipe) < 0) {
		fprintf(stderr, "Client could not close server pipe.\n");
	}

	*fdNotificationPipe = open(notif_pipe_path, O_RDONLY);
	if (*fdNotificationPipe < 0) {
		fprintf(stderr, "Client could not open notification pipe.\n");
		return 1;
	}

	*fdRequestPipe = open(req_pipe_path, O_WRONLY);
	if (*fdRequestPipe < 0) {
		fprintf(stderr, "Client could not open request pipe.\n");
		if(close(*fdNotificationPipe)) {
			fprintf(stderr, "Client could not close pipes.\n");
		}
		return 1;
	}

	*fdResponsePipe = open(resp_pipe_path, O_RDONLY);
	if (*fdResponsePipe < 0) {
		fprintf(stderr, "Client could not open response pipe.\n");
		if(close(*fdNotificationPipe) || close(*fdRequestPipe)) {
			fprintf(stderr, "Client could not close pipes.\n");
		}
		return 1;
	}
	

	// read response from server
	const char expected_OP_Code = OP_CODE_CONNECT;
	char result;
	if (read_server_response(*fdResponsePipe, expected_OP_Code, &result) == 1) {
		fprintf(stderr, "Failed to read connect message from response pipe.\n");
	}

	return 0;
}

/// @brief Closes and unlinks the client pipes.
/// @param fdRequestPipe 
/// @param req_pipe_path 
/// @param fdResponsePipe 
/// @param resp_pipe_path 
/// @param fdNotification 
/// @param notif_pipe_path 
void terminate_pipes(int fdRequestPipe, const char *req_pipe_path,
				   int fdResponsePipe, const char *resp_pipe_path,
				   int fdNotification, const char *notif_pipe_path) {
	if(close(fdRequestPipe)  < 0){
		fprintf(stderr, "Client failed to close requests pipe.\n");
	}

	if (unlink(req_pipe_path)) {
		fprintf(stderr, "Client failed to unlink requests pipe.\n");
	}

	if (close(fdNotification) < 0) {
		fprintf(stderr, "Client failed to close notifications pipe.\n");
	}

	if (unlink(notif_pipe_path)) {
		fprintf(stderr, "Client failed to unlink notifications pipe.\n");
	}

	if (close(fdResponsePipe) < 0) {
		fprintf(stderr, "Client failed to close responses pipe.\n");
	}

	if (unlink(resp_pipe_path)) {
		fprintf(stderr, "Client failed to unlink responses pipe.\n");
	}
}

int kvs_disconnect(int fdRequestPipe, int fdResponsePipe, 
				   pthread_t notificationsThread) {
	int error = 0;
	// send disconnect message to server
	const char opcode = OP_CODE_DISCONNECT;
	if (write_all(fdRequestPipe, &opcode, 1) == -1) {
		fprintf(stderr, "Error writing disconnect OP Code on the server pipe\n");
		error = 1;
	}

	// read for response from server
	char result;
	if (read_server_response(fdResponsePipe, OP_CODE_DISCONNECT, &result) == 1) {
		fprintf(stderr, "Failed to read disconnect response from server.\n");
		error = 1;
	}

	if (pthread_join(notificationsThread, NULL)) {
		fprintf(stderr, "Failed to join notification thread\n");
		error = 1;
	}

	return error;
}



int kvs_subscribe(int fdRequestPipe, int fdResponsePipe, const char *key) {

	// write subscription message on requests pipe
	const char opcode = OP_CODE_SUBSCRIBE;
	if (write_all(fdRequestPipe, &opcode, 1) == -1) {
		fprintf(stderr, "Error writing subscribe OP Code on requests pipe\n");
		return 0;
	}

	if (write_correct_size(fdRequestPipe, key, KEY_MESSAGE_SIZE) == -1) {
		fprintf(stderr, "Error writing key on requests pipe\n");
		return 0;
	}

	char result;

	if (read_server_response(fdResponsePipe, OP_CODE_SUBSCRIBE, &result) == 1) {
		fprintf(stderr, "Failed to read subscribe response from server.\n");
	}
	return 1;
}

int kvs_unsubscribe(int fdResquestPipe, int fdResponsePipe, const char *key) {
	// send unsubscribe message to request pipe 
	const char opcode = OP_CODE_UNSUBSCRIBE;
	if (write_all(fdResquestPipe, &opcode, 1) == -1) {
		fprintf(stderr, "Error writing unsubscribe OP Code on requests pipe\n");
		return 1;
	}
	if (write_correct_size(fdResquestPipe, key, KEY_MESSAGE_SIZE) == -1) {
		fprintf(stderr, "Error writing key on requests pipe\n");
		return 1;
	}

	// should read from response pipe
	char result;

	if (read_server_response(fdResponsePipe, OP_CODE_UNSUBSCRIBE, &result) == 1) {
		fprintf(stderr, "Failed to read unsubscribe response from server.\n");
	}
	return 0;
}


