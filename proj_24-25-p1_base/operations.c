#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "kvs.h"
#include "constants.h"

static struct HashTable* kvs_table = NULL;
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

int compare_pairs(const void *a, const void *b) {
    const KeyValuePair *pair1 = (const KeyValuePair *)a;
    const KeyValuePair *pair2 = (const KeyValuePair *)b;
    return strcmp(pair1->key, pair2->key); // Sort by keys alphabetically
}

int kvs_write(size_t num_pairs, char keys[][MAX_STRING_SIZE], char values[][MAX_STRING_SIZE]) {
  if (kvs_table == NULL) {
    fprintf(stderr, "KVS state must be initialized\n");
    return 1;
  }

  char pairs[num_pairs][2][MAX_STRING_SIZE];
  for (size_t i = 0; i < num_pairs; i++) {
      strcpy(pairs[i][0], keys[i]);
      strcpy(pairs[i][1], values[i]);
  }
  qsort(pairs, num_pairs, sizeof(pairs[0]), compare_pairs);

  // Write the sorted pairs
  pthread_rwlock_rdlock(&kvs_table->globalLock);
  for (size_t i = 0; i < num_pairs; i++) {
      if (write_pair(kvs_table, pairs[i][0], pairs[i][1]) != 0) {
          fprintf(stderr, "Failed to write keypair (%s,%s)\n", pairs[i][0], pairs[i][1]);
      }
  }
  return 0;
}

int compare_keys(const void *a, const void *b) {
    const char *key1 = (const char *)a;
    const char *key2 = (const char *)b;
    return strcmp(key1, key2);
}

int kvs_read(size_t num_pairs, char keys[][MAX_STRING_SIZE], int fdOut) {
  if (kvs_table == NULL) {
    fprintf(stderr, "KVS state must be initialized\n");
    return 1;
  }

  qsort(keys, num_pairs, MAX_STRING_SIZE, compare_keys);

  write(fdOut, "[", 1);
  for (size_t i = 0; i < num_pairs; i++) {
    char buffer[MAX_WRITE_SIZE];
    char* result = read_pair(kvs_table, keys[i]);
    if (result == NULL) {
      snprintf(buffer, sizeof(buffer), "(%s,KVSERROR)", keys[i]);
    } else {
      snprintf(buffer, sizeof(buffer), "(%s,%s)", keys[i], result);
    }
    write(fdOut, buffer, strlen(buffer));
    free(result);
  }
  write(fdOut, "]\n", 2);
  return 0;
}

int kvs_delete(size_t num_pairs, char keys[][MAX_STRING_SIZE], int fdOut) {
  if (kvs_table == NULL) {
    fprintf(stderr, "KVS state must be initialized\n");
    return 1;
  }
  int aux = 0;

  for (size_t i = 0; i < num_pairs; i++) {
    if (delete_pair(kvs_table, keys[i]) != 0) {
      if (!aux) {
        write(fdOut, "[", 1);
        aux = 1;
      }
      char buffer[MAX_WRITE_SIZE];
      snprintf(buffer, sizeof(buffer), "(%s,KVSMISSING)", keys[i]);
      write(fdOut, buffer, strlen(buffer));
    }
  }
  if (aux) {
    write(fdOut, "]\n", 2);
  }
  return 0;
}

void kvs_show(int fdOut) {
  char buffer[MAX_WRITE_SIZE];
  for (int i = 0; i < TABLE_SIZE; i++) {
    KeyNode *keyNode = kvs_table->table[i];
    while (keyNode != NULL) {
      snprintf(buffer,sizeof(buffer),"(%s, %s)\n", keyNode->key, keyNode->value);
      write(fdOut, buffer, strlen(buffer));
      keyNode = keyNode->next; // Move to the next node
    }
  }
}

int kvs_backup(int fdBck) {
  if (kvs_table == NULL) {
    fprintf(stderr, "KVS state must be initialized\n");
    close(fdBck);
    return 1;
  }
  kvs_show(fdBck);
  close(fdBck);
  return 0;
}

void kvs_wait(unsigned int delay_ms) {
  struct timespec delay = delay_to_timespec(delay_ms);
  nanosleep(&delay, NULL);
}