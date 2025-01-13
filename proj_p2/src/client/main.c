#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>


#include "parser.h"
#include "src/client/api.h"
#include "src/common/constants.h"
#include "src/common/io.h"

int serverDisconnected = 0;

struct thread_args {
	int fdNotificationPipe;
	int fdRequestPipe;
	int fdResponsePipe;
	const char *req_pipe_path;
	const char *resp_pipe_path;
	const char *notif_pipe_path;
};

void *process_notif_thread(void *arg) {
	struct thread_args *args = (struct thread_args *) arg;
	int *fdNotificationPipe = &args->fdNotificationPipe;
	int *fdRequestPipe = &args->fdRequestPipe;
	int *fdResponsePipe = &args->fdResponsePipe;
	const char *req_pipe_path = args->req_pipe_path;
	const char *resp_pipe_path = args->resp_pipe_path;
	const char *notif_pipe_path = args->notif_pipe_path;
	
	int readError = 0;
	int status = 0;

	// FIXME ???? as leituras deviam ser feitas as duas de seguida nao?
	char key[KEY_MESSAGE_SIZE];
	char value[KEY_MESSAGE_SIZE];
	while (1) {
		status = read_all(*fdNotificationPipe, key, KEY_MESSAGE_SIZE, &readError);
		if (status == PIPES_CLOSED) {
			serverDisconnected = 1;
			terminate_pipes(*fdRequestPipe, req_pipe_path, *fdResponsePipe,
							 resp_pipe_path, *fdNotificationPipe, notif_pipe_path);
			pthread_exit(NULL);
		}
		if (status == -1 || readError == 1) {
			fprintf(stderr, "Failed to read key from notifications pipe.\n");
		}

		status = read_all(*fdNotificationPipe, value, KEY_MESSAGE_SIZE, &readError);
		if (status == PIPES_CLOSED) {
			serverDisconnected = 1;
			terminate_pipes(*fdRequestPipe, req_pipe_path, *fdResponsePipe,
							 resp_pipe_path, *fdNotificationPipe, notif_pipe_path);
			pthread_exit(NULL);
		}
		if (status == -1 || readError == 1) {
			fprintf(stderr, "Failed to read key from notifications pipe.\n");
		}
		fprintf(stdout, "(%s,%s)\n", key, value);
	}
}

int main(int argc, char *argv[]) {
	if (argc != 3) {
		fprintf(stderr, "Usage: %s <client_unique_id> <register_pipe_path>\n", argv[0]);
		return 1;
	}

	char req_pipe_path[256] = "/tmp/req";
	char resp_pipe_path[256] = "/tmp/resp";
	char notif_pipe_path[256] = "/tmp/notif";

	char keys[MAX_NUMBER_SUB][MAX_STRING_SIZE] = {0};
	unsigned int delay_ms;
	size_t num;

	strncat(req_pipe_path, argv[1], strlen(argv[1]) * sizeof(char));
	strncat(resp_pipe_path, argv[1], strlen(argv[1]) * sizeof(char));
	strncat(notif_pipe_path, argv[1], strlen(argv[1]) * sizeof(char));
	char *server_pipe_path = argv[2];

	int fdNotificationPipe;
	int fdRequestPipe;
	int fdResponsePipe;
	int fdServerPipe;

	if (kvs_connect(req_pipe_path, resp_pipe_path, server_pipe_path, notif_pipe_path,
					&fdNotificationPipe, &fdRequestPipe, &fdResponsePipe,
					&fdServerPipe) != 0) {
		fprintf(stderr, "Failed to connect to the server\n");
		return 1;
	}

	struct thread_args args = {fdNotificationPipe, fdRequestPipe, fdResponsePipe,
							   req_pipe_path, resp_pipe_path, notif_pipe_path};
	pthread_t notificationsThread;
	if (pthread_create(&notificationsThread, NULL, process_notif_thread, (void *) (&args))) {
		fprintf(stderr, "Failed to create thread\n");
		return 1;
	}

	// ???? apagar
	fprintf(stdout, "Connected to server\n");
	while (1) {
		if(serverDisconnected){
			if(pthread_join(notificationsThread, NULL)){
				fprintf(stderr, "Failed to join notification thread\n");
			}
			return 0;
		}

		switch (get_next(STDIN_FILENO)) {
			case CMD_DISCONNECT:
				if (kvs_disconnect(fdRequestPipe, req_pipe_path, fdResponsePipe, resp_pipe_path,
								   fdNotificationPipe, notif_pipe_path, notificationsThread) != 0) {
					fprintf(stderr, "Failed to disconnect to the server\n");
					return 1;
				}
				// TODO: end notifications thread
				//??? apagar
				printf("Disconnected from server\n");
				return 0;

			case CMD_SUBSCRIBE:
				num = parse_list(STDIN_FILENO, keys, 1, MAX_STRING_SIZE);
				if (num == 0) {
					fprintf(stderr, "Invalid command. See HELP for usage\n");
					continue;
				}
				if (!kvs_subscribe(fdRequestPipe, fdResponsePipe, keys[0])) {
					fprintf(stderr, "Command subscribe failed\n");
				}
				break;

			case CMD_UNSUBSCRIBE:
				num = parse_list(STDIN_FILENO, keys, 1, MAX_STRING_SIZE);
				if (num == 0) {
					fprintf(stderr, "Invalid command. See HELP for usage\n");
					continue;
				}

				if (kvs_unsubscribe(fdRequestPipe, fdResponsePipe, keys[0])) {
					fprintf(stderr, "Command subscribe failed\n");
				}

				break;

			case CMD_DELAY:
				if (parse_delay(STDIN_FILENO, &delay_ms) == -1) {
					fprintf(stderr, "Invalid command. See HELP for usage\n");
					continue;
				}

				if (delay_ms > 0) {
					printf("Waiting...\n");
					delay(delay_ms);
				}
				break;

			case CMD_INVALID:
				fprintf(stderr, "Invalid command. See HELP for usage\n");
				break;

			case CMD_EMPTY:
				break;

			case EOC:
				// input should end in a disconnect, or it will loop here forever
				// FIXME esta tarefa é que termina a fifo de notificações!
				kvs_disconnect(fdRequestPipe, req_pipe_path,
							   fdResponsePipe, resp_pipe_path,
							   fdNotificationPipe, notif_pipe_path,
							   notificationsThread);
				break;
		}
	}
}
