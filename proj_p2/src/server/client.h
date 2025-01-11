#ifndef CLIENT_H
#define CLIENT_H

typedef struct SubscriptionsKeyNode {
    char *key;
    struct KeyNode *next;
} SubscriptionsKeyNode;

typedef struct ClientStuff {
    int fd1, fd2, fd3;
    SubscriptionsKeyNode *subscriptions;
	struct ClientStuff *next;
} ClientStuff;

typedef struct ClientList {
	struct ClientStuff *head;
	int size;
} ClientList;

#endif