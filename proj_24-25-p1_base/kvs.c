#include "kvs.h"
#include "string.h"

#include <stdlib.h>
#include <ctype.h>
#include <pthread.h>
#include <stdio.h>


// Hash function based on key initial.
// @param key Lowercase alphabetical string.
// @return hash.
// NOTE: This is not an ideal hash function, but is useful for test purposes of the project
int hash(const char *key) {
    int firstLetter = tolower(key[0]);
    if (firstLetter >= 'a' && firstLetter <= 'z') {
        return firstLetter - 'a';
    } else if (firstLetter >= '0' && firstLetter <= '9') {
        return firstLetter - '0';
    }
    return -1; // Invalid index for non-alphabetic or number strings
}


struct HashTable* create_hash_table() {
    HashTable *ht = malloc(sizeof(HashTable));
    if (!ht) return NULL;
    ht->bucketLocks = malloc(TABLE_SIZE * sizeof(pthread_rwlock_t));
    for (int i = 0; i < TABLE_SIZE; i++) {
        ht->table[i] = NULL;
        pthread_rwlock_init(&ht->bucketLocks[i], NULL);
    }
    if(pthread_rwlock_init(&ht->globalLock, NULL)){
        fprintf(stderr, "Error: Initializing global lock.\n");
    }   
    return ht;
}

int write_pair(HashTable *ht, const char *key, const char *value) {
    int index = hash(key);
    // lock the list with this index
    pthread_rwlock_rdlock(&ht->globalLock);
    pthread_rwlock_wrlock(&ht->bucketLocks[index]);
    KeyNode *keyNode = ht->table[index];

    // Search for the key node
    while (keyNode != NULL) {
        // Key node found; update the value
        if (strcmp(keyNode->key, key) == 0) {
            free(keyNode->value);
            keyNode->value = strdup(value);
            pthread_rwlock_unlock(&ht->bucketLocks[index]);
            pthread_rwlock_unlock(&ht->globalLock);
            return 0;
        }
        keyNode = keyNode->next; // Move to the next node
    }

    // Key not found, create a new key node
    keyNode = malloc(sizeof(KeyNode));
    keyNode->key = strdup(key); // Allocate memory for the key
    keyNode->value = strdup(value); // Allocate memory for the value
    keyNode->next = ht->table[index]; // Link to existing nodes
    ht->table[index] = keyNode; // Place new key node at the start of the list
    pthread_rwlock_unlock(&ht->bucketLocks[index]);
    pthread_rwlock_unlock(&ht->globalLock);
    return 0;
}

char* read_pair(HashTable *ht, const char *key) {
    int index = hash(key);
    pthread_rwlock_rdlock(&ht->globalLock);
    pthread_rwlock_rdlock(&ht->bucketLocks[index]);
    KeyNode *keyNode = ht->table[index];
    char* value;

    while (keyNode != NULL) {
        if (strcmp(keyNode->key, key) == 0) {
            value = strdup(keyNode->value);
            pthread_rwlock_unlock(&ht->bucketLocks[index]);
            pthread_rwlock_unlock(&ht->globalLock);
            return value; // Return copy of the value if found
        }
        keyNode = keyNode->next; // Move to the next node
    }
    pthread_rwlock_unlock(&ht->bucketLocks[index]);
    pthread_rwlock_unlock(&ht->globalLock);
    return NULL; // Key not found
}

int delete_pair(HashTable *ht, const char *key) {
    int index = hash(key);
    pthread_rwlock_rdlock(&ht->globalLock);
    pthread_rwlock_wrlock(&ht->bucketLocks[index]);
    KeyNode *keyNode = ht->table[index];
    KeyNode *prevNode = NULL;

    // Search for the key node
    while (keyNode != NULL) {
        if (strcmp(keyNode->key, key) == 0) {
            // Key found; delete this node
            if (prevNode == NULL) {
                // Node to delete is the first node in the list
                ht->table[index] = keyNode->next; // Update the table to point to the next node
            } else {
                // Node to delete is not the first; bypass it
                prevNode->next = keyNode->next; // Link the previous node to the next node
            }
            // Free the memory allocated for the key and value
            free(keyNode->key);
            free(keyNode->value);
            free(keyNode); // Free the key node itself
            pthread_rwlock_unlock(&ht->bucketLocks[index]);
            pthread_rwlock_unlock(&ht->globalLock);
            return 0; // Exit the function
        }
        prevNode = keyNode; // Move prevNode to current node
        keyNode = keyNode->next; // Move to the next node
    }
    
    return 1;
}

void free_table(HashTable *ht) {
    for (int i = 0; i < TABLE_SIZE; i++) {
        KeyNode *keyNode = ht->table[i];
        while (keyNode != NULL) {
            KeyNode *temp = keyNode;
            keyNode = keyNode->next;
            free(temp->key);
            free(temp->value);
            free(temp);
        }
        pthread_rwlock_destroy(&ht->bucketLocks[i]);
    }
    pthread_rwlock_destroy(&ht->globalLock);
    free(ht->bucketLocks);
    free(ht);
}