#include "kvs.h"
#include "string.h"

#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#include "src/common/io.h"
#include "src/common/constants.h"

// Hash function based on key initial.
// @param key Lowercase alphabetical string.
// @return hash.
// NOTE: This is not an ideal hash function, but is useful for test purposes of
// the project
int hash(const char *key) {
	int firstLetter = tolower(key[0]);
	if (firstLetter >= 'a' && firstLetter <= 'z') {
		return firstLetter - 'a';
	} else if (firstLetter >= '0' && firstLetter <= '9') {
		return firstLetter - '0';
	}
	return -1; // Invalid index for non-alphabetic or number strings
}

struct HashTable *create_hash_table() {
	HashTable *ht = malloc(sizeof(HashTable));
	if (!ht) return NULL;
	ht->bucketLocks = malloc(TABLE_SIZE * sizeof(pthread_rwlock_t));
	if (!ht->bucketLocks) {
		fprintf(stderr, "Error: Allocating bucket locks.\n");
		free(ht);
		return NULL;
	}
	for (int i = 0; i < TABLE_SIZE; i++) {
		ht->table[i] = NULL;
		if (pthread_rwlock_init(&ht->bucketLocks[i], NULL)) {
			fprintf(stderr, "Error: Initializing bucket lock.\n");
			return NULL;
		}
	}
	return ht;
}

int write_pair(HashTable *ht, const char *key, const char *value) {
	int index = hash(key);
	KeyNode *keyNode = ht->table[index];

	// Search for the key node
	while (keyNode != NULL) {
		// Key node found; update the value
		if (strcmp(keyNode->key, key) == 0) {
			free(keyNode->value);
			keyNode->value = strdup(value);
			notify_subscribers(keyNode, key, value);
			return 0;
		}
		keyNode = keyNode->next; // Move to the next node
	}

	// Key not found, create a new key node
	keyNode = malloc(sizeof(KeyNode));
	if (!keyNode) {
		fprintf(stderr, "Error: Allocating key node.\n");
		return 1;
	}

	keyNode->subscriber = NULL;
	keyNode->key = strdup(key);     // Allocate memory for the key
	keyNode->value = strdup(value); // Allocate memory for the value
	if (!keyNode->key || !keyNode->value) {
		fprintf(stderr, "Error: Allocating key or value.\n");
		free(keyNode->key);
		free(keyNode->value);
		free(keyNode);
		return 1;
	}
	keyNode->next = ht->table[index]; // Link to existing nodes
	ht->table[index] = keyNode; // Place new key node at the start of the list
	return 0;
}

char *read_pair(HashTable *ht, const char *key) {
	int index = hash(key);

	KeyNode *keyNode = ht->table[index];
	char *value;

	while (keyNode != NULL) {
		if (strcmp(keyNode->key, key) == 0) {
			value = strdup(keyNode->value);
			return value; // Return copy of the value if found
		}
		keyNode = keyNode->next; // Move to the next node
	}
	return NULL; // Key not found
}

int delete_pair(HashTable *ht, const char *key) {
	int index = hash(key);

	KeyNode *keyNode = ht->table[index];
	KeyNode *prevNode = NULL;

	// Search for the key node
	while (keyNode != NULL) {
		if (strcmp(keyNode->key, key) == 0) {
			// Key found
			// Notify clients that the key is being deleted
			notify_subscribers(keyNode, key, "DELETE");
			// Delete this node
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
			free_subscribers(keyNode->subscriber);
			free(keyNode); // Free the key node itself
			return 0; // Exit the function
		}
		prevNode = keyNode; // Move prevNode to current node
		keyNode = keyNode->next; // Move to the next node
	}
	return 1;
}

int add_subscriber(KeyNode *keyNode, int fdNotifPipe) {
    if (keyNode == NULL) {
        fprintf(stderr, "Error: KeyNode is NULL.\n");
        return -1;
    }

    Subscriber *current = keyNode->subscriber;
    Subscriber *prev = NULL;

    // Traverse the list to check if the subscriber already exists
    while (current != NULL) {
        if (current->fdNotifPipe == fdNotifPipe) {
            return 1; // Subscriber already exists
        }
        prev = current;
        current = current->next;
    }

    // Allocate a new subscriber
    Subscriber *newSubscriber = malloc(sizeof(Subscriber));
    if (newSubscriber == NULL) {
        fprintf(stderr, "Error: Allocating subscriber.\n");
        return -1;
    }
    newSubscriber->fdNotifPipe = fdNotifPipe;
    newSubscriber->next = NULL;

    // Add the new subscriber to the end of the list
    if (prev == NULL) {
        // The list was empty
        keyNode->subscriber = newSubscriber;
    } else {
        prev->next = newSubscriber;
    }
    return 0;
}

int remove_subscriber(KeyNode *keyNode, int fdNotifPipe) {
    Subscriber *subscriber = keyNode->subscriber;
    Subscriber *prev = NULL;

    while (subscriber != NULL) {
        if (subscriber->fdNotifPipe == fdNotifPipe) {
            if (prev == NULL) {
                // Removing the first subscriber
                keyNode->subscriber = subscriber->next;
            } else {
                // Removing a subscriber in the middle or end
                prev->next = subscriber->next;
            }
            free(subscriber);
            return 0; // Subscriber existed and was removed
        }
        prev = subscriber;
        subscriber = subscriber->next;
    }
    return 1; // Subscription not found
}

void notify_subscribers(KeyNode *keyNode, const char *key, const char *value) {
	Subscriber *subscriber = keyNode->subscriber;
	while (subscriber != NULL) {
		if (write_all(subscriber->fdNotifPipe, key, KEY_MESSAGE_SIZE) == -1) {
			fprintf(stderr, "Failed to write key to notification pipe.\n");
		}
		if (write_all(subscriber->fdNotifPipe, value, KEY_MESSAGE_SIZE) == -1) {
			fprintf(stderr, "Failed to write value to notification pipe.\n");
		}
		subscriber = subscriber->next;
	}
}

void free_subscribers(Subscriber *sub) {
	Subscriber *temp;
	while (sub != NULL) {
		temp = sub;
		sub = sub->next;
		free(temp);
	}
}

void free_table(HashTable *ht) {
	for (int i = 0; i < TABLE_SIZE; i++) {
		KeyNode *keyNode = ht->table[i];
		while (keyNode != NULL) {
			KeyNode *temp = keyNode;
			keyNode = keyNode->next;
			free(temp->key);
			free(temp->value);
			free_subscribers(temp->subscriber);
			free(temp);
		}
		if (pthread_rwlock_destroy(&ht->bucketLocks[i])) {
			fprintf(stderr, "Error: Destroying bucket lock.\n");
		}
	}
	free(ht->bucketLocks);
	free(ht);
}