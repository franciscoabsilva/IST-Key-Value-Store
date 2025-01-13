#ifndef KEY_VALUE_STORE_H
#define KEY_VALUE_STORE_H
#define TABLE_SIZE 26

#include <stddef.h>
#include <pthread.h>

typedef struct Subscriber {
	int fdNotifPipe;
	struct Subscriber *next;
} Subscriber;

typedef struct KeyNode {
	char *key;
	char *value;
	struct KeyNode *next;
	Subscriber *subscriber;
} KeyNode;

typedef struct HashTable {
	KeyNode *table[TABLE_SIZE];
	pthread_rwlock_t *bucketLocks;
} HashTable;

/// Creates a new KVS hash table.
/// @return Newly created hash table, NULL on failure
struct HashTable *create_hash_table();

int hash(const char *key);

// Writes a key value pair in the hash table.
// @param ht The hash table.
// @param key The key.
// @param value The value.
// @return 0 if successful.
int write_pair(HashTable *ht, const char *key, const char *value);

// Reads the value of a given key.
// @param ht The hash table.
// @param key The key.
// return the value if found, NULL otherwise.
char *read_pair(HashTable *ht, const char *key);

/// Deletes a pair from the table.
/// @param ht Hash table to read from.
/// @param key Key of the pair to be deleted.
/// @return 0 if the node was deleted successfully, 1 otherwise.
int delete_pair(HashTable *ht, const char *key);

/// @brief Adds a subscriber to a key.
/// @param keyNode To be subscribed.
/// @param fdNotifPipe fdNotifPipe of the client subscribing.
/// @return 0 if successful, 1 if subscriber already exists, -1 if error
int add_subscriber(KeyNode *keyNode, int fdNotifPipe);

/// @brief Removes a subscriber from a key.
/// @param keyNode 
/// @param fdNotifPipe 
/// @return 0 deleted successfully, 1 subscriber not found
int remove_subscriber(KeyNode *keyNode, int fdNotifPipe);

/// @brief Notifies all subscribers of a key.
/// @param keyNode 
/// @param key 
/// @param value 
void notify_subscribers(KeyNode *keyNode, const char *key, const char *value);

/// Frees the subscribers list.
/// @param sub List of subscribers to be deleted.
void free_subscribers(Subscriber *sub);

/// Frees the hashtable.
/// @param ht Hash table to be deleted.
void free_table(HashTable *ht);


#endif  // KVS_H
