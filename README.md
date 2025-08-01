

# Peer-to-Peer Distributed File Sharing System

## Table of Contents
1. [How to Use](#how-to-use)
2. [System Architecture](#system-architecture)
3. [Commands Implemented](#commands-implemented)

## How to Use

### Compile and Run the Tracker

1. **Compile the Tracker Code**:

    ```bash
    g++ tracker.cpp -o tracker
    ```

2. **Start the Tracker**:

    ```bash
    ./tracker tracker_info.txt 1
    ```

### Compile and Run the Client

3. **Compile the Client Code**:

    ```bash
    g++ client.cpp -o client -lssl -lcrypto
    ```

4. **Start the Client**:

    ```bash
    ./client <IP>:<PORT> tracker_info.txt
    ```
**Note**: Replace <IP> and <PORT> with the IP address and port number where you want the client to listen. Running this command with different <PORT> values will create new clients listening on different ports.

## System Architecture

### Tracker-Client Communication

1. **Initialization**: The tracker starts first and listens for incoming client connections.
2. **Connection**: A client connects to the tracker using its IP and port number.
3. **Authentication**: The client must log in or create a new user to proceed.

### Multithreading

1. **Concurrency**: Both the tracker and the clients use multithreading for handling multiple operations concurrently.
2. **Client-side Threads**: Each client spawns a new thread to listen for incoming connections from other clients for file sharing.
3. **Tracker-side Threads**: The tracker spawns a new thread for each connected client to handle its commands and operations.

### Command Execution

1. **Command Input**: The client enters a command.
2. **Tracker Verification**: The tracker verifies the command and executes the corresponding function.
3. **Peer-to-Peer File Sharing**: For file uploads and downloads, the tracker facilitates the initial handshake. After that, file transfer happens directly between clients.


## COMMANDS IMPLEMENTED

### 1. `create_user`

**Description**: Creates a new user with a unique user ID and password.

**Usage**:
```
create_user <user_id> <password>
```

### 2. `login`

**Description**: Log in to the system using your user ID and password.

**Usage**:
```
login <user_id> <password>
```

### 3. `create_group`

**Description**: Creates a new group with a unique group ID.

**Usage**:
```
create_group <group_id>
```

### 4. `join_group`

**Description**: Join an existing group using its group ID.

**Usage**:
```
join_group <group_id>
```

### 5. `leave_group`

**Description**: Leave a group that you are a part of.
**Usage**:
```
leave_group <group_id>
```

### 6. `upload_file`

**Description**: Upload a file to a specific group.

**Usage**:
```
upload_file <file_path> <group_id>
```

### 7. `list_requests`

**Description**: List all pending join requests for a specific group.

**Usage**:
```
list_requests <group_id>
```

### 8. `accept_request`

**Description**: Accept a user's request to join a specific group.

**Usage**:
```
accept_request <group_id> <user_id>
```
NOTE: Only owner of the group can accept join request.

### 9. `list_groups`

**Description**: List all the groups in the network.

**Usage**:
```
list_groups
```

### 10. `download_file`

**Description**: Download a specific file from a group to a specified destination path.

**Usage**:
```
download_file <group_id> <file_name> <destination_path>
```

### 11. `show_downloads`

**Description**: Show the status of ongoing and completed downloads.

**Usage**:
```
show_downloads
```

### 12. `stop_share`

**Description**: Stops sharing a specified file across all groups and peers.

**Usage**:
```
stop_share <file_name>
```

### 13. `logout`

**Description**: Log out the current user of a particular client.

**Usage**:
```
logout
```

### 13. `quit`

**Description**: Quit the tracker.

**Usage**:
```
quit
```

