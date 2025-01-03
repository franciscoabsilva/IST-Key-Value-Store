#ifndef CLIENT_API_H
#define CLIENT_API_H

#include <stddef.h>
#include "src/common/constants.h"

/// Connects to a kvs server.
/// @param req_pipe_path Path to the name pipe to be created for requests.
/// @param resp_pipe_path Path to the name pipe to be created for responses.
/// @param server_pipe_path Path to the name pipe where the server is listening.
/// @param notif_pipe_path Path to the name pipe for notifications.
/// @param notif_pipe File descriptor for the name pipe for notifications.
/// @param req_pipe File descriptor for the name pipe for requests.
/// @param resp_pipe File descriptor for the name pipe for responses.
/// @return 0 if the connection was established successfully, 1 otherwise.
int kvs_connect(char const* req_pipe_path, char const* resp_pipe_path,
                char const* server_pipe_path, char const* notif_pipe_path,
                int* fdNotificationPipe, int* fdRequestPipe, int* fdResponsePipe,
                int* fdServerPipe);
                
/// Disconnects from an KVS server.
/// @return 0 in case of success, 1 otherwise.
int kvs_disconnect(int fdRequestPipe, const char* req_pipe_path,
                   int fdResponsePipe, const char* resp_pipe_path,
                   int fdNotification, const char* notif_pipe_path,
                   int fdServerPipe);

/// Requests a subscription for a key
/// @param key Key to be subscribed
/// @return 1 if the key was subscribed successfully (key existing), 0 otherwise.

int kvs_subscribe(const char* key);

/// Remove a subscription for a key
/// @param key Key to be unsubscribed
/// @return 0 if the key was unsubscribed successfully  (subscription existed and was removed), 1 otherwise.

int kvs_unsubscribe(const char* key);
 
#endif  // CLIENT_API_H
