#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "io.h"
#include "constants.h"
#include "kvs.h"
#include "src/common/constants.h"
#include "src/common/io.h"
#include "src/common/protocol.h"
#include <fcntl.h>

#include "client.h"

static struct HashTable *kvs_table = NULL;

typedef struct {
	char key[MAX_STRING_SIZE];
	char value[MAX_STRING_SIZE];
} KeyValuePair;


struct Client *connectedClients[MAX_SESSION_COUNT] = {NULL};
pthread_mutex_t connectedClientsMutex = PTHREAD_MUTEX_INITIALIZER;


/// Calculates a timespec from a delay in milliseconds.
/// @param delay_ms Delay in milliseconds.
/// @return Timespec with the given delay.
static struct timespec delay_to_timespec(unsigned int delay_ms) {
	return (struct timespec) {delay_ms / 1000, (delay_ms % 1000) * 1000000};
}

int kvs_init() {
	if (kvs_table != NULL) {
		fprintf(stderr, "KVS state has already been initialized\n");
		return 1;
	}

	kvs_table = create_hash_table();
	return kvs_table == NULL;
}

int kvs_terminate() {
	if (kvs_table == NULL) {
		fprintf(stderr, "KVS state must be initialized\n");
		return 1;
	}
	free_table(kvs_table);
	return 0;
}

// Alphabetical comparison of pairs
int compare_pairs(const void *a, const void *b) {
	const KeyValuePair *pair1 = (const KeyValuePair *) a;
	const KeyValuePair *pair2 = (const KeyValuePair *) b;
	return strcmp(pair1->key, pair2->key);
}

// Alphabetical comparison of keys
int compare_keys(const void *a, const void *b) {
	const char *key1 = (const char *) a;
	const char *key2 = (const char *) b;
	return strcmp(key1, key2);
}

int lock_write_list(size_t num_pairs, char keys[][MAX_STRING_SIZE],
					int indexList[]) {
	for (size_t i = 0; i < num_pairs; i++) {
		int index = hash(keys[i]);

		// If that index still hasn't been locked
		if (indexList[index] == 0) {
			if (pthread_rwlock_wrlock(&kvs_table->bucketLocks[index])) {
				fprintf(stderr, "Failed to lock bucket %d\n", index);
				return 1;
			}
			indexList[index] = 1;
		}
	}
	return 0;
}

int lock_read_list(size_t num_pairs, char keys[][MAX_STRING_SIZE],
				   int indexList[]) {
	for (size_t i = 0; i < num_pairs; i++) {
		int index = hash(keys[i]);

		// If that index still hasn't been locked
		if (indexList[index] == 0) {
			if (pthread_rwlock_rdlock(&kvs_table->bucketLocks[index])) {
				fprintf(stderr, "Failed to lock bucket %d\n", index);
				return 1;
			}
			indexList[index] = 1;
		}
	}
	return 0;
}

int unlock_list(int indexList[]) {
	for (int i = 0; i < TABLE_SIZE; i++) {
		if (indexList[i] == 1) {
			if (pthread_rwlock_unlock(&kvs_table->bucketLocks[i])) {
				fprintf(stderr, "Failed to unlock bucket %d\n", i);
				return 1;
			}
			indexList[i] = 0;
		}
	}
	return 0;
}

int kvs_write(size_t num_pairs, char keys[][MAX_STRING_SIZE],
			  char values[][MAX_STRING_SIZE]) {
	if (kvs_table == NULL) {
		fprintf(stderr, "KVS state must be initialized\n");
		return 1;
	}

	// Sort the pairs alphabetically
	char pairs[num_pairs][2][MAX_STRING_SIZE];
	for (size_t i = 0; i < num_pairs; i++) {
		strcpy(pairs[i][0], keys[i]);
		strcpy(pairs[i][1], values[i]);
	}
	qsort(pairs, num_pairs, sizeof(pairs[0]), compare_pairs);
	char sortedKeys[num_pairs][MAX_STRING_SIZE];
	for (size_t i = 0; i < num_pairs; i++) {
		strcpy(sortedKeys[i], pairs[i][0]);
	}

	// Lock all meaningful keys
	int indexList[TABLE_SIZE] = {0};
	if (lock_write_list(num_pairs, sortedKeys, indexList)) {
		return 1;
	}

	for (size_t i = 0; i < num_pairs; i++) {
		if (write_pair(kvs_table, pairs[i][0], pairs[i][1])) {
			fprintf(stderr, "Failed to write keypair (%s,%s)\n", pairs[i][0],
					pairs[i][1]);
		}
	}

	if (unlock_list(indexList)) {
		return 1;
	}
	return 0;
}

int kvs_read(size_t num_pairs, char keys[][MAX_STRING_SIZE], int fdOut) {
	if (kvs_table == NULL) {
		fprintf(stderr, "KVS state must be initialized\n");
		return 1;
	}
	qsort(keys, num_pairs, MAX_STRING_SIZE, compare_keys);

	// Lock all meaningful keys
	int indexList[TABLE_SIZE] = {0};
	if (lock_read_list(num_pairs, keys, indexList)) {
		return 1;
	}
	if (write(fdOut, "[", 1) < 0) {
		fprintf(stderr, "Failed to write to output file\n");
	}
	for (size_t i = 0; i < num_pairs; i++) {
		char buffer[MAX_WRITE_SIZE];
		char *result = read_pair(kvs_table, keys[i]);

		if (result == NULL) {
			snprintf(buffer, sizeof(buffer), "(%s,KVSERROR)", keys[i]);
		} else {
			snprintf(buffer, sizeof(buffer), "(%s,%s)", keys[i], result);
		}

		if (write(fdOut, buffer, strlen(buffer)) < 0) {
			fprintf(stderr, "Failed to write to output file\n");
		}
		free(result);
	}
	if (write(fdOut, "]\n", 2) < 0) {
		fprintf(stderr, "Failed to write to output file\n");
	}

	if (unlock_list(indexList)) {
		return 1;
	}
	return 0;
}

int kvs_delete(size_t num_pairs, char keys[][MAX_STRING_SIZE], int fdOut) {
	if (kvs_table == NULL) {
		fprintf(stderr, "KVS state must be initialized\n");
		return 1;
	}
	qsort(keys, num_pairs, MAX_STRING_SIZE, compare_keys);

	// Lock all meaningful keys
	int indexList[TABLE_SIZE] = {0};
	if (lock_write_list(num_pairs, keys, indexList)) {
		return 1;
	}

	int aux = 0;
	for (size_t i = 0; i < num_pairs; i++) {
		if (delete_pair(kvs_table, keys[i]) != 0) {
			if (!aux) {
				if (write(fdOut, "[", 1) < 0) {
					fprintf(stderr, "Failed to write to output file\n");
				}
				aux = 1;
			}
			char buffer[MAX_WRITE_SIZE];
			snprintf(buffer, sizeof(buffer), "(%s,KVSMISSING)", keys[i]);
			if (write(fdOut, buffer, strlen(buffer)) < 0) {
				fprintf(stderr, "Failed to write to output file\n");
			}
		}
	}
	if (aux) {
		if (write(fdOut, "]\n", 2) < 0) {
			fprintf(stderr, "Failed to write to output file\n");
		}
	}

	if (unlock_list(indexList)) {
		return 1;
	}
	return 0;
}

int kvs_show(int fdOut) {
	// Lock all keys
	for (int i = 0; i < TABLE_SIZE; i++) {
		if (pthread_rwlock_rdlock(&kvs_table->bucketLocks[i])) {
			fprintf(stderr, "Failed to lock bucket %d\n", i);
			return 1;
		}
	}

	char buffer[MAX_WRITE_SIZE];
	for (int i = 0; i < TABLE_SIZE; i++) {
		KeyNode *keyNode = kvs_table->table[i];
		while (keyNode != NULL) {
			snprintf(buffer, sizeof(buffer), "(%s, %s)\n", keyNode->key,
					 keyNode->value);
			if (write(fdOut, buffer, strlen(buffer)) < 0) {
				fprintf(stderr, "Failed to write to output file.\n");
			}
			keyNode = keyNode->next;
		}
	}

	for (int i = 0; i < TABLE_SIZE; i++) {
		if (pthread_rwlock_unlock(&kvs_table->bucketLocks[i])) {
			fprintf(stderr, "Failed to unlock bucket %d\n", i);
			return 1;
		}
	}
	return 0;
}

int kvs_backup(int fdBck) {
	for (int i = 0; i < TABLE_SIZE; i++) {
		KeyNode *keyNode = kvs_table->table[i];
		while (keyNode != NULL) {
			char aux[MAX_STRING_SIZE];
			aux[0] = '(';
			size_t num_bytes_copied = 1; // the "("
			// the - 1 are all to leave space for the '/0'
			num_bytes_copied += strn_memcpy(aux + num_bytes_copied,
											keyNode->key, MAX_STRING_SIZE - num_bytes_copied - 1);
			num_bytes_copied += strn_memcpy(aux + num_bytes_copied,
											", ", MAX_STRING_SIZE - num_bytes_copied - 1);
			num_bytes_copied += strn_memcpy(aux + num_bytes_copied,
											keyNode->value, MAX_STRING_SIZE - num_bytes_copied - 1);
			num_bytes_copied += strn_memcpy(aux + num_bytes_copied,
											")\n", MAX_STRING_SIZE - num_bytes_copied - 1);
			aux[num_bytes_copied] = '\0';
			write_str(fdBck, aux);
			keyNode = keyNode->next;
		}
	}
	close(fdBck);

	return 0;
}

void kvs_wait(unsigned int delay_ms) {
	struct timespec delay = delay_to_timespec(delay_ms);
	nanosleep(&delay, NULL);
}

int kvs_subscribe(const char *key, struct Client **client) {
	if(strlen(key) == 0 || strlen(key) > MAX_STRING_SIZE){
		write_to_resp_pipe((*client)->fdResp, OP_CODE_SUBSCRIBE, RESULT_KEY_DOESNT_EXIST);
		fprintf(stderr, "Client tried to subscribe an invalid key\n");
		return 0;
	}
	int index = hash(key);
	if (pthread_rwlock_wrlock(&kvs_table->bucketLocks[index])) {
		fprintf(stderr, "Failed to lock key %d\n", index);
		return -1;
	}

	int subscriptionStatus = 0;
	char result = RESULT_KEY_DOESNT_EXIST;
	KeyNode *keyNode = kvs_table->table[index];
	while (keyNode != NULL) {
		if (strcmp(keyNode->key, key) == 0) {
			subscriptionStatus = add_subscriber(keyNode, (*client)->fdNotif);
			if (subscriptionStatus == -1) {
				fprintf(stderr, "Failed to add subscriber\n");
			}
			result = RESULT_KEY_EXISTS;
			break;
		}
		keyNode = keyNode->next;
	}

	if (pthread_rwlock_unlock(&kvs_table->bucketLocks[index])) {
		fprintf(stderr, "Failed to unlock key %d\n", index);
	}

	if (subscriptionStatus == 0) { // if client wasnt subscribed already
		SubscriptionsKeyNode *newSub = malloc(sizeof(SubscriptionsKeyNode));
		if (newSub == NULL) {
			fprintf(stderr, "Failed to allocate memory for new subscription\n");
			return -1;
		}

		newSub->key = strdup(key);
		if (newSub->key == NULL) {
			fprintf(stderr, "Failed to allocate memory for new subscription key\n");
			free(newSub);
			return -1;
		}

		newSub->next = (*client)->subscriptions;
		(*client)->subscriptions = newSub;
	}

	const char opcode = OP_CODE_SUBSCRIBE;
	if(write_to_resp_pipe((*client)->fdResp, opcode, result) == 1){
		return -1;
	}
	return 0;
}

int kvs_aux_unsubscribe(const char *key, struct Client **client) {
	int index = hash(key);
	if (pthread_rwlock_wrlock(&kvs_table->bucketLocks[index])) {
		fprintf(stderr, "Failed to lock key %d\n", index);
	}

	int result = 1; // subscription not found
	KeyNode *keyNode = kvs_table->table[index];
	while (keyNode != NULL) {
		if (strcmp(keyNode->key, key) == 0) {
			if (keyNode->subscriber == NULL) {
				if (pthread_rwlock_unlock(&kvs_table->bucketLocks[index])) {
					fprintf(stderr, "Failed to unlock key %d\n", index);
				}
				return result;
			}
			result = remove_subscriber(keyNode, (*client)->fdNotif);
			break;
		}
		keyNode = keyNode->next;
	}

	if (pthread_rwlock_unlock(&kvs_table->bucketLocks[index])) {
		fprintf(stderr, "Failed to unlock key %d\n", index);
	}
	return result;
}

void client_remove_subscription(const char *key, struct Client **client) {
    SubscriptionsKeyNode *current = (*client)->subscriptions;
    SubscriptionsKeyNode *prev = NULL;
    
    while (current != NULL) {
        if (strcmp(current->key, key) == 0) {
            if (prev == NULL) {
                (*client)->subscriptions = current->next;
            } else {
                prev->next = current->next;
            }
            SubscriptionsKeyNode *to_free = current; 
            current = current->next; 
            free(to_free->key);
            free(to_free);
        } else {
            prev = current;
            current = current->next;
        }
    }
}


int kvs_unsubscribe(const char *key, struct Client **client) {
	if(strlen(key) == 0 || strlen(key) > MAX_STRING_SIZE){
		write_to_resp_pipe((*client)->fdResp, OP_CODE_UNSUBSCRIBE, 1);
		fprintf(stderr, "Client tried to unsubscribe an invalid key\n");
		return 0;
	}
	
	// Remove the key from the client's subscription list
	client_remove_subscription(key, client);

	// Remove the client from the key's subscriber list on the kvs table
	char result_char = (kvs_aux_unsubscribe(key, client) ? '1' : '0');

	const char opcode = OP_CODE_UNSUBSCRIBE;
	if(write_to_resp_pipe((*client)->fdResp, opcode, result_char) == 1){
		return 1;
	}
	return 0;
}


int add_client(struct Client *client) {
	pthread_mutex_lock(&connectedClientsMutex);
	for (int i = 0; i < MAX_SESSION_COUNT; i++) {
		if (connectedClients[i] == NULL) {
			connectedClients[i] = client;
			pthread_mutex_unlock(&connectedClientsMutex);
			return 0;
		}
	}
	pthread_mutex_unlock(&connectedClientsMutex);
	fprintf(stderr, "Client list is full\n");
	return 1;
}

int remove_client(int fdReqPipe, int fdRespPipe, int fdNotifPipe) {
	pthread_mutex_lock(&connectedClientsMutex);
	for (int i = 0; i < MAX_SESSION_COUNT; i++) {
		if (connectedClients[i] != NULL
			&& connectedClients[i]->fdReq == fdReqPipe 
			&& connectedClients[i]->fdResp == fdRespPipe
			&& connectedClients[i]->fdNotif == fdNotifPipe) {

			free(connectedClients[i]);
			connectedClients[i] = NULL;
			pthread_mutex_unlock(&connectedClientsMutex);
			return 0;
		}
	}
	pthread_mutex_unlock(&connectedClientsMutex);
	fprintf(stderr, "Client not found in the list\n");
	return 1;
}

int kvs_connect(char *req_pipe, char *resp_pipe, char *notif_pipe, struct Client **client) {

	int result = 0; // 0 if successful, 1 otherwise

	int fdNotifPipe = open(notif_pipe, O_WRONLY);
	if (fdNotifPipe < 0) {
		fprintf(stderr, "Failed to open notifications pipe\n");
		result = 1;
	}

	int fdReqPipe = open(req_pipe, O_RDONLY);
	if (fdReqPipe < 0) {
		fprintf(stderr, "Failed to open requests pipe\n");
		result = 1;
	}

	int fdRespPipe = open(resp_pipe, O_WRONLY);
	if (fdRespPipe < 0) {
		fprintf(stderr, "Failed to open responses pipe\n");
		return 1;
	}

	*client = malloc(sizeof(struct Client)); // if an error occurs here, no disconnect is needed
	if (*client == NULL) {
		if(close(fdReqPipe) < 0 || close(fdRespPipe) < 0 || close(fdNotifPipe) < 0){
			fprintf(stderr, "Failed to close pipes\n");
		}
		fprintf(stderr, "Failed to allocate memory for client\n");
		result = -1;
	}
	else {
		(*client)->fdReq = fdReqPipe;
		(*client)->fdResp = fdRespPipe;
		(*client)->fdNotif = fdNotifPipe;
		(*client)->subscriptions = NULL;

		if (add_client(*client)) {
			fprintf(stderr, "Failed to add client to list\n");
			result = 1;
		}
	}
	
	char result_char = result ? '1' : '0';

	write_to_resp_pipe((*client)->fdResp, OP_CODE_CONNECT, result_char);
	return result;
}

void kvs_disconnect(struct Client **client) {

	// Unsubscribe all keys
	SubscriptionsKeyNode *current = (*client)->subscriptions;
	while (current != NULL) {
		kvs_aux_unsubscribe(current->key, client);
		SubscriptionsKeyNode *next = current->next;
		free(current->key);
		free(current);
		current = next;
	}
	(*client)->subscriptions = NULL;

	char result = '0';
	if (close((*client)->fdReq) < 0) {
		fprintf(stderr, "kvs_disconnect: Failed to close requests pipe.\n");
		result = '1';
	}

	if (close((*client)->fdNotif) < 0) {
		fprintf(stderr, "kvs_disconnect: Failed to close from notifications pipe.\n");
		result = '1';
	}

	if(write_to_resp_pipe((*client)->fdResp, OP_CODE_DISCONNECT, result) == 1) {
		fprintf(stderr, "kvs_disconnect: Failed to write disconnect response to responses pipe\n");
	}

	if (close((*client)->fdResp) < 0) {
		fprintf(stderr, "kvs_disconnect: Failed to close responses pipe.\n");
	}

	if (remove_client((*client)->fdReq, (*client)->fdResp, (*client)->fdNotif)) {
		fprintf(stderr, "kvs_disconnect: Failed to remove client from the list\n");
	}
}


int clean_all_clients() {
	pthread_mutex_lock(&connectedClientsMutex);
	for (int i = 0; i < MAX_SESSION_COUNT; i++) {

		if (connectedClients[i] == NULL) continue;
		SubscriptionsKeyNode *current = connectedClients[i]->subscriptions;
		while (current != NULL) {
			kvs_aux_unsubscribe(current->key, &connectedClients[i]);
			SubscriptionsKeyNode *next = current->next;
			free(current->key);
			free(current);
			current = next;
		}
		connectedClients[i]->subscriptions = NULL;

		if (close(connectedClients[i]->fdReq) < 0) {
			fprintf(stderr, "Failed to close requests pipe.\n");
		}

		if (close(connectedClients[i]->fdNotif) < 0) {
			fprintf(stderr, "Failed to close notifications pipe.\n");
		}

		if (close(connectedClients[i]->fdResp) < 0) {
			fprintf(stderr, "Failed to close responses pipe.\n");
		}

		free(connectedClients[i]);
		connectedClients[i] = NULL;
	}
	pthread_mutex_unlock(&connectedClientsMutex);
	return 0;
}
