#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <dirent.h>
#include <signal.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <errno.h>
#include <semaphore.h>


#include "constants.h"
#include "operations.h"
#include "parser.h"
#include "src/common/io.h"
#include "src/common/constants.h"
#include "src/common/protocol.h"

#include "client.h"

pthread_mutex_t backupCounterMutex;
pthread_mutex_t dirMutex;
pthread_rwlock_t globalHashLock;
pthread_mutex_t clientsBufferMutex; // For reading


struct ThreadArgs {
	DIR *dir;
	char *directory_path;
	unsigned int *backupCounter;
};

struct ClientInfo {
	char respFifo[MAX_PIPE_PATH_LENGTH];
	char reqFifo[MAX_PIPE_PATH_LENGTH];
	char notifFifo[MAX_PIPE_PATH_LENGTH];
};

struct ClientInfo *clientsBuffer[MAX_SESSION_COUNT];
sem_t readSem;
sem_t writeSem;

// reading index for the clientsBuffer
int in, out;
int restartClients = 0;

void *process_thread(void *arg) {
	sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGUSR1);
    pthread_sigmask(SIG_BLOCK, &set, NULL);

	struct ThreadArgs *arg_struct = (struct ThreadArgs *) arg;
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

		if (lenFilePath < 0 || lenFilePath >= MAX_JOB_FILE_NAME_SIZE) {
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
			fprintf(stderr, "Failed to open output file.\n");
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
							 (int) (strlen(filePath) - 4), filePath, fileBackups);

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

int read_connect_message(int fdServerPipe, char *opcode, char *req_pipe, char *resp_pipe, char *notif_pipe) {
	int reading_error = 0;
	char buffer[MAX_PIPE_PATH_LENGTH*3 + 1];

	if(read_all(fdServerPipe, buffer, MAX_PIPE_PATH_LENGTH*3 + 1, &reading_error) <= 0
	   || reading_error == 1) {
		return 1;
	}
	
	*opcode = buffer[0];
    opcode[1] = '\0'; 
	
    if (*opcode != OP_CODE_CONNECT) return 1;

	strncpy(req_pipe, &buffer[1], MAX_PIPE_PATH_LENGTH);
    req_pipe[MAX_PIPE_PATH_LENGTH] = '\0';

    strncpy(resp_pipe, &buffer[1 + MAX_PIPE_PATH_LENGTH], MAX_PIPE_PATH_LENGTH);
    resp_pipe[MAX_PIPE_PATH_LENGTH] = '\0'; 
    
    strncpy(notif_pipe, &buffer[1 + MAX_PIPE_PATH_LENGTH * 2], MAX_PIPE_PATH_LENGTH);
    notif_pipe[MAX_PIPE_PATH_LENGTH] = '\0';
	
	return 0;
}

/// @brief 
/// @param fdNotifPipe 
/// @param fdReqPipe 
/// @param fdRespPipe 
/// @param opcode 
/// @return 1 if CONNECT, SUBSCRIBE, UNSUBSCRIBE, 0 if DISCONNECT, -1 unknown opcode
int manage_request(struct Client *client, const char opcode) {

	switch (opcode) {
		case OP_CODE_CONNECT: {
			fprintf(stderr, "Client already connected to server.\n");
			return 0;
		}

		case OP_CODE_DISCONNECT: {
			kvs_disconnect(&client);
			return 1;
		}

		case OP_CODE_SUBSCRIBE: {
			char key[KEY_MESSAGE_SIZE];
			int readingError = 0;
			if(read_all(client->fdReq, key, KEY_MESSAGE_SIZE, &readingError) <= 0){
				fprintf(stderr, "Failed to read key from requests pipe.\n");
				kvs_disconnect(&client);
				return 1;
			}
			if (kvs_subscribe(key, &client)) {
				fprintf(stderr, "Failed to subscribe client\n");
				kvs_disconnect(&client);
				return 1;
			}
			return 0;
		}

		case OP_CODE_UNSUBSCRIBE: {
			char key[KEY_MESSAGE_SIZE];
			int readingError = 0;
			if (read_all(client->fdReq, key, KEY_MESSAGE_SIZE, &readingError) <= 0) {
				fprintf(stderr, "Failed to read key from requests pipe. Client was disconnected.\n");
				kvs_disconnect(&client);
				return 1;
			}
			kvs_unsubscribe(key, &client);
			return 0;
		}
		default: {
			fprintf(stderr, "Unrecognized OP Code: %c. Client was disconnected.\n", opcode);
			kvs_disconnect(&client);
			return 1;
		}
	}
}


void *process_client_thread() {
	sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGUSR1);
    pthread_sigmask(SIG_BLOCK, &set, NULL);

	while (1) {
		// Read from producer consumer buffer
		sem_wait(&readSem);
		pthread_mutex_lock(&clientsBufferMutex);

		struct ClientInfo *newClient = clientsBuffer[out];

		char reqPath[MAX_PIPE_PATH_LENGTH];
		char respPath[MAX_PIPE_PATH_LENGTH];
		char notifPath[MAX_PIPE_PATH_LENGTH];

		strcpy(reqPath, newClient->reqFifo);
		strcpy(respPath, newClient->respFifo);
		strcpy(notifPath, newClient->notifFifo);

		free(newClient);

		out++;
		if (out == MAX_SESSION_COUNT) out = 0;

		pthread_mutex_unlock(&clientsBufferMutex);
		sem_post(&writeSem);

		struct Client *client = NULL;
		int status = kvs_connect(reqPath, respPath, notifPath, &client);
		if(status){
			fprintf(stderr, "Failed to connect to the server\n");
			if(status == -1) continue; // could not allocate client
			kvs_disconnect(&client);
			continue;
		}

		int clientStatus = 0;
		int readingError = 0;
		char opcode = OP_CODE_CONNECT;
		status = 0;

		while (clientStatus != CLIENT_TERMINATED) {
			status = read_all(client->fdReq, &opcode, 1, &readingError);
			if (status != 1) {
				if(status == -1 || readingError){
					fprintf(stderr, "Failed to read OP Code from requests pipe.\n");
				}
				kvs_disconnect(&client);
				break;
			}
			clientStatus = manage_request(client, opcode);
		}
	}
	return NULL;
}


void handle_SIGUSR1(){
	restartClients = 1;
	sem_post(&writeSem);
}

int restart_clients(){
	clean_all_clients();
	restartClients = 0;
	return 0;
}

void *process_host_thread(void *arg){
	sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGUSR1);
    pthread_sigmask(SIG_UNBLOCK, &set, NULL);

	pid_t pid = getpid();
	printf("PID: %d\n", pid); // ???? TODO TIRAR

	const char *fifo_path = (const char *) arg;

	in  = 0;
	out = 0;
	
	// Create and open server pipe
	if (unlink(fifo_path) && errno != ENOENT) {
		fprintf(stderr, "Failed to unlink server pipe\n");
		return NULL;
	}

	if (mkfifo(fifo_path, 0666)) {
		fprintf(stderr, "Failed to create server pipe\n");
		return NULL;
	}

	sem_init(&readSem, 0, 0);
	sem_init(&writeSem, 0, MAX_SESSION_COUNT);
	pthread_mutex_init(&clientsBufferMutex, NULL);

	int fdServerPipe = open(fifo_path, O_RDONLY);
	if (fdServerPipe < 0) {
		fprintf(stderr, "Failed to open server pipe\n");
		if (pthread_mutex_destroy(&clientsBufferMutex)) 
			fprintf(stderr, "Failed to destroy clientsBufferMutex\n");
		if (sem_destroy(&readSem) == -1 || sem_destroy(&writeSem)) 
			fprintf(stderr, "Failed to destroy semaphores\n");
		return NULL;
	}

	// Create threads to deal with clients
	pthread_t thread[MAX_SESSION_COUNT];
	for(int i = 0; i < MAX_SESSION_COUNT; i++){
		if(pthread_create(&thread[i], NULL, process_client_thread, NULL)){
			fprintf(stderr, "Failed to create thread\n");
		}
	}

	char opCode;
	char req_pipe[MAX_PIPE_PATH_LENGTH];
	char resp_pipe[MAX_PIPE_PATH_LENGTH];
	char notif_pipe[MAX_PIPE_PATH_LENGTH];

	while (1) {
		sem_wait(&writeSem);
		
		// check if SIGUSR1 was sent
		if(restartClients){
			restart_clients();
			continue;
		} 
		
		if (read_connect_message(fdServerPipe, &opCode, req_pipe, resp_pipe, notif_pipe) == 1) {
			sem_post(&writeSem);
			continue;
		}

		struct ClientInfo *newClient = malloc(sizeof(struct ClientInfo));
		if(newClient == NULL){
			fprintf(stderr, "Failed to allocate memory for new client\n");
			sem_post(&writeSem);
			continue;
		}
		strcpy(newClient->respFifo, resp_pipe);
		strcpy(newClient->reqFifo, req_pipe);
		strcpy(newClient->notifFifo, notif_pipe);

		// add the new client to producer-consumer buffer
		clientsBuffer[in] = newClient;
		in++;
		if (in == MAX_SESSION_COUNT) in = 0;
		sem_post(&readSem);
	}
	return NULL;
}

int main(int argc, char *argv[]) {	
	signal(SIGPIPE, SIG_IGN);

    // Configurar o tratador de SIGUSR1
    struct sigaction sa;
    sa.sa_handler = handle_SIGUSR1;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR1, &sa, NULL);

	if (argc != 5) {
		fprintf(stderr, "Usage: %s <dir_jobs> <max_threads> <backups_max> [name_registry_FIFO]\n", argv[0]);
		return 1;
	}

	char *directory_path = argv[1];
	unsigned int backupCounter = (unsigned int) strtoul(argv[2], NULL, 10);
	unsigned int MAX_THREADS = (unsigned int) strtoul(argv[3], NULL, 10);
	const char *fifo_path = argv[4];

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

	// create jobs threads
	pthread_t thread[MAX_THREADS];
	struct ThreadArgs args = {dir, directory_path, &backupCounter};
	for (unsigned int i = 0; i < MAX_THREADS; i++) {
		if (pthread_create(&thread[i], NULL, process_thread, (void *) &args)) {
			fprintf(stderr, "Failed to create thread\n");
		}
	}
	
	// create thread that deals with clients	
	pthread_t host_thread;
	if (pthread_create(&host_thread, NULL, process_host_thread, (void *) fifo_path)) {
		fprintf(stderr, "Failed to create thread\n");
	}

	for (unsigned int i = 0; i < MAX_THREADS; i++) {
		if (pthread_join(thread[i], NULL)) {
			fprintf(stderr, "Failed to join job thread\n");
		}
	}
	
	if (pthread_join(host_thread, NULL)) {
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