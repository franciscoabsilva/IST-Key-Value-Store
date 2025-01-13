#ifndef KVS_OPERATIONS_H
#define KVS_OPERATIONS_H

#include <stddef.h>
#include "src/common/constants.h"
#include "client.h"



/// Initializes the KVS state.
/// @return 0 if the KVS state was initialized successfully, 1 otherwise.
int kvs_init();

/// Destroys the KVS state.
/// @return 0 if the KVS state was terminated successfully, 1 otherwise.
int kvs_terminate();

/// Writes a key value pair to the KVS. If key already exists it is updated.
/// @param num_pairs Number of pairs being written.
/// @param keys Array of keys' strings.
/// @param values Array of values' strings.
/// @return 0 if the pairs were written successfully, 1 otherwise.
int kvs_write(size_t num_pairs, char keys[][MAX_STRING_SIZE], char values[][MAX_STRING_SIZE]);

/// Reads values from the KVS.
/// @param num_pairs Number of pairs to read.
/// @param keys Array of keys' strings.
/// @param fd File descriptor to write the (successful) output.
/// @return 0 if the key reading, 1 otherwise.
int kvs_read(size_t num_pairs, char keys[][MAX_STRING_SIZE], int fdOut);

/// Deletes key value pairs from the KVS.
/// @param num_pairs Number of pairs to read.
/// @param keys Array of keys' strings.
/// @return 0 if the pairs were deleted successfully, 1 otherwise.
int kvs_delete(size_t num_pairs, char keys[][MAX_STRING_SIZE], int fdOut);

/// Writes the state of the KVS.
/// @param fd File descriptor to write the output.
int kvs_show(int fdOut);

/// Creates a backup of the KVS state and stores it in the correspondent
/// backup file
/// @return 0 if the backup was successful, 1 otherwise.
int kvs_backup(int fdBck);

/// Waits for a given amount of time.
/// @param delay_us Delay in milliseconds.
void kvs_wait(unsigned int delay_ms);


int remove_client(int fdReqPipe, int fdRespPipe, int fdNotifPipe);

int kvs_subscribe(const char *key, struct Client **client);

int kvs_unsubscribe(const char *key, struct Client **client);

// Connects to the client.
/// @param req_pipe Path to the request pipe.
/// @param resp_pipe Path to the response pipe.
/// @param notif_pipe Path to the notification pipe.
/// @param client Pointer to the client struct.
/// @return 0 if the connection was successful, -1 if the client was not allocated, 1 for other errors.
int kvs_connect(char *req_pipe, char *resp_pipe, char *notif_pipe, struct Client **client);

void kvs_disconnect(struct Client **client);

int clean_all_clients();

#endif  // KVS_OPERATIONS_H
