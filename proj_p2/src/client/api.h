#ifndef CLIENT_API_H
#define CLIENT_API_H

#include <stddef.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <pthread.h>

#include "src/common/constants.h"

/// Connects to a kvs server.
/// @param req_pipe_path Path to the name pipe to be created for requests.
/// @param resp_pipe_path Path to the name pipe to be created for responses.
/// @param server_pipe_path Path to the name pipe where the server is listening.
/// @param notif_pipe_path Path to the name pipe for notifications.
/// @param fdNotificationPipe File descriptor for the pipe for notifications.
/// @param fdRequestPipe File descriptor for the pipe for requests.
/// @param fdResponsePipe File descriptor for the pipe for responses.
/// @return 0 if the connection was established successfully, 1 otherwise.
int kvs_connect(char const *req_pipe_path, char const *resp_pipe_path,
				char const *server_pipe_path, char const *notif_pipe_path,
				int *fdNotificationPipe, int *fdRequestPipe, int *fdResponsePipe,
				int *fdServerPipe);

/// Disconnects from an KVS server.
/// @param fdRequestPipe FFile descriptor of the requests pipe.
/// @param req_pipe_path Path to the pipe for requests.
/// @param fdResponsePipe File descriptor of the responses pipe.
/// @param resp_pipe_path Path to the pipe for responses.
/// @param fdNotification File descriptor of the notifications pipe.
/// @param notif_pipe_path Path to the pipe for notifications.
/// @param fdServerPipe File descriptor for the pipe for the server.
/// @return 0 in case of success, 1 otherwise.
int kvs_disconnect(int fdRequestPipe, const char *req_pipe_path,
				   int fdResponsePipe, const char *resp_pipe_path,
				   int fdNotification, const char *notif_pipe_path,
				   int fdServerPipe, pthread_t notificationsThread);

/// Requests a subscription for a key
/// @param fdRequestPipe FFile descriptor of the requests pipe.
/// @param fdResponsePipe File descriptor of the responses pipe.
/// @param key Key to be subscribed
/// @return 1 if the key was subscribed successfully (key existing), 0 otherwise.
int kvs_subscribe(int fdRequestPipe, int fdResponsePipe, const char *key);

/// Remove a subscription for a key
/// @param fdRequestPipe File descriptor of the requests pipe.
/// @param fdResponsePipe File descriptor of the responses pipe.
/// @param key Key to be unsubscribed
/// @return 0 if the key was unsubscribed successfully  (subscription existed and was removed), 1 otherwise.
int kvs_unsubscribe(int fdResquestPipe, int fdResponsePipe, const char *key);

#endif  // CLIENT_API_H
