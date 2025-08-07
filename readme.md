# 🚀 Group based P2P Distributed File Sharing System

**Robust. Efficient. Fault Tolerant. A group-based peer-to-peer file sharing system with centralized tracking, chunk-level hashing, and smart piece selection logic. Built using C++ with multithreading, sockets, and custom protocols.**


![Last Commit](https://img.shields.io/github/last-commit/zenvisjr/p2p-distributed-file-sharing-system?color=purple&style=flat-square)
![C++](https://img.shields.io/badge/C%2B%2B-17-blue?style=flat-square)
![Platform](https://img.shields.io/badge/Platform-Linux-lightgrey?style=flat-square)
![Multithreading](https://img.shields.io/badge/Multithreaded-Yes-brightgreen?style=flat-square)
![Sockets](https://img.shields.io/badge/Socket-TCP%2FIP-orange?style=flat-square)
[![License](https://img.shields.io/github/license/zenvisjr/p2p-distributed-file-sharing-system?style=flat-square)](./LICENSE)

---

### 📚 Table of Contents

1. [🧩 Overview](#-overview)
2. [🚀 Features](#-features)

   * [Core Functionality](#-core-functionality)
   * [Advanced Features](#-advanced-features)
3. [🧱 System Architecture](#-system-architecture)

   * [Components](#-components)
   * [Data & Control Flow](#-data--control-flow)
4. [🛠️ Build and Run Instructions](#-build-and-run-instructions)
5. [🧾 Command Reference](#-command-reference)

   * [User Management](#-user-management)
   * [Group Management](#-group-management)
   * [File Management](#-file-management)
   * [System Control](#-system-control)
6. [📤 File Upload Workflow](#-file-upload-workflow)
7. [📥 Download File Workflow](#-download-file-workflow)
8. [📊 Load Balanced Peer-Quality-Aware Piece Selection Algorithm](#-load-balanced-peer-quality-aware-piece-selection-algorithm)
9. [📊 Testing – Chunk Assignment Analysis](#-testing--chunk-assignment-analysis)
10. [🚀 Optimization: Streaming Large Files with mmap](#-optimization-streaming-large-files-with-mmap)
11. [🌐 Multi-Tracker Sync Mechanism](#-multi-tracker-sync-mechanism)
12. [❤️ Heartbeat System – Tracker Liveness Monitoring & Failover](#-heartbeat-system--tracker-liveness-monitoring--failover)
13. [⚖️ Load Balancer](#-load-balancer)
14. [🤝 Contributing](#-contributing)
15. [👨‍💻 Author](#-author)


---



## 🧩 Overview

This project is a **Group-Based Centralized Fault Tolerant Peer-to-Peer Distributed File Sharing System**, designed and implemented in C++ from scratch. It allows users to create groups, upload files, and download shared content directly from peers — with coordination managed by centralized **trackers**.

Although file transfers occur peer-to-peer, **trackers act as the central authority** for managing users, groups, and file metadata. The system ensures data integrity using **SHA1 hashing** at both chunk and full-file levels.

---

## 🚀 Features

This system combines centralized coordination with decentralized peer-to-peer data transfer. Below are all the core and advanced features implemented:

### 🔹 Core Functionality

* **User Management**
  Create, authenticate, and manage users using tracker-side authentication.

* **Group Management**
  Users can create, join, leave, and manage groups. Only group owners can accept join requests.

* **File Upload**
  Upload files to a group. Files are split into chunks, hashed using SHA1, and made available for other peers in the group.

* **File Download**
  Download files from multiple peers concurrently using a chunk-based, multi-threaded downloader with retry logic.

* **Command-Driven Interface**
  Users interact with the system via a set of well-defined commands like `upload_file`, `download_file`, `list_groups`, `stop_share`, `logout`, etc.

---

### 🔹 Advanced Features

* **Multi-Tracker Support (Active-Active Mode)**
  All trackers run simultaneously and synchronize state changes (e.g., user creation, group joins) with each other.

* **Load Balancer**
  A custom-built load balancer assigns new clients to the tracker with the lowest load using a min-heap priority queue.

* **SHA1-Based Integrity Verification**
  All file chunks and final file hashes are verified using SHA1 to ensure end-to-end data integrity.

* **Heartbeat Mechanism**
  Clients ping their connected tracker periodically; if the tracker doesn't respond, the client automatically switches to another active tracker.

* **Retry and Fallback Mechanism**
  Failed downloads are retried up to 3 times per chunk. If a peer fails, another available peer is chosen dynamically.

* **Piece Selection Algorithm with Peer Scoring**
  Peers are scored based on performance (chunks served / time), and chunk distribution is optimized using hybrid load-aware selection.

* **Thread Pool for Chunk Upload**
  Peers use a thread pool to handle simultaneous chunk upload requests efficiently, reducing overhead and improving concurrency.

* **Memory-Mapped File I/O**
  Files are written to disk using `mmap()` to avoid loading large files entirely into memory, enabling efficient 5–10GB file handling.

* **Tracker Failover and Re-registration**
  Clients auto-detect dead trackers and re-register with another one without restarting the system.

* **File Sharing Tracker State**
  The tracker maintains per-group file metadata, peer lists for each file, and chunk availability for coordinating downloads.

---

## 🏗️ System Architecture

The system follows a **centralized coordination, decentralized transfer** model. Trackers manage metadata and group membership, while file sharing happens directly between peers.

```
         +--------------------+
         |     Load Balancer  |
         +--------------------+
                   |
   +---------------+----------------+
   |               |                |
+--------+     +--------+      +--------+
|Tracker1|<--->|Tracker2|<---> |Tracker3|  ←→ Active-Active sync
+--------+     +--------+      +--------+
     ↑              ↑               ↑
     |              |               |
+----------+   +----------+   +----------+
|  Client  |   |  Client  |   |  Client  |  ←→ Direct file sharing via chunks
+----------+   +----------+   +----------+
```

---

### 🧩 Components

#### 1. **Client**

* Connects to tracker via Load Balancer
* Registers/login, joins groups, uploads/downloads files
* Downloads file chunks from multiple peers in parallel
* Sends heartbeat to tracker and auto-switches if unresponsive

#### 2. **Tracker**

* Authenticates users and manages groups
* Maintains metadata of all shared files and active peers per group
* Handles commands like `create_group`, `upload_file`, `list_groups`, etc.
* Syncs all critical state with other trackers in real time

#### 3. **Load Balancer**

* Maintains client connection count per tracker
* Always returns the tracker with minimum active load (min-heap)
* Trackers report client connect/disconnect to keep the count updated

---

### 🔄 Data & Control Flow

#### 1. **Joining the System**

* Client contacts Load Balancer → receives least-loaded tracker IP
* Connects to tracker and authenticates/creates account

#### 2. **File Upload**

* Client uploads file to group → splits into chunks
* Tracker stores chunk metadata and peer info
* Other clients fetch chunk details from tracker before download

#### 3. **File Download**

* Client requests file → tracker replies with available peers per chunk
* Chunks downloaded in parallel from peers
* Each chunk verified using SHA1, and written to disk via `mmap`

#### 4. **Tracker Synchronization**

* State updates (user/group/file) are broadcast over TCP to all other trackers
* Ensures consistent metadata even if clients switch tracker mid-session

---


## 🛠️ Build and Run Instructions

The system consists of three main components: the **Load Balancer**, the **Tracker(s)**, and the **Client(s)**. Follow the steps below to compile and run each component.

---

### 🔧 Prerequisites

* **OpenSSL development libraries** (for SHA1 hashing):

  ```bash
  sudo apt-get install libssl-dev
  ```
* **.env configuration support**
  The project uses a `.env` file to store runtime configuration (e.g., buffer size, ports, timeouts).
  Copy the provided `.env.example` to `.env`:

  ```bash
  cp .env.example .env
  ```

  Then adjust values in `.env` as needed for your setup.

---

### ⚙️ Step 1: Environment Configuration

**Edit `.env`** to define system behavior without recompiling. Example:

```ini
# Load Balancer Config
LOAD_BALANCER_IP=127.0.0.1
LOAD_BALANCER_PORT=9000

# File Transfer
BUFFERSIZE=524288
MAX_CONNECTIONS=50
THREAD_POOL_SIZE=5
MAX_RETRIES=3

# Timeouts (ms)
RECV_TIMEOUT_MS=5000
SEND_TIMEOUT_MS=5000

# Scoring Algorithm
SCORE_ALPHA=0.20
SCORE_BETA=0.80
THRESHOLD_K=1.25

# Tracker Config
HEARTBEAT_INTERVAL_SEC=10
HEARTBEAT_TIMEOUT_SEC=5
TRACKER_RECV_TIMEOUT_MS=5000
TRACKER_SEND_TIMEOUT_MS=5000
```

> **Note:** The `.env` file is **ignored by Git** to prevent local config leaks. Only `.env.example` is committed.

---

---

### 📦 Step 2: Build All Components

Use the provided Makefile to build everything at once:

```bash
make clean
make
```


---

### 🚦 Step 3: Start Load Balancer

Then run the load balancer:

```bash
./build/load_balancer
```

It listens on port `9000` by default and selects the best tracker based on load -> `tracker with least active connections`.

---

### 🧠 Step 4: Start Multiple Trackers

Ensure `tracker_info.txt` contains all tracker entries with their IP and Port:

```txt
127.0.0.1:6960
127.0.0.1:6961
127.0.0.1:6962
```

Each tracker:

* Reads the shared `tracker_info.txt`.
* Detects its own identity based on the **line number** in the file (starting at 1).
* Automatically connects and syncs state with other trackers.

Run each tracker in a separate terminal:

```bash
./build/tracker tracker_info.txt <index>
```

Example:

```bash
./build/tracker tracker_info.txt 1
./build/tracker tracker_info.txt 2
./build/tracker tracker_info.txt 3
```
You can add more tracker entry in `tracker_info.txt` to start more tracker instances. 

---

### 👤 Step 5: Start Client(s)

Run multiple clients in different terminals. Each will connect to the load balancer to get the best tracker IP:

```bash
./build/client <IP>:<PORT>
```

Example:

```bash
./build/client 127.0.0.1:8081 tracker_info.txt
./build/client 127.0.0.1:8082 tracker_info.txt
./build/client 127.0.0.1:8083 tracker_info.txt
```
Run each of the above command in different terminal and it will start 3 clients with ports `8081`, `8082`, `8083`.
NOTE: Make sure to use different ports for each client.

The client will:

* Query the load balancer for the best tracker.
* Connect to that tracker for all file-sharing commands.

Each client listens on its own `<PORT>` for incoming P2P chunk transfers.
You can change ther `<IP>` and `<PORT>` to any valid IP and PORT.
Because we are currently running all clients on my own machine, we are using different ports for each client with same IP as localhost.

---


## 🧾 Command Reference

Below is the full list of commands supported by the system.

### 👤 User Management

#### 1. `create_user`

Creates a new user account.

```
create_user <user_id> <password>
```

#### 2. `login`

Logs in an existing user.

```
login <user_id> <password>
```

#### 3. `logout`

Logs out the currently logged-in user.

```
logout
```

---

### 👥 Group Management

#### 4. `create_group`

Creates a new group (user becomes group owner).

```
create_group <group_id>
```

#### 5. `join_group`

Sends a request to join the specified group.

```
join_group <group_id>
```

#### 6. `leave_group`

Leaves the specified group.

```
leave_group <group_id>
```

#### 7. `list_groups`

Lists all existing groups in the system.

```
list_groups
```

#### 8. `list_requests`

Lists pending join requests for a group.
**Note:** Can only be used by the group owner.

```
list_requests <group_id>
```

#### 9. `accept_request`

Accepts a join request for the group.
**Note:** Only group owner can accept.

```
accept_request <group_id> <user_id>
```

---

### 📁 File Management

#### 10. `upload_file`

Uploads a file to a group and shares it with tracker.

**Note:** We only upload metadata to the tracker and not the file itself. We tell the tracker: 

`"Hey tracker, I have this file and I want to share its metadata with you so that when other peers wants to download this file, they can get the metadata from you and download the file from me."`

```
upload_file <file_path> <group_id>
```

#### 11. `download_file`

Downloads a file from peers in the group to the specified path.

**Note:** We ask the list of peers who have uploaded the file from the tracker and not the file itself. We tell the tracker: 

`"Hey tracker, I want to download this file so can you pleaase tell me the list of peers having this file so that I can download the file from them directly in chunks."`

```
download_file <group_id> <file_name> <destination_path>
```

#### 12. `stop_share`

Stops sharing a specific file. The file will no longer be available to peers.

```
stop_share <file_name>
```

#### 13. `show_downloads`

Displays the status of all ongoing and completed downloads.

```
show_downloads
```

---

### 🛑 System Control

#### 14. `quit`

Shuts down the tracker.
**Note:** For admin use only in tracker terminal.

```
quit
```

---

## 📤 File Upload Workflow

### 🔸 What it Does

The `upload_file` command lets a user share a file in a specific group so other group members can download it. The system stores important info (called metadata) about the file but doesn't actually send the file to the tracker. Only metadata is shared — actual file transfer happens peer-to-peer later.

---

### 🧑‍💻 What Happens on the Client Side

1. **User Runs the Upload Command**
   The user runs a command with the file path and the group name where the file should be shared.

2. **File Checks**
   The client first makes sure:

   * The file path is valid.
   * The file is a regular file (not a folder or link).
   * The group name is properly formatted.

3. **Metadata Creation**
   The client collects all important details about the file:

   * File name and size
   * How many chunks it will be split into
   * SHA1 hash of every chunk (to detect corruption later)
   * A final hash of the whole file (built by combining all chunk hashes)

4. **Send Metadata to Tracker**
   Once all the data is ready, the client sends this metadata to the tracker and waits for confirmation.

---

### 🗂️ What Happens on the Tracker Side

1. **Access Checks**
   The tracker confirms:

   * The user is logged in
   * The group exists
   * The user is a member of that group

2. **Duplicate Check**

   * If the same file is already shared by this user in the group → reject
   * If the file is already in the group but shared by others → add this user to the list of peers who have it
   * If it’s a new file → save the metadata and mark this user as its sharer

3. **Inform Other Trackers**
   If there are multiple trackers running, this tracker tells the others about the new file so they all stay in sync.

4. **Send Confirmation**
   The tracker finally replies to the client saying the file was shared successfully.

---

### 🧠 Why This Matters

* Files are never uploaded to a central server — only metadata is shared.
* Real files are downloaded directly from other peers who have them.
* This makes the system fast, scalable, and decentralized.
* It also avoids duplicate storage and ensures file integrity during download later.

---

## 🧠 Load Balanced Peer-Quality-Aware Piece Selection Algorithm


When a file is being downloaded from multiple peers, the system must decide **which peer will serve which chunk** of the file. Your system uses a **smart assignment algorithm** that considers:

* Peer performance (score)
* Load balancing (avoiding overloading one peer)
* Fairness (spreading chunks across peers)
* Penalty for already assigned chunks

---

### 🔁 High-Level Process

For a file with `N` chunks and `P` available peers:

1. **Assign every chunk to one peer only.**
2. **Prefer faster peers** (higher `score`) but avoid giving them too many chunks.
3. **Cap the maximum chunks** a peer can be assigned using a threshold.
4. Each chunk is assigned one-by-one using a **scoring formula**.

---

### 🧮 Step-by-Step Breakdown

#### 1. 📊 Calculate Per-Peer Threshold

To ensure fair load balancing:

* Each peer is allowed to download up to:

  ```
  threshold = ceil((totalChunks / number of peers) * K)
  ```
* The multiplier `K` (e.g., 1.25) allows a buffer so that slightly better peers can take more load.

---

#### 2. 🧠 For Each Chunk (0 to N-1), Choose the Best Peer

For every chunk, loop through all peers to find the most suitable one:

##### ✅ Conditions checked for each peer:

* **Check load**: If this peer has already reached the `threshold`, skip it.
* **Compute score** using:

  ```
  finalScore = α × (peer’s score) − β × (current load / threshold)
  ```

Where:

* α and β are weights you define.
* A peer’s **score** increases with successful downloads and fast response times.
* A **penalty** is applied based on how many chunks this peer has already been assigned.

---

#### 3. 🏁 Choose the Peer with the Highest Final Score

* Whichever peer gets the **highest finalScore** is chosen for this chunk.
* The chunk is then assigned to that peer.
* That peer’s chunk count is incremented.
* Repeat for the next chunk.

---

### 🔐 Why This Algorithm Works Well

| ✅ Design Goal   | How It's Achieved                                                          |
| --------------- | -------------------------------------------------------------------------- |
| Fairness        | Every peer has a max cap using the threshold                               |
| Performance     | High-scoring (fast) peers are prioritized                                  |
| Load Balancing  | Chunks are spread based on dynamic peer performance                        |
| Flexibility     | Parameters α and β can be tuned for aggressive vs. balanced behavior       |
| Fault Tolerance | If a peer is slow or faulty, its score drops and future chunks are avoided |

---

### 📊 Testing – Chunk Assignment Analysis

You can test the chunk assignment algorithm using the `test.cpp` file inside the `test/` directory.

#### 🧪 How to Run:

Provide the following arguments:


```
./build/test
```

---

### ✅ What It Shows

Once the algorithm runs, it prints detailed analytics to help understand:

* **Fairness** in chunk distribution
* **Score vs load** correlation
* **Impact of α and β** tuning
* **Chunk assignment trends**

---

### 📊 Output Summary Metrics

The following statistics are computed and printed:

* **Total Chunks Assigned**
* **Max, Min, and Average Chunks Per Peer**
* **Peer Distribution by Chunk Count**

```plaintext
=== Results for α=0.20, β=0.60 ===
Summary: Total=20, Max=6, Min=2, Avg=4.00

📊 Chunk Distribution:
2 peers got 2 chunks
1 peer got 4 chunks
2 peers got 6 chunks
```

---

### 📈 Score-wise Peer Distribution

Peers are grouped based on their **rounded score**, showing how many fall into each score group.

```plaintext
📈 Score-wise Peer Distribution:
0.80 - 2 peers
0.65 - 1 peer
0.42 - 2 peers
```

This helps visualize **swarm diversity and peer quality spread**.

---

### 📈 Score → Chunk Count Mapping

Finally, it maps **score bins to chunk counts**, i.e., how many total chunks were given to peers of a certain score.

```plaintext
📈 Score → Chunk Count:
0.80 score peer → 8 chunks
0.65 score peer → 4 chunks
0.42 score peer → 8 chunks
```

This reveals whether **higher score actually led to more work** — and how much β penalized overused peers.


This comprehensive test output helps fine-tune α/β values for optimal real-world performance.

`You can choose values according to your needs.`

---

## 📥 Download File Workflow

The `download_file` command enables a user to fetch a file from other peers who have shared it in a group. The file is downloaded **in parallel chunks**, verified for integrity, and stored at the given path.

We’ll break it down into these key subtopics:

---

### 1️⃣ File Lookup and Metadata Retrieval (Client ↔ Tracker)

* **User runs**:

  ```
  download_file <group_id> <file_name> <destination_path>
  ```

* **Client checks**:

  * The user is logged in.
  * The user is a member of the specified group.

* **Client sends request to Tracker** with group ID and file name.

* **Tracker responds with**:

  * File metadata: total chunks, file size, chunk hashes, final hash.
  * List of peers currently sharing the file.
  * Score and IP\:Port of each peer.

✅ Tracker-side access control ensures only authorized users can download.

---

### 2️⃣ Piece Selection Algorithm: Intelligent Chunk Assignment

* The system runs a **custom chunk assignment algorithm** that distributes chunks intelligently across available peers.

* It uses a **utility formula**:

  ```
  utility = α × peer_score - β × load_penalty
  ```

  * `peer_score`: Based on past performance (chunks served / time taken)
  * `load_penalty`: How many chunks are already assigned to that peer
  * `threshold`: Max chunks a peer should handle (`K × totalChunks / peers`)

* This ensures:

  * Fast peers are slightly favored.
  * No peer is overloaded.
  * Idle peers get a chance to contribute.
  * The whole system remains balanced and fair.

📊 The final assignment is printed to show who downloads what.

---

### 3️⃣ Parallel Chunk Download (Client ↔ Peers) – *One Connection per Peer*

* The client launches **one thread per assigned peer**.

* Each thread:

  * Establishes **a single TCP connection** to its assigned peer.
  * Sends a **list of chunk indices** it wants from that peer.
  * Stays connected for the entire session.
  * **Sequentially downloads all assigned chunks** from that peer over the same connection.

* For each chunk:

  * The peer sends:

    * The chunk data.
    * The corresponding SHA1 hash.
  * The client:

    * Verifies the chunk hash.
    * If valid, writes the chunk to the correct location in the output buffer.

* ✅ If hash matches → chunk is accepted and saved to memory.

* ❌ If hash mismatch → triggers retry mechanism.

✅ This **reduces socket overhead** and **speeds up downloads**, especially when:

* The number of chunks per peer is large.
* The file is large (multi-GB).
* Network latency is high.

❌ If any chunk fails (timeout, bad hash):

* That **specific chunk is re-assigned** to another peer using the retry mechanism.



---

### 4️⃣ Retry and Timeout Mechanism

* If:

  * Peer is **slow to respond**
  * Connection fails
  * Chunk hash is invalid

→ The chunk download is **retried up to 3 times**.

This ensures **resilience** against bad or malicious peers and unstable networks.

---

### 6️⃣ Final Hash Verification and File Write

After all chunks are downloaded, stored at destination location and individually verified:

#### 🧪 What Happens:

* Client computes the **SHA1 hash of the entire final file**.
* This is compared with the **original file hash provided by the tracker** (which was computed and stored during upload).

#### 🟢 If Hash Matches:

* ✅ The file is marked as successfully downloaded.
* ✅ A success message is logged.

#### 🔴 If Hash Mismatch:

* ❌ Indicates possible corruption (e.g., undetected chunk tampering, silent disk error).
* ❌ The partially written file is discarded.
* ❌ An error is logged with the expected and actual hash for debugging.

---

This **end-to-end hash verification** ensures full file integrity — even if all chunks passed individual checks. It acts as the final safety net before accepting the download.

---

### 7️⃣ Post-Processing and Status

* Download status is updated in the local tracker.
* `show_downloads` command reflects:

  * ✅ Completed files
  * 🔄 Ongoing downloads
  * ❌ Failed downloads

---

## 🚀 Optimization: Streaming Large Files with `mmap`

To support high-performance file sharing — especially for **large files (e.g., multi-GB)** — our system avoids loading entire files into memory. Instead, it uses **memory-mapped file I/O (`mmap`)**, which maps disk files directly into virtual memory.

### 🧠 What is `mmap`?

`mmap` allows you to **access files like arrays in memory**, without manually reading or writing them using `read()`/`write()` calls.

Behind the scenes, the OS handles paging and loading chunks of the file into memory on demand — making it ideal for large-scale, sequential access.

---

### ⚙️ How We Use `mmap` in Our System

#### ✅ On Upload (Sending File to Peers):

* The uploader **memory-maps the file to be uploaded**.
* It reads each chunk **directly from the mapped memory**, one by one.
* Each chunk’s **SHA1 hash is calculated** and sent along with the data.
* No need to load the entire file — only small mapped pages are touched.
* Saves time, avoids memory spikes, and scales well with large files.

#### ✅ On Download (Storing File Locally):

* Once all chunks are downloaded and verified,
* We **pre-allocate the final file size on disk**.
* Then we **memory-map the output file**.
* Each verified chunk is written **directly into the mapped memory** at the correct offset.
* This enables **direct random-access writes**, avoiding file pointer seeks or file fragmentation.

---

### 💡 Why `mmap` is a Big Win

| Benefit                   | Description                                                                  |
| ------------------------- | ---------------------------------------------------------------------------- |
| **Memory Efficient**      | Only required parts are loaded into memory, not the whole file.              |
| **Faster Access**         | OS-level optimizations like caching and paging make reads/writes faster.     |
| **Direct Chunk Access**   | We can write to exact offsets without manually seeking file pointers.        |
| **Cleaner Code**          | Treats files like arrays — no need for explicit `read()` or `write()` logic. |
| **Ideal for Chunked I/O** | Perfect fit for systems that deal with files in pieces (like ours).          |

---

### 🛡️ Edge Handling

We ensure:

* `mmap` regions are properly unmapped after use to prevent memory leaks.
* File descriptors are closed after operations.
* Proper permission flags are used for read/write mapping.

---

This optimization makes our system **production-grade** when handling real-world files — especially important when streaming movies, ISO images, or software installers across peers.


---

## 🌐 Multi-Tracker Sync Mechanism

Our system uses **multiple trackers running in Active-Active mode** to avoid a single point of failure. To ensure **all trackers remain in sync**, we implemented a **real-time state synchronization mechanism**.

This allows clients to connect to *any* tracker, while all trackers maintain a **consistent global view** of:

* Users
* Groups
* File metadata
* Peer lists

---

### 🧩 System Design

* Each tracker reads its identity and peers from a config file (e.g., `tracker_info.txt`)
* When a client connects, it may hit **any one tracker** via the **Load Balancer**
* The connected tracker becomes the **coordinator** for that operation
* After processing the command, it **broadcasts the state change** to all other trackers using dedicated TCP connections

---

### 🔄 Sync Protocol (Step-by-Step)

#### 1. **Tracker receives a command from client**

* e.g., `create_user ayush 1234`

#### 2. **Tracker updates its own local state**

* Adds user to its in-memory `users` map

#### 3. **Tracker prepares a sync message**

* Format: `"SYNC:CREATE_USER:ayush:1234"`

#### 4. **Broadcast to all other trackers**

* Each tracker runs a persistent TCP connection to every peer
* Sync messages are sent over these channels

#### 5. **Other trackers parse and apply**

* The receiving tracker checks command type (`CREATE_USER`)
* It updates its own in-memory state accordingly

---

### 🧠 Design Notes

* Sync messages are **idempotent** → applying them multiple times doesn't break state
* A failed sync to one tracker **does not block** the rest — this improves fault tolerance
* A failed tracker is **unrecoverable** so it cant go online again and sync its state with other trackers (`future feature`)

---

### 📦 Result of sync

Now a client connected to tracker A can upload file and a client connected to tracker B can download the file as tracker B also knows how has uploaded the file.

---
Here’s a complete, plain-English breakdown of your **Heartbeat Mechanism** that monitors tracker health and ensures seamless failover:

---

## ❤️ Heartbeat System – Tracker Liveness Monitoring & Failover

Your system runs **multiple trackers**. To make sure clients don’t talk to a dead one, we’ve added a **Heartbeat Monitor** that:

1. Regularly checks if the connected tracker is still alive
2. Notifies the Load Balancer if it’s dead
3. Automatically switches to another healthy tracker if needed

---

### 🧠 Why This Is Needed

In a distributed P2P system:

* If the tracker goes down, clients can't upload/download files.
* Waiting for manual restart is not acceptable in production.
* This heartbeat system ensures high **availability** and **fault tolerance**.

---

## 🔄 How the Heartbeat System Works

### 1️⃣ Start Heartbeat Monitor on Client Side

As soon as a client connects to a tracker:

A **background thread** is launched that keeps monitoring that specific tracker connection.

---

### 2️⃣ Ping Every 10 Seconds

* Every 10 seconds:

  * Client sends a `PING` message to the tracker over the same socket
  * Tracker should reply with `PONG`

* If `PONG` is received:

  * Tracker is healthy, continue monitoring

---

### 3️⃣ Detect Tracker Failure

If `PONG` is **not received within 5 seconds**, the client assumes the tracker is **down**:

This immediately marks the current tracker as dead.

---

### 4️⃣ Notify Load Balancer

The client then opens a **new TCP connection to the Load Balancer** and sends:

```plaintext
REMOVE <trackerIP> <trackerPort>
```

This tells the Load Balancer to remove the dead tracker from its pool.

✅ The Load Balancer replies with:

```plaintext
REMOVED
```

---

### 5️⃣ Reconnect to Another Healthy Tracker

In the main command loop, every time the user types a command:

If we detect that tracker is dead, we try to reconnect to another **healthy tracker using load balancer**.

If successful:

* Old socket is closed
* New socket replaces it
* Heartbeat monitoring restarts
* User can continue working without interruption

---

## ⚙️ Flow Diagram

```
[Client Connected to Tracker A]
         |
         v
   Heartbeat Monitor Thread
         |
         |---> Send PING every 10 sec
         |<--- Expect PONG within 5 sec
         |
     ❌ If timeout
         |
         v
 Notify Load Balancer (REMOVE)
         |
         v
 Get another tracker from LB
         |
         v
 Reconnect + Restart Heartbeat
```

---

## 🧪 Fail-Safe Behavior

* All of this runs in the background → user experience is uninterrupted
* If **all trackers** are dead, the system prints a clean message and exits gracefully:

```cpp
"❌ All trackers are down. Please restart the system manually."
```

---

## 🔐 Robustness Summary

| Component             | Purpose                            |
| --------------------- | ---------------------------------- |
| `PING`/`PONG` loop    | Liveness detection                 |
| 5s timeout            | Failure detection threshold        |
| Load Balancer notify  | Remove dead tracker                |
| Auto reconnect        | Maintain availability              |
| Global `trackerAlive` | Shared flag for command loop check |

---

✅ This heartbeat system makes your tracker architecture **highly available**, **self-healing**, and production-grade.

---

## ⚖️ Load Balancer

The Load Balancer is like a **traffic cop** in your distributed P2P file-sharing system.
It ensures:

1. Clients connect to the **least loaded tracker** (one with fewer active clients)
2. If a tracker dies, it's **removed from the list**
3. Tracks which tracker is serving how many clients
4. Keeps the system **balanced, scalable, and resilient**

---

## 🧠 Core Concepts

* **Tracker** = A central node that manages user/group/file metadata.
* **ClientCount** = How many clients are currently connected to that tracker.
* **Min-Heap** = A priority queue that always gives us the tracker with the least clients first.

---

## 📦 Tracker Registration

### When a tracker starts:

It sends this command to the Load Balancer:

```
REGISTER <ip> <port>
```

The Load Balancer then:

* Adds this tracker to its internal list
* Initializes its client count to 0
* Pushes it into a **min-heap** that keeps trackers sorted by client load

This allows the Load Balancer to always know which tracker is least busy.

---

## 📤 Giving Best Tracker to Clients

### When a client starts:

It connects to the Load Balancer and asks for the best tracker:

```
GET_TRACKER
```

The Load Balancer looks into its **min-heap** and returns the IP and port of the **least loaded tracker**.

If no trackers are available, it sends back `NO_TRACKERS`.

---

## 📈 INCREMENT and DECREMENT

Every time a client connects to or disconnects from a tracker, that client tells the Load Balancer:

```
INCREMENT <ip> <port>   → for new connection
DECREMENT <ip> <port>   → for disconnection
```

The Load Balancer updates that tracker's `clientCount` and **rebuilds the min-heap** so the order of "least loaded" is always correct.

This ensures that next time a client asks for a tracker, the Load Balancer gives the most optimal one.

---

## 🪦 Handling Tracker Failure (REMOVE)

If a tracker **dies** (e.g. heartbeat failed), the client that detected this sends:

```
REMOVE <ip> <port>
```

The Load Balancer then:

* Removes the dead tracker from its internal map
* Rebuilds the heap without that tracker
* Sends back `REMOVED` as confirmation

So, dead trackers are automatically dropped — no manual cleanup needed.

---

## 🧵 Concurrent Connections

Every incoming connection (whether from tracker or client) is handled in a **separate thread**, so multiple clients or trackers can interact with the Load Balancer simultaneously — no blocking.

---

## 📊 Summary Table

| Command       | From    | What It Does                        |
| ------------- | ------- | ----------------------------------- |
| `REGISTER`    | Tracker | Adds tracker to pool with 0 clients |
| `GET_TRACKER` | Client  | Sends best (least loaded) tracker   |
| `INCREMENT`   | Client  | Increases load count for tracker    |
| `DECREMENT`   | Client  | Decreases load count for tracker    |
| `REMOVE`      | Client  | Removes dead/unresponsive tracker   |

---

## 🔁 Why This Design Works Well

* ✅ **Efficient Load Balancing** via min-heap
* ✅ **High Availability** with failover support
* ✅ **Real-Time Tracking** of active clients per tracker
* ✅ **Thread-safe** using mutex locking
* ✅ **Decoupled Control Plane** — doesn't interfere with actual file sharing

---

## 🤝 Contributing

1. Fork the repo  
2. Create your feature branch (`git checkout -b feature/<feature-name>`)  
3. Commit your changes (`git commit -am 'Add <feature-name>'`)  
4. Push to the branch (`git push origin feature/<feature-name>`)  
5. Create a Pull Request  

---

💬 Need feature ideas? Add:

- Partial file sharing (download only selected chunks)
- Tracker fault tolerance with automatic data sync
- Dynamic tracker registration via API
- Persistent client session resumption
- Smart retry logic with exponential backoff
- Web dashboard to monitor file and peer status
- Peer reputation scoring & blacklist mechanism
- Efficient duplicate detection using Bloom Filters
- Compression support during file transfer
- TLS support for secure connections


---

## 👨‍💻 Author

**Ayush Rai**

📧 Email: [ayushrai.cse@gmail.com](mailto:ayushrai.cse@gmail.com)









