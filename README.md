# IST-KVS: Key Value Store

## Project Overview

The **IST-KVS (Key Value Store)** is a distributed system for storing and managing data as key-value pairs, implemented with client-server architecture. The project is designed to support commands for creating, reading, updating, and deleting data, while also providing features like parallel processing, non-blocking backups, and graceful termination of client connections. It ensures consistent and efficient communication between clients and the server.

---

## Features

### 1. **Key-Value Pair Management**

- Supports adding, reading, updating, and deleting key-value pairs.
- Handles hash table collisions using linked lists at each index.

### 2. **Client-Server Communication**

- Clients interact with the server through communication pipes.
- Supports multiple simultaneous client connections.
- Implements graceful disconnection of clients.

### 3. **Command Processing**

- Processes commands sent by clients and returns appropriate responses.
- Commands include writing, reading, deleting, displaying all pairs, waiting, and creating backups.

### 4. **File System Interaction**

- Processes batch commands from `.job` files and generates corresponding `.out` files.

### 5. **Non-Blocking Backups**

- Utilizes processes (`fork`) to create backups without blocking the main execution.

### 6. **Parallel Execution**

- Supports handling multiple clients and `.job` files in parallel using multithreading.

### 7. **Signal Handling**

- Handles `SIGUSR1` signals to:
  - Terminate active client connections.
  - Remove client subscriptions from the hash table.
  - Close communication pipes cleanly.

### 8. **POSIX Compliance**

- File access and manipulation implemented with POSIX system calls.

---

## How Connections and Subscriptions Work

### Client-Server Communication

Clients establish communication with the server using two named pipes:

1. **Request Pipe**: Used by the client to send commands to the server.
2. **Response Pipe**: Used by the server to send responses back to the client.

Each client creates its unique communication pipes, ensuring isolation and concurrency. Upon connecting, the client sends a subscription request to the server, registering its response pipe. The server keeps track of all active client connections in a hash table.

### Command Handling

When a client sends a command through the command pipe, the server processes it, executes the requested operation, and sends the result back to the client through the response pipe. This mechanism ensures asynchronous and non-blocking communication between clients and the server.

### Signal Handling

The server handles the `SIGUSR1` signal to manage active client connections and subscriptions gracefully:

- **Terminate Client Connections**: The server closes all active communication pipes for the clients.
- **Remove Subscriptions**: All client entries are deleted from the hash table to free resources.
- **Cleanup Communication Pipes**: Named pipes associated with the clients are unlinked and closed.

This signal handling allows the server to shut down or reset without leaving dangling connections or orphaned resources.

---

## Commands

### 1. **WRITE**

- Writes one or more key-value pairs.
- Updates values if the key already exists.
- Example:
  ```plaintext
  WRITE [(key1,value1)(key2,value2)]
  ```

### 2. **READ**

- Reads one or more values by their keys.
- Returns `KVSERROR` for missing keys.
- Example:
  ```plaintext
  READ [key1,key2]
  Output: [(key1,value1)(key2,KVSERROR)]
  ```

### 3. **DELETE**

- Deletes one or more key-value pairs.
- Returns `KVSMISSING` for non-existent keys.
- Example:
  ```plaintext
  DELETE [key1,key2]
  Output: [(key2,KVSMISSING)]
  ```

### 4. **SHOW**

- Displays all key-value pairs sorted alphabetically by key.
- Example:
  ```plaintext
  SHOW
  Output: [(Akey,ValueA)(Bkey,ValueB)]
  ```

### 5. **WAIT**

- Introduces a delay in milliseconds.
- Example:
  ```plaintext
  WAIT 1000
  ```

### 6. **BACKUP**

- Creates a non-blocking backup of the current hash table state.
- Example:
  ```plaintext
  BACKUP
  ```

### 7. **HELP**

- Lists all supported commands and their usage.
- Example:
  ```plaintext
  HELP
  ```

---

## Project Structure

- **Server**: Implements the core hash table and handles client commands.
- **Client**: Communicates with the server to send commands and receive responses.
- **Makefile**: Automates compilation, cleaning, and running the project.
- **Input Files**: `.job` files with batch commands.
- **Output Files**: `.out` files containing results of `.job` commands.
- **Backup Files**: `.bck` files storing snapshots of the hash table.

---

## Compilation & Execution

### Requirements

- POSIX-compliant environment.
- GCC compiler.

### Steps

1. Compile the project:

   ```bash
   make
   ```

2. Run the server:

   ```bash
   ./ist-kvs-server <max-backups> 
   ```

   - `<max-backups>`: Maximum concurrent backups.

3. Run a client:

   ```bash
   ./ist-kvs-client <server-pipe>
   ```

   - `<server-pipe>`: Path to the communication pipe for sending commands to the server.

4. Clean build files:

   ```bash
   make clean
   ```

---

## Example Usage

### Input: `/jobs/test.job`

```plaintext
# Read a non-existent key
READ [key1]

# Write two key-value pairs
WRITE [(key1,value1)(key2,value2)]

# Read an existing key
READ [key2]

# Update an existing key and add a new one
WRITE [(key2,value3)(key3,value3)]

# Display all key-value pairs
SHOW

# Delete a key-value pair
DELETE [key1]

# Create a backup
BACKUP

# Introduce a delay
WAIT 500
```

### Output: `/output/test.out`

```plaintext
READ [key1]
[(key1,KVSERROR)]
WRITE [(key1,value1)(key2,value2)]
READ [key2]
[(key2,value2)]
WRITE [(key2,value3)(key3,value3)]
SHOW
[(key2,value3)(key3,value3)]
DELETE [key1]
[(key1,KVSMISSING)]
BACKUP
WAIT 500
```

