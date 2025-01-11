#ifndef CLIENT_H
#define CLIENT_H

typedef struct SubscriptionsKeyNode {
    char *key;
    struct KeyNode *next;
} SubscriptionsKeyNode;

struct Client {
    int fd1, fd2, fd3;
    SubscriptionsKeyNode *subscriptions;
	//struct ClientNode *next;
};

typedef struct ClientList {
	struct ClientNode *head;
	int size;
} ClientList;

#endif