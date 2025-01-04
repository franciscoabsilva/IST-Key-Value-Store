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


static struct HashTable *kvs_table = NULL;

typedef struct {
  char key[MAX_STRING_SIZE];
  char value[MAX_STRING_SIZE];
} KeyValuePair;

/// Calculates a timespec from a delay in milliseconds.
/// @param delay_ms Delay in milliseconds.
/// @return Timespec with the given delay.
static struct timespec delay_to_timespec(unsigned int delay_ms) {
  return (struct timespec){delay_ms / 1000, (delay_ms % 1000) * 1000000};
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
  const KeyValuePair *pair1 = (const KeyValuePair *)a;
  const KeyValuePair *pair2 = (const KeyValuePair *)b;
  return strcmp(pair1->key, pair2->key);
}

// Alphabetical comparison of keys
int compare_keys(const void *a, const void *b) {
  const char *key1 = (const char *)a;
  const char *key2 = (const char *)b;
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
    fprintf(stderr, "Failed to write to output file");
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
      fprintf(stderr, "Failed to write to output file");
    }
    free(result);
  }
  if (write(fdOut, "]\n", 2) < 0) {
    fprintf(stderr, "Failed to write to output file");
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
          fprintf(stderr, "Failed to write to output file");
        }
        aux = 1;
      }
      char buffer[MAX_WRITE_SIZE];
      snprintf(buffer, sizeof(buffer), "(%s,KVSMISSING)", keys[i]);
      if (write(fdOut, buffer, strlen(buffer)) < 0) {
        fprintf(stderr, "Failed to write to output file");
      }
    }
  }
  if (aux) {
    if (write(fdOut, "]\n", 2) < 0) {
      fprintf(stderr, "Failed to write to output file");
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
        fprintf(stderr, "Failed to write to output file");
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

int kvs_subscribe(const char *key, int fdNotifPipe, int fdRespPipe) {
  int index = hash(key);
  if (pthread_rwlock_wrlock(&kvs_table->bucketLocks[index])) {
    fprintf(stderr, "Failed to lock key %d\n", index);
    return 1;
  }

  int result = RESULT_KEY_DOESNT_EXIST; 
  KeyNode *keyNode = kvs_table->table[index];
  while (keyNode != NULL) {
      if (strcmp(keyNode->key, key) == 0) {
          if(add_subscriber(keyNode, fdNotifPipe) == -1){
            fprintf(stderr, "Failed to add subscriber\n");
            //FIXME result = RESULT_ERROR;
          }
          result = RESULT_KEY_EXISTS;
          break;
        keyNode = keyNode->next;
    }
  }
  if (pthread_rwlock_unlock(&kvs_table->bucketLocks[index])) {
      fprintf(stderr, "Failed to unlock key %d\n", index);
  }
  int opcode = OP_CODE_SUBSCRIBE;
  write_all(fdRespPipe, &opcode, 1);
  write_all(fdRespPipe, &result, 1);
  return 0;
}

int kvs_aux_unsubscribe(const char *key, int fdNotifPipe){
  int index = hash(key);
  if (pthread_rwlock_wrlock(&kvs_table->bucketLocks[index])) {
    fprintf(stderr, "Failed to lock key %d\n", index);
  }

  int result = 1; // subscription not found
  KeyNode *keyNode = kvs_table->table[index];
  while (keyNode != NULL) {
      if (strcmp(keyNode->key, key) == 0) {
        result = remove_subscriber(keyNode->subscriber, fdNotifPipe);
        break;
      }
      keyNode = keyNode->next;
    }
  if (pthread_rwlock_unlock(&kvs_table->bucketLocks[index])) {
      fprintf(stderr, "Failed to unlock key %d\n", index);
  }
  return result;
}

int kvs_unsubscribe(const char *key, int fdNotifPipe, int fdRespPipe){
  int result = kvs_aux_unsubscribe(key, fdNotifPipe);
  int opcode = OP_CODE_SUBSCRIBE;
  if(write_all(fdRespPipe, &opcode, 1) == -1){
    fprintf(stderr, "Failed to write unsubscribe OP Code on the responses pipe.\n");
  }
  if(write_all(fdRespPipe, &result, 1) == -1){
    fprintf(stderr, "Failed to write unsubscribe result on the responses pipe.\n");
  }
  return 0;
}


int kvs_disconnect(int fdRespPipe, int fdReqPipe, int fdNotifPipe, int subCount,
                   char subscribedKeys[MAX_NUMBER_SUB][MAX_STRING_SIZE]){
  for(int i = 0; i < subCount; i++ ){
    kvs_aux_unsubscribe(subscribedKeys[i], fdNotifPipe);
  }
  // FIXME COMO DAR RESPOSTA NO DISCONNECT?
  if(close(fdRespPipe) < 0) {
    fprintf(stderr, "Failed to close responses pipe.\n");
  }

  if(close(fdReqPipe) < 0){
    fprintf(stderr, "Failed to close requests pipe.\n");
  }

  if(close(fdNotifPipe) < 0){
    fprintf(stderr, "Failed to close notifications pipe.\n");
  }
  return 0;
}

