#ifndef CLIENT_H
#define CLIENT_H

typedef struct SubscriptionsKeyNode {
    char *key;
    struct KeyNode *next;
} SubscriptionsKeyNode;

typedef struct ClientNode {
    int fd1, fd2, fd3;
    SubscriptionsKeyNode *subscriptions;
	struct ClientNode *next;
} ClientNode;

typedef struct ClientList {
	struct ClientNode *head;
	int size;
} ClientList;

#endif