#ifndef CLIENT_H
#define CLIENT_H

typedef struct SubscriptionsKeyNode {
    char *key;
    struct SubscriptionsKeyNode *next;
} SubscriptionsKeyNode;

struct Client {
    int fdReq, fdResp, fdNotif;
    SubscriptionsKeyNode *subscriptions;
};

#endif