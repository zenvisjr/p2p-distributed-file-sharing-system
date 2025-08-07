#include "env_utils.h"
#include <algorithm>
#include <arpa/inet.h> // Needed for inet_pton
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdio> //for snprintf
#include <cstring>
#include <fcntl.h> // For open()
#include <future>
#include <iostream>
#include <mutex>
#include <netinet/in.h>
#include <openssl/sha.h> //for SHA1 hashing
#include <queue>
#include <sstream>
#include <sys/mman.h> // For mmap(), PROT_READ, PROT_WRITE, MAP_SHARED
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "env_utils.h"

using namespace std;

// Globals or a struct you pass around:
std::string loadBalancerIP = envStr("LOAD_BALANCER_IP", "127.0.0.1");
int loadBalancerPort = envInt("LOAD_BALANCER_PORT", 9000);

int BUFFERSIZE = envInt("BUFFERSIZE", 512 * 1024);
int MAX_CONNECTIONS = envInt("MAX_CONNECTIONS", 50);
int THREAD_POOL_SIZE = envInt("THREAD_POOL_SIZE", 5);
int MAX_RETRIES = envInt("MAX_RETRIES", 3);

int RECV_TIMEOUT_MS = envInt("RECV_TIMEOUT_MS", 5000);
int SEND_TIMEOUT_MS = envInt("SEND_TIMEOUT_MS", 5000);

float SCORE_ALPHA = envFloat("SCORE_ALPHA", 0.20f);
float SCORE_BETA = envFloat("SCORE_BETA", 0.80f);
float THRESHOLD_K = envFloat("THRESHOLD_K", 1.25f);


int HEARTBEAT_INTERVAL_SEC = envInt("HEARTBEAT_INTERVAL_SEC", 10);
int HEARTBEAT_TIMEOUT_SEC = envInt("HEARTBEAT_TIMEOUT_SEC", 5);


mutex queueMutex;
mutex globalMutex; // 🌐 [Phase 3] Protect shared maps
atomic<bool> trackerAlive; // Tracker health check
int myTrackerIndex;
vector<pair<string, string>> trackerList;
queue<int> socketQueue;

condition_variable queueCV;

class FileMetadata {
public:
  int fileSize; // uint64_t is a 64-bit unsigned integer, which can represent a
                // much larger range of values
  string fileName;
  int noOfChunks;
  vector<string> hashOfChunks;
  string HashOfCompleteFile;
};

unordered_map<string, string> fnameToPath;

// 📦 [piece selection] data structure to support leecher concept
struct PeerStats {
  string ip;
  int port;
  float score = 1.0; // chunks / time
  int chunksServed = 0;
  double totalDownloadTime = 0.0;

  // ✅ Default constructor
  PeerStats() : ip(""), port(0) {}
  PeerStats(string i, int p) : ip(i), port(p) {}
};

unordered_map<string, PeerStats> peerStatsMap; // ✅ stores per-peer stats
vector<pair<string, int>> peers;               // ✅ stores available peers
// 📦 [Piece Selection]
struct ChunkAssignment {
  int chunkIndex;
  PeerStats assignedPeer;
  ChunkAssignment()
      : chunkIndex(-1), assignedPeer(PeerStats("", 0)) {} // ✅ added

  ChunkAssignment(int i, PeerStats p) : chunkIndex(i), assignedPeer(p) {}
};

vector<PeerStats> peerList; // ✅ can use this if needed for ordering
unordered_map<int, ChunkAssignment>
    assignedChunks;                 // ✅ chunk → peer assignment
unordered_set<int> completedChunks; // ✅ track downloaded chunks
// 📦 Global vector of chunk assignments
// vector<ChunkAssignment> assignedChunks;

void checkAppendSendRecieve(vector<string> &arg, string &command, string ip,
                            string port, int clientSocket);
string calculateSHA1Hash(unsigned char *buffer, int size);
string ConvertToHex(unsigned char c);
vector<unsigned char> hexStringToByteArray(string &hexString);
void FindFileMetadata(string filepath, string &command);
void print(vector<string> &str);
vector<string> tokenizePeers(string &str);
vector<string> tokenizeVector(string &str);
vector<string> ExtractArguments(string &str);

void RecieveFile(int clientSocket, string destinationPath, string fileName,
                 const vector<string> &chunkHashes,
                 const string &expectedCompleteHash);
void DownloadFromClient(int clientPort, string destinationPath, string fileName,
                        const vector<string> &chunkHashes,
                        const string &expectedCompleteHash);
void ShareToClient(int listeningSocket);
void SendFile(int socket, string fpath, string fname);
void DownloadHandler(string clientIP, string clientPort);

void fileServerThread(int port);
void handleUploadReq(int peerSocket);

bool isValidIdentifier(const string &str);
bool isStrongPassword(const string &password);

bool sendAll(int socket, const char *data, size_t totalBytes);
string DeserializePeerStats(const PeerStats &peer);
// 📦 [Piece Selection] Download chunks from a specific peer
void DownloadChunkRange(PeerStats &peer, const vector<int> &chunkIndices,
                        const vector<string> &chunkHashes,
                        const string &fileName, mutex &failedLock,
                        vector<bool> &failedChunks, char *fileMemory);

void AssignChunksToPeers(int totalChunks);
int PrepareFileForWriting(const string &filePath, size_t totalSize);
char *MapFileToMemory(int fd, size_t fileSize);
void ChunkWorkerThread();
void StartHeartbeatMonitor(int clientSocket, const string &currentTrackerIP,
                           const string &currentTrackerPort);

bool ReconnectToAnotherTracker(int &clientSocket);
bool GetBestTrackerFromLoadBalancer(string &trackerIP, int &trackerPort);
int main(int argc, char *argv[]) {
  // checking if correct no of arguments were passed
  if (argc != 2) {
    perror("Usage : ./client <IP>:<PORT>");
    exit(1);
  }

  // extracting IP and port number of client passed as 2nd argument
  string socketInfo = argv[1];
  string cLientIP, clientPort;
  for (int i = 0; i < int(socketInfo.size()); i++) {
    if (socketInfo[i] == ':') {
      clientPort = socketInfo.substr(i + 1);
      break;
    }

    cLientIP += socketInfo[i];
  }
  // cout<<IP<<endl;
  // cout<<PORT<<endl;
  // int port = stoi(PORT);

  // creating a new thread for listening for new connection from another clients
  thread clientAsServerThread(DownloadHandler, cLientIP, clientPort);
  clientAsServerThread.detach();
  sleep(1);

  // moving forward with our main logic

  // extracting IP and port number of tracker from tracker_info.txt passed as
  // 3rd argument

  // // opening the file tracker_info.txt
  // int fd = open(argv[2], O_RDONLY);
  // if (fd < 0) {
  //   perror("tracker_info.txt cant be opened due to unexpected error");
  //   exit(1);
  // }

  // // reading its content into buffer
  // char buffer[BUFFERSIZE];
  // ssize_t bytesRead;
  // string extract = "";
  // while ((bytesRead = read(fd, buffer, sizeof(buffer) - 1)) >
  //        0) // read() doesn't automatically null-terminate the buffer, so we
  //           // create space for null
  // {
  //   // adding null at end of buffer so that it is easy to work with string
  //   buffer[bytesRead] = '\0';
  //   extract += buffer;
  // }

  // if (bytesRead == -1) {
  //   perror("there was a error in reading tracker_info.txt");
  //   exit(1);
  // }

  // close(fd);
  // // cout<<extract<<endl;

  // // extracting IP and port of tracker
  // istringstream iss(extract);
  // string line;

  // while (getline(iss, line)) {
  //   if (line.empty())
  //     continue;
  //   size_t delim = line.find(':');
  //   if (delim == string::npos)
  //     continue;
  //   string ip = line.substr(0, delim);
  //   string port = line.substr(delim + 1);
  //   trackerList.emplace_back(ip, port);
  // }

  // if (trackerList.empty()) {
  //   cerr << "No valid trackers found in tracker_info.txt" << endl;
  //   exit(1);
  // }

  // // Seed the random number generator
  // srand(time(NULL));
  // myTrackerIndex = rand() % trackerList.size();

  // if (clientPort == "6001") {
  //   myTrackerIndex = 0;
  // }

  // else if (clientPort == "6002") {
  //   myTrackerIndex = 1;
  // }

  // else if (clientPort == "6003") {
  //   myTrackerIndex = 2;
  // }

  // string trackerIP = trackerList[myTrackerIndex].first;
  // string trackerPort = trackerList[myTrackerIndex].second;
  // trackerPort = stoi(trackerPort);

  // Step 1: Retry getting tracker from Load Balancer
  int attempt = 0;
  bool success = false;

  string trackerIP;
  int trackerPort;

  while (attempt < MAX_RETRIES) {
    if (GetBestTrackerFromLoadBalancer(trackerIP, trackerPort)) {
      success = true;
      break;
    } else {
      cerr << "⚠️ Failed to get tracker from LB. Retrying (" << (attempt + 1)
           << "/" << MAX_RETRIES << ")..." << endl;
      this_thread::sleep_for(chrono::seconds(1));
      attempt++;
    }
  }

  if (!success) {
    cerr << "❌ Unable to get tracker from Load Balancer after " << MAX_RETRIES
         << " attempts. Exiting." << endl;
    exit(1);
  }

  // cout << "🛰️ Connecting to best tracker: " << trackerIP << ":" << trackerPort
  //      << endl;

  // cout<<IPAddress<<endl;
  // cout<<portNumber<<endl;

  // Step 2: Create socket and connect with retry
  int clientSocket;
  attempt = 0;
  success = false;

  while (attempt < MAX_RETRIES) {
    // Create a new socket on client side
    int domain = AF_INET;
    int type = SOCK_STREAM;
    int protocol = 0;
    clientSocket = socket(domain, type, protocol);
    // cout<<"client socket: "<<clientSocket<<endl;
    if (clientSocket == -1) {
      perror("unable to create a socket at client side");
      exit(1);
    }
    // setting options for the serverSocket
    int option = 1;
    int setOption =
        setsockopt(clientSocket, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT,
                   &option, sizeof(option));
    if (setOption == -1) {
      perror("unable to create a socket at server side");
      exit(1);
    }

    // Define the server's address structure
    int trackerPortNumber = trackerPort;
    // int trackerPortNumber = 9000;

    sockaddr_in serverAddress;
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(trackerPortNumber);
    inet_pton(AF_INET, trackerIP.c_str(),
              &serverAddress.sin_addr); // Convert IP address to byte form

    // Connect to the server
    int connectFD = connect(clientSocket, (struct sockaddr *)&serverAddress,
                            sizeof(serverAddress));
    if (connectFD == -1) {
      cerr << "⚠️ Failed to connect to tracker " << trackerIP << ":"
           << trackerPort << " (Attempt " << (attempt + 1) << "/" << MAX_RETRIES
           << ")" << endl;
      close(clientSocket);
      this_thread::sleep_for(chrono::seconds(1));
      attempt++;
    } else {
      success = true;
      cout << "Client is connected with tracker on " << trackerIP << ":"
           << trackerPortNumber << endl;
      break;
    }
  }
  if (!success) {
    cerr << "❌ Unable to connect to tracker after " << MAX_RETRIES
         << " attempts. Exiting." << endl;
    exit(1);
  }

  // cout << "Client is listening for request on " << cLientIP << ":"
  //      << clientPort << endl;
  trackerAlive = true;
  // thread hb(StartHeartbeatMonitor, clientSocket, trackerIP,
  //           to_string(trackerPort));
  // hb.detach();
  // Let it run in background
  if (clientPort == "6001") {
    cout << "Client is running on port 6001" << endl;
    vector<string> arguments;
    string command;

    // Alice creates user and logs in
    command = "create_user alice password";
    arguments = ExtractArguments(command);
    checkAppendSendRecieve(arguments, command, cLientIP, clientPort,
                           clientSocket);

    command = "login alice password";
    arguments = ExtractArguments(command);
    checkAppendSendRecieve(arguments, command, cLientIP, clientPort,
                           clientSocket);

    // Alice creates group g1
    command = "create_group g1";
    arguments = ExtractArguments(command);
    checkAppendSendRecieve(arguments, command, cLientIP, clientPort,
                           clientSocket);

    sleep(7); // Give time for bob and charlie to send join requests

    // Alice accepts bob's request
    command = "accept_request g1 bob";
    arguments = ExtractArguments(command);
    checkAppendSendRecieve(arguments, command, cLientIP, clientPort,
                           clientSocket);

    // Alice accepts charlie's request
    command = "accept_request g1 charlie";
    arguments = ExtractArguments(command);
    checkAppendSendRecieve(arguments, command, cLientIP, clientPort,
                           clientSocket);

    // command = "accept_request g1 dave";
    // arguments = ExtractArguments(command);
    // checkAppendSendRecieve(arguments, command, cLientIP, clientPort,
    //                        clientSocket);

    // command = "accept_request g1 eve";
    // arguments = ExtractArguments(command);
    // checkAppendSendRecieve(arguments, command, cLientIP, clientPort,
    //                        clientSocket);

    // Alice uploads file to group
    command = "upload_file hi.pdf g1";
    // command = "upload_file yo.mkv g1";
    arguments = ExtractArguments(command);
    checkAppendSendRecieve(arguments, command, cLientIP, clientPort,
                           clientSocket);

  } else if (clientPort == "6002") {
    cout << "Client is running on port 6002" << endl;
    vector<string> arguments;
    string command;

    // Bob creates user and logs in
    command = "create_user bob password";
    arguments = ExtractArguments(command);
    checkAppendSendRecieve(arguments, command, cLientIP, clientPort,
                           clientSocket);

    command = "login bob password";
    arguments = ExtractArguments(command);
    checkAppendSendRecieve(arguments, command, cLientIP, clientPort,
                           clientSocket);

    // Bob sends join request to group g1
    command = "join_group g1";
    arguments = ExtractArguments(command);
    checkAppendSendRecieve(arguments, command, cLientIP, clientPort,
                           clientSocket);
    sleep(6);
    command = "upload_file hi.pdf g1";
    arguments = ExtractArguments(command);
    checkAppendSendRecieve(arguments, command, cLientIP, clientPort,
                           clientSocket);

  } else if (clientPort == "6003") {
    cout << "Client is running on port 6003" << endl;
    vector<string> arguments;
    string command;

    // Charlie creates user and logs in
    command = "create_user charlie password";
    arguments = ExtractArguments(command);
    checkAppendSendRecieve(arguments, command, cLientIP, clientPort,
                           clientSocket);

    command = "login charlie password";
    arguments = ExtractArguments(command);
    checkAppendSendRecieve(arguments, command, cLientIP, clientPort,
                           clientSocket);

    // Charlie sends join request
    command = "join_group g1";
    arguments = ExtractArguments(command);
    checkAppendSendRecieve(arguments, command, cLientIP, clientPort,
                           clientSocket);

  sleep(6); // Wait for alice to accept and upload

  // Charlie downloads file
  command = "download_file g1 hi.pdf /home/zenvis/yo";
  arguments = ExtractArguments(command);
  checkAppendSendRecieve(arguments, command, cLientIP, clientPort,
                         clientSocket);
  }
  // } else if (clientPort == "6004") {
  //   cout << "Client is running on port 6004" << endl;
  //   vector<string> arguments;
  //   string command;

  //   // Dave creates user and logs in
  //   command = "create_user dave password";
  //   arguments = ExtractArguments(command);
  //   checkAppendSendRecieve(arguments, command, cLientIP, clientPort,
  //                          clientSocket);

  //   command = "login dave password";
  //   arguments = ExtractArguments(command);
  //   checkAppendSendRecieve(arguments, command, cLientIP, clientPort,
  //                          clientSocket);

  //   // Dave sends join request
  //   command = "join_group g1";
  //   arguments = ExtractArguments(command);
  //   checkAppendSendRecieve(arguments, command, cLientIP, clientPort,
  //                          clientSocket);
  // } else if (clientPort == "6005") {
  //   cout << "Client is running on port 6005" << endl;
  //   vector<string> arguments;
  //   string command;

  //   // Eve creates user and logs in
  //   command = "create_user eve password";
  //   arguments = ExtractArguments(command);
  //   checkAppendSendRecieve(arguments, command, cLientIP, clientPort,
  //                          clientSocket);

  //   command = "login eve password";
  //   arguments = ExtractArguments(command);
  //   checkAppendSendRecieve(arguments, command, cLientIP, clientPort,
  //                          clientSocket);

  //   // Eve sends join request
  //   command = "join_group g1";
  //   arguments = ExtractArguments(command);
  //   checkAppendSendRecieve(arguments, command, cLientIP, clientPort,
  //                          clientSocket);
  // }

  while (true) {
    // cout << "\n" << endl;
    cout << "Enter commands:> ";

    string command;
    // cout << "🔍 Main thread: About to call getline" << endl;
    getline(cin, command);
    // cout << "🔍 Main thread: Received command: " << command << endl;

    if (command == "") {
      cout << "Command cannot be empty" << endl;
      continue;
    }
    // 🔽 Put this BEFORE sending to tracker
    if (!trackerAlive) {
      cout << "Trying to reconnect to another tracker..." << endl;
      if (!ReconnectToAnotherTracker(clientSocket)) {
        cerr << "❌ All trackers are down. Please restart the system manually."
             << endl;
        exit(1);
      }
    }

    vector<string> arguments = ExtractArguments(command);
    // cout<<"tracker socket: "<<clientSocket<<endl;
    // cout << "🔍 Main thread: About to call checkAppendSendRecieve" << endl;
    checkAppendSendRecieve(arguments, command, cLientIP, clientPort,
                           clientSocket);
    // cout << "🔍 Main thread: Finished checkAppendSendRecieve" << endl;
  }

  close(clientSocket);
  return 0;
}

void checkAppendSendRecieve(vector<string> &arg, string &command, string ip,
                            string port, int clientSocket) {
  // int sizeOfArgs = arg.size();
  char bufferRecv[BUFFERSIZE];
  // int type;
  // socklen_t len = sizeof(type);
  // if (getsockopt(clientSocket, SOL_SOCKET, SO_TYPE, &type, &len) == -1) {
  //   perror("getsockopt at entry to checkAppendSendRecieve");
  // } else {
  //   std::cerr << "[DEBUG] FD at entry to checkAppendSendRecieve = "
  //             << clientSocket << ", SO_TYPE=" << type
  //             << " (1 means SOCK_STREAM)" << std::endl;
  // }

  if (arg[0] == "create_user") {
    if (arg.size() != 3) {
      cout << "USAGE: create_user <user_id> <passwd>" << endl;
      return;
    }
    //  command = arg[0] + " " + arg[1] + " " + arg[2];

    // Validating user_id and password
    string userId = arg[1];
    string password = arg[2];
    if (!isValidIdentifier(userId)) {
      cout << "Error: Invalid user_id. Only [a-zA-Z0-9_.-] allowed, max 20 "
              "characters."
           << endl;
      return;
    }
    // if (!isStrongPassword(password))
    // {
    //     cout << "Error: Password must be at least 8 characters long, contain
    //     uppercase, lowercase, digit, and special character." << endl; return;
    // }
    if (send(clientSocket, command.c_str(), command.length(), 0) == -1) {
      perror("there was a error in sending command to server");

      // if (getsockopt(clientSocket, SOL_SOCKET, SO_TYPE, &type, &len) == -1) {
      //   perror("getsockopt before send");
      // }
      return;
    }

    // char bufferRecv[512];
    int bytesRecieved =
        recv(clientSocket, bufferRecv, sizeof(bufferRecv) - 1, 0);
    // cout<<"after recv"<<endl;
    if (bytesRecieved < 0) {
      perror("unable to recieve data from tracker");
    }
    bufferRecv[bytesRecieved] =
        '\0'; // Null-terminating the received acknowledge

    // Safely converting char array to string
    string recvAck(bufferRecv, bytesRecieved);

    // cout<<"hii"<<endl;
    cout << recvAck << endl;
  } else if (arg[0] == "login") {
    if (arg.size() != 3) {
      cout << "USAGE: login <user_id> <passwd>" << endl;
      return;
    }

    // Validating user_id and password
    string userId = arg[1];
    string password = arg[2];

    if (!isValidIdentifier(userId)) {
      cout << "Error: Invalid user_id. Only [a-zA-Z0-9_.-] allowed, max 20 "
              "characters."
           << endl;
      return;
    }
    // if (!isStrongPassword(password))
    // {
    //     cout << "Error: Password must be at least 8 characters long, contain
    //     uppercase, lowercase, digit, and special character." << endl; return;
    // }

    command = arg[0] + " " + arg[1] + " " + arg[2] + " " + ip + " " + port;
    if (send(clientSocket, command.c_str(), command.length(), 0) == -1) {
      perror("there was a error in sending command to server");
      return;
    }

    // char bufferRecv[512];
    int bytesRecieved =
        recv(clientSocket, bufferRecv, sizeof(bufferRecv) - 1, 0);
    if (bytesRecieved < 0) {
      perror("unable to recieve data from tracker");
    }
    bufferRecv[bytesRecieved] = '\0';
    string recvAck(bufferRecv, bytesRecieved);
    cout << recvAck << endl;
  } else if (arg[0] == "logout") {
    if (arg.size() != 1) {
      cout << "USAGE: logout" << endl;
    } else {
      // command = arg[0];
      if (send(clientSocket, command.c_str(), command.length(), 0) == -1) {
        perror("there was a error in sending command to server");
        return;
      }

      // char bufferRecv[512];
      int bytesRecieved =
          recv(clientSocket, bufferRecv, sizeof(bufferRecv) - 1, 0);
      if (bytesRecieved < 0) {
        perror("unable to recieve data from tracker");
      }
      bufferRecv[bytesRecieved] = '\0';
      string recvAck(bufferRecv, bytesRecieved);
      cout << recvAck << endl;
    }
  } else if (arg[0] == "create_group") {
    if (arg.size() != 2) {
      cout << "USAGE: create_group <group_id>" << endl;
      return;
    }

    // Validating group_id
    string groupId = arg[1];
    if (!isValidIdentifier(groupId)) {
      cout << "Error: Invalid group_id. Only [a-zA-Z0-9_.-] allowed, max 20 "
              "characters."
           << endl;
      return;
    }

    // command = arg[0];
    if (send(clientSocket, command.c_str(), command.length(), 0) == -1) {
      perror("there was a error in sending command to server");
      return;
    }

    // char bufferRecv[512];
    int bytesRecieved =
        recv(clientSocket, bufferRecv, sizeof(bufferRecv) - 1, 0);
    if (bytesRecieved < 0) {
      perror("unable to recieve data from tracker");
    }
    bufferRecv[bytesRecieved] = '\0';
    string recvAck(bufferRecv, bytesRecieved);
    cout << recvAck << endl;
  } else if (arg[0] == "join_group") {
    if (arg.size() != 2) {
      cout << "USAGE: join_group <group_id>" << endl;
      return;
    }
    string groupId = arg[1];

    if (!isValidIdentifier(groupId)) {
      cout << "Error: Invalid group_id. Only [a-zA-Z0-9_.-] allowed, max 20 "
              "characters."
           << endl;
      return;
    }

    // command = arg[0];
    if (send(clientSocket, command.c_str(), command.length(), 0) == -1) {
      perror("there was a error in sending command to server");
      return;
    }

    // char bufferRecv[512];
    int bytesRecieved =
        recv(clientSocket, bufferRecv, sizeof(bufferRecv) - 1, 0);
    if (bytesRecieved < 0) {
      perror("unable to recieve data from tracker");
    }
    bufferRecv[bytesRecieved] = '\0';
    string recvAck(bufferRecv, bytesRecieved);
    cout << recvAck << endl;
  } else if (arg[0] == "leave_group") {
    if (arg.size() != 2) {
      cout << "USAGE: leave_group <group_id>" << endl;
      return;
    }

    string groupId = arg[1];
    if (!isValidIdentifier(groupId)) {
      cout << "Error: Invalid group_id. Only [a-zA-Z0-9_.-] allowed, max 20 "
              "characters."
           << endl;
      return;
    }

    // command = arg[0];
    if (send(clientSocket, command.c_str(), command.length(), 0) == -1) {
      perror("there was a error in sending command to server");
      return;
    }

    // char bufferRecv[512];
    int bytesRecieved =
        recv(clientSocket, bufferRecv, sizeof(bufferRecv) - 1, 0);
    if (bytesRecieved < 0) {
      perror("unable to recieve data from tracker");
    }
    bufferRecv[bytesRecieved] = '\0';
    string recvAck(bufferRecv, bytesRecieved);
    cout << recvAck << endl;
  } else if (arg[0] == "list_requests") {
    if (arg.size() != 2) {
      cout << "USAGE: list_requests <group_id>" << endl;
      return;
    }
    string groupId = arg[1];
    if (!isValidIdentifier(groupId)) {
      cout << "Error: Invalid group_id. Only [a-zA-Z0-9_.-] allowed, max 20 "
              "characters."
           << endl;
      return;
    }
    // command = arg[0];
    if (send(clientSocket, command.c_str(), command.length(), 0) == -1) {
      perror("there was a error in sending command to server");
      return;
    }

    // char bufferRecv[512];
    int bytesRecieved =
        recv(clientSocket, bufferRecv, sizeof(bufferRecv) - 1, 0);
    if (bytesRecieved < 0) {
      perror("unable to recieve data from tracker");
    }
    bufferRecv[bytesRecieved] = '\0';
    // cout << "Received " << bytesRecieved << " bytes: " << bufferRecv << endl;
    string recvAck(bufferRecv, bytesRecieved);

    // checking if 1st char of string is #
    if (recvAck[0] != ' ') {
      cout << recvAck << endl;
    } else {
      // cout<<"Entering else"<<endl;
      string pendingList = recvAck;
      // cout<<pendingList<<endl;
      vector<string> pending = tokenizeVector(pendingList);
      for (auto list : pending) {
        cout << list << endl;
      }
      // int noOfGroups = stoi(recvAck.substr(1));
    }
  } else if (arg[0] == "accept_request") {
    if (arg.size() != 3) {
      cout << "USAGE: accept_request <group_id> <user_id>" << endl;
      return;
    }

    string groupId = arg[1];
    string userId = arg[2];

    if (!isValidIdentifier(groupId) || !isValidIdentifier(userId)) {
      cout << "Error: Invalid group_id or user_id. Only [a-zA-Z0-9_.-] "
              "allowed, max 20 characters."
           << endl;
      return;
    }
    // command = arg[0];
    if (send(clientSocket, command.c_str(), command.length(), 0) == -1) {
      perror("there was a error in sending command to server");
      return;
    }

    // char bufferRecv[512];
    int bytesRecieved =
        recv(clientSocket, bufferRecv, sizeof(bufferRecv) - 1, 0);
    if (bytesRecieved < 0) {
      perror("unable to recieve data from tracker");
    }
    bufferRecv[bytesRecieved] = '\0';
    string recvAck(bufferRecv, bytesRecieved);
    cout << recvAck << endl;
  } else if (arg[0] == "list_groups") {
    if (arg.size() > 1) {
      cout << "USAGE: list_groups" << endl;
    } else {
      // command = arg[0];
      if (send(clientSocket, command.c_str(), command.length(), 0) == -1) {
        perror("there was a error in sending command to server");
        return;
      }

      // char bufferRecv[512];
      int bytesRecieved =
          recv(clientSocket, bufferRecv, sizeof(bufferRecv) - 1, 0);
      if (bytesRecieved < 0) {
        perror("unable to recieve data from tracker");
      }
      bufferRecv[bytesRecieved] = '\0';
      // cout << "Received " << bytesRecieved << " bytes: " << bufferRecv <<
      // endl;
      string recvAck(bufferRecv, bytesRecieved);

      // checking if 1st char of string is #
      if (recvAck[0] != ' ') {
        cout << recvAck << endl;
      } else {
        // cout<<"Entering else"<<endl;
        string groupName = recvAck;
        // cout<<groupName<<endl;
        vector<string> groups = tokenizeVector(groupName);
        for (auto gr : groups) {
          cout << gr << endl;
        }
        // int noOfGroups = stoi(recvAck.substr(1));
      }
    }
  }

  else if (arg[0] == "upload_file") {
    // FileMetadata metadata;
    if (arg.size() != 3) {
      cout << "upload_file <file_path> <group_id>" << endl;
      return;
    }
    string filePath = arg[1];
    string groupId = arg[2];

    // check if path is valid path of file
    struct stat fileStat;
    if (stat(filePath.c_str(), &fileStat) == -1) {
      perror("cannot access the given file: INVALID FILE PATH");
      return;
    }

    if (!S_ISREG(fileStat.st_mode)) {
      perror("Invalid file: file is not a regular file");
      return;
    }

    if (!isValidIdentifier(groupId)) {
      cout << "Error: Invalid group_id. Only [a-zA-Z0-9_.-] allowed, max 20 "
              "characters."
           << endl;
      return;
    }
    // check if file exists

    // final required metadata of file and append it in command
    FindFileMetadata(filePath, command);

    // command = arg[0];
    cout << "file metadata prepared for upload: " << command << endl;
    std::size_t len = command.size();
    string header = "@LARGE@";
    send(clientSocket, header.c_str(), header.size(), 0);
    send(clientSocket, &len, sizeof(len), 0);
    if (!sendAll(clientSocket, command.c_str(), command.length())) {
      perror("Failed to send full metadata to tracker");
      return;
    }
    int bytesRecieved =
        recv(clientSocket, bufferRecv, sizeof(bufferRecv) - 1, 0);
    if (bytesRecieved < 0) {
      perror("unable to recieve data from tracker");
    }
    bufferRecv[bytesRecieved] = '\0';
    // cout << "Received " << bytesRecieved << " bytes: " << bufferRecv << endl;
    string recvAck(bufferRecv, bytesRecieved);

    // checking if 1st char of string is #
    cout << recvAck << endl;
  } else if (arg[0] == "list_files") {
    if (arg.size() != 2) {
      cout << "USAGE: list_files <group_id>" << endl;
      return;
    }

    string groupId = arg[1];
    if (!isValidIdentifier(groupId)) {
      cout << "Error: Invalid group_id. Only [a-zA-Z0-9_.-] allowed, max 20 "
              "characters."
           << endl;
      return;
    }
    // command = arg[0];
    if (send(clientSocket, command.c_str(), command.length(), 0) == -1) {
      perror("there was a error in sending command to server");
      return;
    }

    // char bufferRecv[512];
    int bytesRecieved =
        recv(clientSocket, bufferRecv, sizeof(bufferRecv) - 1, 0);
    if (bytesRecieved < 0) {
      perror("unable to recieve data from tracker");
    }
    bufferRecv[bytesRecieved] = '\0';
    // cout << "Received " << bytesRecieved << " bytes: " << bufferRecv << endl;
    string recvAck(bufferRecv, bytesRecieved);

    // cout<<recvAck<<endl;
    // checking if 1st char of string is #
    if (recvAck[0] != ' ') {
      cout << recvAck << endl;
    } else {
      // cout<<"Entering else"<<endl;
      string fileName = recvAck;
      // cout<<fileName<<endl;
      vector<string> files = tokenizeVector(fileName);
      for (auto f : files) {
        cout << f << endl;
      }
      // int noOfGroups = stoi(recvAck.substr(1));
    }
  } else if (arg[0] == "stop_share") {
    if (arg.size() != 3) {
      cout << "USAGE: stop_share <group_id> <file_name>" << endl;
      return;
    }

    string groupId = arg[1];
    string fileName = arg[2];

    if (!isValidIdentifier(groupId) || !isValidIdentifier(fileName)) {
      cout << "Error: Invalid group_id or file_name. Only [a-zA-Z0-9_.-] "
              "allowed, max 20 characters."
           << endl;
      return;
    }
    if (send(clientSocket, command.c_str(), command.length(), 0) == -1) {
      perror("there was a error in sending command to server");
      return;
    }

    // char bufferRecv[512];
    int bytesRecieved =
        recv(clientSocket, bufferRecv, sizeof(bufferRecv) - 1, 0);
    if (bytesRecieved < 0) {
      perror("unable to recieve data from tracker");
    }
    bufferRecv[bytesRecieved] = '\0';
    // cout << "Received " << bytesRecieved << " bytes: " << bufferRecv << endl;
    string recvAck(bufferRecv, bytesRecieved);

    cout << recvAck << endl;
  } else if (arg[0] == "download_file") {
    // ✅ Validate argument count
    if (arg.size() != 4) {
      cout << "USAGE: download_file <group_id> <file_name> <destination_path>"
           << endl;
      return;
    }

    string groupId = arg[1];
    string fileName = arg[2];
    string destinationPath = arg[3];

    // ✅ Validate group_id and file_name
    if (!isValidIdentifier(groupId) || !isValidIdentifier(fileName)) {
      cout << "Error: Invalid group_id or file_name. Only [a-zA-Z0-9_.-] "
              "allowed, max 20 characters."
           << endl;
      return;
    }

    // ✅ Validate destination path
    struct stat st;
    if (stat(destinationPath.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
      perror("Invalid destination path");
      return;
    }

    // ✅ Send command to tracker
    if (send(clientSocket, command.c_str(), command.length(), 0) == -1) {
      perror("Error sending command to server");
      return;
    }

    // ✅ Step 1: Receive metadata size
    int metadataLen;
    int n = recv(clientSocket, &metadataLen, sizeof(int), 0);
    if (n <= 0) {
      perror("Error receiving metadata length");
      return;
    }
    cout << "Metadata size to receive: " << metadataLen << " bytes" << endl;

    // ✅ Step 2: Receive full metadata content
    string recvAck;
    int totalReceived = 0;
    while (totalReceived < metadataLen) {
      char buffer[512];
      int bytes =
          recv(clientSocket, buffer, min(512, metadataLen - totalReceived), 0);
      if (bytes <= 0) {
        perror("Error receiving metadata content");
        return;
      }
      recvAck.append(buffer, bytes);
      totalReceived += bytes;
    }

    // ✅ Parse response
    vector<string> tokens = tokenizeVector(recvAck);
    print(tokens);
    // cout << "1st token: " << tokens[0] << endl;

    if (tokens[0] != "OK") {
      //   cout << recvAck << endl;
      return;
    }

    cout << "File metadata received successfully." << endl;

    // ✅ Extract metadata
    int fileSize = stoi(tokens[1]);
    int noOfChunks = stoi(tokens[2]);
    string completeFileHash = tokens[3];

    cout << "File Size: " << fileSize << endl;
    cout << "No of Chunks: " << noOfChunks << endl;

    // ✅ Extract chunk hashes
    vector<string> chunkHashes;
    for (int i = 4; i < 4 + noOfChunks; ++i) {
      chunkHashes.push_back(tokens[i]);
      cout << "Chunk " << (i - 4) << " Hash: " << tokens[i] << endl;
    }

    // ✅ Extract peers

    // vector<pair<string, int>> peers;
    for (int i = 4 + noOfChunks; i < int(tokens.size()); i += 5) {
      string peerID = tokens[i];
      int peerPort = stoi(tokens[i + 1]);
      float score = stof(tokens[i + 2]);
      int served = stoi(tokens[i + 3]);
      double time = stod(tokens[i + 4]);
      PeerStats peer(peerID, peerPort); // userID used as IP
      peer.score = score;
      peer.chunksServed = served;
      peer.totalDownloadTime = time;

      string key = peerID + ":" + to_string(peerPort);
      peerStatsMap[key] = peer;

      peers.push_back({peerID, peerPort});
    }

    cout << "Peers with file:" << endl;
    for (auto &p : peers)
      cout << p.first << " : " << p.second << endl;

    // ✅ Prepare chunk assignments
    int totalChunks = noOfChunks;
    // int peerCount = peers.size();
    // vector<string> receivedChunkHashes(totalChunks); // Stores per-chunk
    // SHA1s

    // basic round robin
    // TODO: implement piece selection
    // for (int i = 0; i < totalChunks; ++i) {
    //   string ip = peers[i % peerCount].first;
    //   int port = peers[i % peerCount].second;
    //   PeerStats peer(ip, port);
    //   chunkAssignments[i] = ChunkAssignment(i, peer);
    // }

    // implementing Peer-Quality-Aware Selection
    // unordered_map<string, PeerStats> peerStatsMap;

    // for (auto &[ip, port] : peers) {
    //   string key = ip + ":" + to_string(port);
    //   peerStatsMap[key] = PeerStats(ip, port);
    // }

    // for (int i = 0; i < totalChunks; ++i) {
    //   PeerStats *bestPeer = nullptr;
    //   double bestScore = -1;

    //   for (auto &[key, peer] : peerStatsMap) {
    //     double score = (peer.totalDownloadTime > 0)
    //                        ? (double)peer.chunksServed /
    //                        peer.totalDownloadTime : 1.0; // default score if
    //                        new peer
    //     if (!bestPeer || score > bestScore) {
    //       bestPeer = &peer;
    //       bestScore = score;
    //     }
    //   }

    //   if (bestPeer) {
    //     assignedChunks[i] = ChunkAssignment(i, *bestPeer);
    //     cout << "Chunk " << i << " assigned to " << bestPeer->ip << ":"
    //          << bestPeer->port << " (score: " << bestScore << ")" << endl;
    //   }
    // }

    // implementing Peer-Quality-Aware Selection with hybrid utility based chunk
    // assignment along with fallback appraoch
    AssignChunksToPeers(noOfChunks);

    string fullPath = destinationPath + "/" + fileName;
    int destinationFileFd = PrepareFileForWriting(fullPath, fileSize);
    if (destinationFileFd < 0) {
      return;
    }

    char *fileMemory = MapFileToMemory(destinationFileFd, fileSize);
    if (!fileMemory) {
      close(destinationFileFd);
      return;
    }

    // ✅ Download chunks

    unordered_map<string, vector<int>> peerToChunks;
    for (const auto &[chunkIndex, assignment] : assignedChunks) {
      string peerKey = assignment.assignedPeer.ip + ":" +
                       to_string(assignment.assignedPeer.port);
      peerToChunks[peerKey].push_back(chunkIndex);
    }

    for (const auto &[peerKey, chunkIndices] : peerToChunks) {
      cout << "Downloading chunks from " << peerKey << endl;
      for (int chunkIndex : chunkIndices) {
        cout << "chunk number " << chunkIndex << endl;
      }
    }

    // vector<string> downloadedChunks(totalChunks);
    // vector<bool> isChunkDone(totalChunks, false);
    vector<bool> failedChunks;
    mutex failedLock;
    vector<thread> threads;

    // for (const auto &entry : assignedChunks) {
    //   int chunkIndex = entry.first;
    //   //   PeerStats peer = entry.second.assignedPeer;
    //   // 🔑 Get the key to access peer from peerStatsMap for reference update
    //   string peerKey = entry.second.assignedPeer.ip + ":" +
    //                    to_string(entry.second.assignedPeer.port);
    //   PeerStats &peer = peerStatsMap[peerKey];
    //   threads.emplace_back(DownloadChunkRange, ref(peer),
    //                        vector<int>{chunkIndex}, ref(chunkHashes),
    //                        ref(destinationPath), ref(fileName),
    //                        ref(writeLock), ref(isChunkDone),
    //                        ref(receivedChunkHashes), fileMemory);
    // }

    // ✅ Spawn one thread per peer
    for (const auto &[peerKey, chunkIndices] : peerToChunks) {
      PeerStats &peer = peerStatsMap[peerKey];
      threads.emplace_back(DownloadChunkRange, ref(peer), chunkIndices,
                           ref(chunkHashes), ref(fileName), ref(failedLock),
                           ref(failedChunks), fileMemory);
    }

    for (auto &t : threads) {
      t.join();
    }

    // ✅ Reassemble file
    // string fullPath = destinationPath + "/" + fileName;
    // ofstream out(fullPath, ios::binary);
    // for (int i = 0; i < totalChunks; ++i) {
    //   if (!isChunkDone[i]) {
    //     cerr << "❌ Missing chunk " << i << " — download failed!" << endl;
    //     return;
    //   }
    //   out << downloadedChunks[i];
    // }
    // out.close();

    // checking if all chunks are received
    // bool allChunksReceived = true;
    for (int i = 0; i < int(failedChunks.size()); ++i) {
      if (!failedChunks[i]) {
        cerr << "❌ Chunk " << i << " was not downloaded successfully.\n";
        // allChunksReceived = false;
      }
    }
    if (failedChunks.size() > 0) {
      cerr << "🚫 Download failed. Incomplete file. Aborting.\n";
      munmap(fileMemory, fileSize);
      close(destinationFileFd);
      return;
    }

    // string downloadedFileHash;
    // // ✅ Calculate complete file hash
    // for (int i = 0; i < totalChunks; ++i) {
    //   downloadedFileHash += receivedChunkHashes[i];
    // }
    // vector<unsigned char> concatenatedByteArray =
    //     hexStringToByteArray(downloadedFileHash);
    // downloadedFileHash = calculateSHA1Hash(concatenatedByteArray.data(),
    //                                        concatenatedByteArray.size());

    string finalHash = calculateSHA1Hash((unsigned char *)fileMemory, fileSize);
    cout << "Final combined hash: " << finalHash << endl;
    cout << "Expected complete file hash: " << completeFileHash << endl;
    cout << "Downloaded file hash: " << finalHash << endl;

    if (finalHash != completeFileHash) {
      cerr << "❌ Final file hash mismatch! File may be corrupted." << endl;
      unlink(fullPath.c_str());
      return;
    } else {
      cout << "✅ Final file hash verified successfully." << endl;
    }

    // munmap and close
    munmap(fileMemory, fileSize);
    close(destinationFileFd);

    // 🧠 Step 1: Combine all peer stats into one string
    string combinedStats;
    int statCount = 0;

    for (auto &[key, peer] : peerStatsMap) {
      if (peer.chunksServed > 0) { // ✅ only send if this peer helped
        combinedStats += DeserializePeerStats(peer); // Already ends with '\n'
        statCount++;

        cout << "📤 Will send stats to tracker for " << key
             << " → Score: " << peer.score << ", Served: " << peer.chunksServed
             << ", Time: " << peer.totalDownloadTime << endl;
      }
    }
    cout << "Combined stats: " << combinedStats << endl;

    if (statCount > 0) {
      // 🧠 Step 2: Send length of the combinedStats first
      uint32_t len = static_cast<uint32_t>(combinedStats.size()); // 100% safe
      cout << "Combined stats length: " << len << endl;
      uint32_t netLen = htonl(len);
      cout << "Combined stats length in network byte order: " << netLen << endl;

      if (send(clientSocket, &netLen, sizeof(netLen), 0) < 0) {
        perror("❌ Failed to send peer stats length to tracker");
        return;
      }

      // 🧠 Step 3: Send the combinedStats payload
      int totalSent = 0;
      while (totalSent < int(len)) {
        int sent = send(clientSocket, combinedStats.c_str() + totalSent,
                        len - totalSent, 0);
        if (sent <= 0) {
          perror("❌ Failed to send combined peer stats to tracker");
          return;
        }
        totalSent += sent;
      }
      cout << "Combined stats sent successfully, size: " << totalSent << endl;
    }

    // ✅ Step 4: Send Download completion signal
    // string response = "DownloadCompleteSuccessfully\n";
    // if (send(clientSocket, response.c_str(), response.size(), 0) < 0) {
    //   perror("❌ Error sending download status to tracker");
    //   return;
    // }
  } else if (arg[0] == "show_downloads") {
    if (arg.size() != 1) {
      cout << "USAGE: show_downloads" << endl;
    } else {
      if (send(clientSocket, command.c_str(), command.length(), 0) == -1) {
        perror("there was a error in sending command to server");
        return;
      }

      // char bufferRecv[512];
      int bytesRecieved =
          recv(clientSocket, bufferRecv, sizeof(bufferRecv) - 1, 0);
      if (bytesRecieved < 0) {
        perror("unable to recieve data from tracker");
      }
      bufferRecv[bytesRecieved] = '\0';
      // cout << "Received " << bytesRecieved << " bytes: " << bufferRecv <<
      // endl;
      string recvAck(bufferRecv, bytesRecieved);
      vector<string> downloadInfo = tokenizeVector(recvAck);
      print(downloadInfo);
      // cout<<recvAck<<endl;
    }
  } else if (arg[0] == "quit") {
    if (arg.size() != 1) {
      cout << "USAGE: quit" << endl;
    } else {
      if (send(clientSocket, command.c_str(), command.length(), 0) == -1) {
        perror("there was a error in sending command to server");
        return;
      }

      int bytesRecieved =
          recv(clientSocket, bufferRecv, sizeof(bufferRecv) - 1, 0);
      if (bytesRecieved < 0) {
        perror("unable to recieve data from tracker");
      }
      bufferRecv[bytesRecieved] = '\0';
      // cout << "Received " << bytesRecieved << " bytes: " << bufferRecv <<
      // endl;
      string recvAck(bufferRecv, bytesRecieved);
      cout << recvAck << endl;
    }
  } else {
    cout << "WRONG COMMAND: The command u entered does not exists" << endl;
    // string response = "The command u entered does not exists";
    // send(clientSocket, response.c_str(), response.size(), 0);
  }
}

// 📦 [Piece Selection]
void DownloadChunkRange(PeerStats &peer, const vector<int> &chunkIndices,
                        const vector<string> &chunkHashes,
                        const string &fileName, mutex &failedLock,
                        vector<bool> &failedChunks, char *fileMemory) {

  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) {
    cerr << "❌ Failed to create socket to peer " << peer.ip << ":" << peer.port
         << endl;
    return;
  }

  int option = 1;
  if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &option,
                 sizeof(option)) == -1) {
    perror("unable to create a socket at server side");
    close(sock);
    exit(1);
  }

  // ⏳ Add a 5s timeout for recv to avoid hangs
  timeval tv;
  tv.tv_sec = 5;
  tv.tv_usec = 0;
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  sockaddr_in serverAddr;
  serverAddr.sin_family = AF_INET;
  serverAddr.sin_port = htons(peer.port);
  inet_pton(AF_INET, peer.ip.c_str(), &serverAddr.sin_addr);

  if (connect(sock, (sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) {
    cerr << "❌ Failed to connect to peer " << peer.ip << ":" << peer.port
         << endl;
    close(sock);
    return;
  }

  cout << "Client is connected to peer on port and IP to download file: "
       << peer.port << " " << peer.ip << endl;

  for (int chunkIndex : chunkIndices) {

    bool success = false;

    for (int attempt = 1; attempt <= MAX_RETRIES && !success; attempt++) {
      auto start =
          chrono::high_resolution_clock::now(); // ✅ Start time per chunk

      string request =
          "send_chunk " + fileName + " " + to_string(chunkIndex) + "\n";
      int sendBytes = send(sock, request.c_str(), request.size(), 0);
      if (sendBytes != request.size()) {
        cerr << "❌ Failed to send chunk request for chunk " << chunkIndex
             << endl;
        continue; // retry
      }

      int chunkLenNetwork;
      int lenBytes = recv(sock, &chunkLenNetwork, sizeof(chunkLenNetwork), 0);
      if (lenBytes != sizeof(chunkLenNetwork)) {
        cerr << "❌ Failed to receive chunk length for chunk " << chunkIndex
             << endl;
        continue; // retry
      }
      int chunkLen = ntohl(chunkLenNetwork);

      string chunkData;
      int received = 0;
      while (received < chunkLen) {
        char buffer[BUFFERSIZE];
        int bytes = recv(sock, buffer, BUFFERSIZE, 0);
        if (bytes <= 0)
          break;
        chunkData.append(buffer, bytes);
        received += bytes;
      }

      if (received != chunkLen) {
        cerr << "❌ Incomplete chunk " << chunkIndex << " from " << peer.ip
             << endl;
        continue; // retry
      }

      string localHash = calculateSHA1Hash((unsigned char *)chunkData.c_str(),
                                           chunkData.size());
      if (localHash != chunkHashes[chunkIndex]) {
        cerr << "❌ Hash mismatch for chunk " << chunkIndex << " from "
             << peer.ip << endl;
        continue; // retry
      }

      // receivedChunkHashes[chunkIndex] = localHash;

      // {
      // lock_guard<mutex> lock(writeLock);
      // downloadedChunks[chunkIndex] = chunkData;
      // isChunkDone[chunkIndex] = true;
      cout << "✔️ Chunk " << chunkIndex << " received from " << peer.ip << ":"
           << peer.port << endl;
      // }

      // ✅ All checks passed → Write to memory-mapped file at correct offset
      size_t offset = chunkIndex * BUFFERSIZE;
      memcpy(fileMemory + offset, chunkData.data(), chunkData.size());

      auto end = chrono::high_resolution_clock::now();
      chrono::duration<double> elapsed = end - start;
      peer.totalDownloadTime += elapsed.count();
      peer.chunksServed++;

      // ✅ Set success flag to true as we successfully downloaded the file
      success = true;
    }
    if (!success) {
      cerr << "🛑 Failed to download chunk " << chunkIndex << " from "
           << peer.ip << " after " << MAX_RETRIES << " attempts\n";
      lock_guard<mutex> lk(failedLock);
      failedChunks.push_back(chunkIndex); // record for a second pass
    }
  }

  // ✅ Calculate final score after all chunks are downloaded
  peer.score = (peer.totalDownloadTime > 0)
                   ? peer.chunksServed / peer.totalDownloadTime
                   : 1.0;

  close(sock); // ✅ Close only once after all chunks are done

  cout << "📥 All chunks downloaded from peer " << peer.ip << ":" << peer.port
       << ", total served: " << peer.chunksServed
       << ", total time: " << peer.totalDownloadTime
       << ", final score: " << peer.score << endl;
}

void DownloadFromClient(int clientPort, string destinationPath, string fileName,
                        const vector<string> &chunkHashes,
                        const string &expectedCompleteHash)

{

  cout << "Download from Client" << endl;

  // Create a new socket on client side
  int clientSocket = socket(AF_INET, SOCK_STREAM, 0);
  if (clientSocket == -1) {
    perror("unable to create a socket for downloading");
    return;
  }
  // setting options for the clientSocket
  int option = 1;
  int setOption =
      setsockopt(clientSocket, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &option,
                 sizeof(option));
  if (setOption == -1) {
    perror("unable to create a socket at server side");
    // close the clientSocket if fail to set options
    close(clientSocket);
    exit(1);
  }

  // Define other client's address structure
  sockaddr_in serverAddress;
  serverAddress.sin_family = AF_INET;
  serverAddress.sin_port = htons(clientPort);
  inet_pton(AF_INET, "127.0.0.1",
            &serverAddress.sin_addr); // Convert IP address to byte form

  // Connect to the other client for downloading
  int connectFD = connect(clientSocket, (struct sockaddr *)&serverAddress,
                          sizeof(serverAddress));
  if (connectFD == -1) {
    perror("unable to connect to the other client for downloading");
    close(clientSocket); // Close the socket if connection fails
    return;
  } else {
    cout << "Client is connected to other client on port: " << clientPort
         << endl;
  }

  // updated send to handle failure
  // Send request to peer for file
  string request = fileName;

  if (send(clientSocket, request.c_str(), request.size(), 0)) {
    cout << "Request sent to peer for file: " << fileName << endl;
  } else {
    perror("Failed to send request to peer");
    shutdown(clientSocket, SHUT_RDWR); // Disables read/write on socket
    close(clientSocket);               // Releases file descriptor
    return;
  }

  // cout<<"hyyyyyyy"<<endl;
  RecieveFile(clientSocket, destinationPath, fileName, chunkHashes,
              expectedCompleteHash);
  // cout<<"sending response back to tracker"<<endl;
  string response = "DownloadCompleteSuccessfully";
  if (send(clientSocket, response.c_str(), response.size(), 0) == -1) {
    perror("Failed to send response to tracker");
  }

  // close the clientSocket after download is complete
  shutdown(clientSocket, SHUT_RDWR); // Disables read/write on socket
  close(clientSocket);               // Releases file descriptor
}

//   Function to receive data from the client
void RecieveFile(int clientSocket, string destinationPath, string fileName,
                 const vector<string> &chunkHashes,
                 const string &expectedCompleteHash) {
  string fullPath = destinationPath + "/" + fileName;

  int fd = open(fullPath.c_str(), O_WRONLY | O_CREAT, 0644);
  if (fd == -1) {
    perror("Unable to reserve space for downloaded file");
    return;
  }

  long long totalFileSize = 0;
  int bytesRead = recv(clientSocket, &totalFileSize, sizeof(totalFileSize), 0);
  cout << "Total file size to receive: " << totalFileSize << " bytes" << endl;
  if (bytesRead != sizeof(totalFileSize)) {
    perror("Failed to receive total file size");
    close(fd);
    return;
  }

  unsigned char buffer[BUFFERSIZE];
  string combinedChunkHashes;
  long long totalReceived = 0;
  int chunkIndex = 0;

  cout << "DOWNLOAD STARTED..." << endl;

  while (totalReceived < totalFileSize) {
    int bytesToRead =
        min(BUFFERSIZE, static_cast<int>(totalFileSize - totalReceived));
    int bytesReceived = 0;

    // 🛠️ Receive exactly bytesToRead in a loop
    while (bytesReceived < bytesToRead) {
      int r = recv(clientSocket, buffer + bytesReceived,
                   bytesToRead - bytesReceived, 0);
      if (r <= 0) {
        perror("Failed to receive data from sender");
        close(fd);
        return;
      }
      bytesReceived += r;
    }

    // ✅ Hash verification
    string localHash = calculateSHA1Hash(buffer, bytesReceived);
    cout << "Local hash for chunk " << chunkIndex << ": " << localHash << endl;
    if (chunkIndex >= int(chunkHashes.size())) {
      cerr << "❌ More chunks received than expected!" << endl;
      break;
    }
    if (localHash != chunkHashes[chunkIndex]) {
      cerr << "❌ Hash mismatch in chunk " << (chunkIndex + 1) << endl;
    } else {
      cout << "✔️ Chunk " << (chunkIndex + 1) << " hash verified." << endl;
    }

    combinedChunkHashes += localHash;
    chunkIndex++;

    if (write(fd, buffer, bytesReceived) == -1) {
      perror("Error writing file to disk");
      close(fd);
      return;
    }

    totalReceived += bytesReceived;
    cout << "Received " << totalReceived << " bytes out of " << totalFileSize
         << " bytes." << endl;
  }

  // ✅ Final SHA1 hash of all chunks
  vector<unsigned char> concatenatedByteArray =
      hexStringToByteArray(combinedChunkHashes);

  string finalHash = calculateSHA1Hash((concatenatedByteArray.data()),
                                       concatenatedByteArray.size());
  cout << "Final combined hash: " << finalHash << endl;
  cout << "Expected complete file hash: " << expectedCompleteHash << endl;
  if (finalHash != expectedCompleteHash) {
    cerr << "❌ Final file hash mismatch! File may be corrupted." << endl;
  } else {
    cout << "✅ Final file hash verified successfully." << endl;
  }

  close(fd);
  cout << "✅ File is downloaded and stored at: " << fullPath << endl;
}

void DownloadHandler(string clientIP, string port) {
  int clientPort = stoi(port);

  // cout<<"Entering download handler"<<endl;
  // Step 1: Create a new socket
  int listeningSocket = socket(AF_INET, SOCK_STREAM, 0);
  if (listeningSocket == -1) {
    perror("Can't create listening socket");
    exit(1);
  }

  // Step 2: Bind the socket
  sockaddr_in listeningAddress;
  listeningAddress.sin_family = AF_INET;
  listeningAddress.sin_port = htons(clientPort);
  inet_pton(AF_INET, clientIP.c_str(), &listeningAddress.sin_addr);

  if (bind(listeningSocket, (struct sockaddr *)&listeningAddress,
           sizeof(listeningAddress)) == -1) {
    perror("Can't bind listening socket");
    exit(1);
  }

  // Step 3: Put the socket in listen mode
  if (listen(listeningSocket, MAX_CONNECTIONS) == -1) {
    perror("Can't listen on listening socket");
    exit(1);
  } else {
    cout << "Client is listening for request on " << clientIP << ":"
         << clientPort << endl;
  }

  // Initialize thread pool for chunk server
  for (int i = 0; i < THREAD_POOL_SIZE; ++i) {
    thread(ChunkWorkerThread).detach();
  }
  cout << "✅ Chunk server thread pool initialized with " << THREAD_POOL_SIZE
       << " workers" << endl;

  // Step 4 and 5: Loop to accept and handle incoming connections
  while (true) {
    // cout<<"Enterinng accept phase"<<endl;
    sockaddr_in clientAddress;
    socklen_t clientSize = sizeof(clientAddress);
    int peerSocket =
        accept(listeningSocket, (struct sockaddr *)&clientAddress, &clientSize);

    if (peerSocket == -1) {
      perror("Can't accept client");
      continue; // go back to listening for the next client
    } else if (peerSocket > 0) {
      cout << "Accepted connection from other client" << endl;
    }

    // Spawn a new thread to handle this client
    // thread downloadThread(ShareToClient, peerSocket);
    // downloadThread.detach(); // detach the thread

    // using thread pool
    {
      lock_guard<mutex> lock(queueMutex);
      socketQueue.push(peerSocket);
    }
    queueCV.notify_one(); // Wake one sleeping worker
    cout << "Download thread spawned successfully" << endl;
    // break;
    // return;
  }
}

void ShareToClient(int peerSocket) {
  while (true) {
    char buffer[BUFFERSIZE];
    int bytesReceived = recv(peerSocket, buffer, sizeof(buffer) - 1, 0);

    if (bytesReceived <= 0) {
      if (bytesReceived < 0) {
        perror("Unable to read command");
      } else {
        cout << "🔌 Client has closed the connection" << endl;
      }
      break; // Exit the loop and close socket
    }

    buffer[bytesReceived] = '\0';
    string command(buffer);
    cout << "📥 Received command: " << command << endl;

    vector<string> tokens = tokenizeVector(command);
    if (tokens.size() != 3 || tokens[0] != "send_chunk") {
      cerr << "❌ Invalid command format from peer: " << command << endl;
      continue;
    }

    string fname = tokens[1];
    int chunkIndex = stoi(tokens[2]);

    if (fnameToPath.find(fname) == fnameToPath.end()) {
      cout << "❌ File not found for: " << fname << endl;
      continue;
    }

    string fullPath = fnameToPath[fname];
    int fd = open(fullPath.c_str(), O_RDONLY);
    if (fd == -1) {
      perror("Unable to open file");
      continue;
    }

    off_t offset = chunkIndex * BUFFERSIZE;
    unsigned char chunkBuf[BUFFERSIZE];

    if (lseek(fd, offset, SEEK_SET) == -1) {
      perror("Failed to seek to chunk");
      close(fd);
      continue;
    }

    int bytesRead = read(fd, chunkBuf, BUFFERSIZE);
    if (bytesRead <= 0) {
      perror("Failed to read chunk");
      close(fd);
      continue;
    }

    close(fd);

    // Send 4-byte chunk length
    int chunkLenNetwork = htonl(bytesRead);
    if (send(peerSocket, &chunkLenNetwork, sizeof(chunkLenNetwork), 0) == -1) {
      perror("Failed to send chunk size");
      continue;
    }

    // Send actual chunk data
    int totalSent = 0;
    while (totalSent < bytesRead) {
      int sent =
          send(peerSocket, chunkBuf + totalSent, bytesRead - totalSent, 0);
      if (sent <= 0) {
        perror("Failed to send chunk data");
        break;
      }
      totalSent += sent;
    }

    cout << "✅ Sent chunk " << chunkIndex << " of file " << fname
         << " of size " << bytesRead << " to peer.\n";
    cout << "📤 Finished serving chunk " << chunkIndex
         << ". Ready for next...\n";
  }

  // ✅ Close socket after all requests are done
  close(peerSocket);
  cout << "🧵 Exiting peer handler thread\n";
  // Show prompt again to resume clean input
  cout << "\nEnter commands:> " << flush;
}

// void SendFile(int peerSocket, string fpath, string fname) {
//   // cout<<"entering 3rd"<<endl;
//   // string fullPath = fpath + "/" + fname;
//   string fullPath = fpath;
//   // sending file to client
//   int fd = open(fullPath.c_str(), O_RDONLY);

//   if (fd == -1) {
//     perror("Unable to open file");
//     return;
//   }

//   struct stat fileStat;
//   if (stat(fullPath.c_str(), &fileStat) == -1) {
//     perror("Failed to stat file");
//     close(fd);
//     return;
//   }
//   off_t fileSize = fileStat.st_size;

//   // Send file size first
//   if (send(peerSocket, &fileSize, sizeof(fileSize), 0) == -1) {
//     perror("Failed to send file size");
//     close(fd);
//     return;
//   }

//   cout << "FILE SHARING STARTED..." << endl;
//   // once file is opened, read the contents in small chunks and send it
//   throught
//   // the socket to client
//   unsigned char buffer[BUFFERSIZE];
//   int bytesRead, bytesSent;
//   int chunkCount = 0;
//   string finalHash = "";
//   long long totalSizeSend = 0;

//   while ((bytesRead = read(fd, buffer, sizeof(buffer))) > 0) {
//     chunkCount++;
//     // Loop to ensure all bytes are sent (handles partial send)
//     ssize_t totalSent = 0;
//     while (totalSent < bytesRead) {
//       bytesSent =
//           send(peerSocket, buffer + totalSent, bytesRead - totalSent, 0);
//       if (bytesSent < 0) {
//         perror("Failed to send file chunk to peer");
//         close(fd); // 🔧 [GPT] Ensure file is closed
//         return;
//       }
//       totalSent += bytesSent;
//     }
//     totalSizeSend += totalSent;

//     // 🔐 [Phase 2] Send SHA1 hash right after each chunk
//     // string hash = calculateSHA1Hash(buffer, bytesRead);
//     // finalHash += hash; // Concatenate the first 10 char of hash of
//     this chunk
//     // to the final hash

//     // if (send(peerSocket, hash.c_str(), hash.size(), 0) == -1)
//     // {
//     //     perror("Failed to send chunk hash to peer");
//     //     close(fd);
//     //     return;
//     // }

//     // sleep(1);
//   }

//   // cout << "Final concatenated hash: " << finalHash << endl;

//   if (bytesRead < 0) {
//     perror("error reading file during send");
//   }

//   // string fileHash = calculateSHA1Hash((unsigned char
//   *)finalHash.c_str(),
//   // finalHash.size());

//   // Send it to peer
//   // if (send(peerSocket, fileHash.c_str(), fileHash.size(), 0) == -1)
//   // {
//   //     perror("Failed to send final file hash");
//   //     close(fd);
//   //     return;
//   // }
//   // cout << "Final file hash sent to peer: " << fileHash << endl;
//   close(fd);
//   cout << "File is sent to client successfully" << endl;

//   // cout << "Final concatenated hash of chunks send from server: " <<
//   finalHash
//   // << endl;
//   cout << "Total chunks sent: " << chunkCount << endl;
//   cout << "✅ Sent " << totalSizeSend << " bytes in total." << endl;
//   cout << "File transfer complete to requesting peer." << endl;

//   close(peerSocket); // Close the peer socket after sending the file
// }

void FindFileMetadata(string filePath, string &command) {
  FileMetadata metadata;

  struct stat fileStat;
  if (stat(filePath.c_str(), &fileStat) == -1) {
    perror("cannot access the given file: INVALID FILE PATH");
    return;
  }
  // finding file size
  metadata.fileSize = fileStat.st_size;

  // cout << "size of file: " << metadata.fileSize << " byte"<< endl;

  // finding filename
  int index = filePath.find_last_of("/\\");
  metadata.fileName = filePath.substr(index + 1);

  // cout << "Filename: " << metadata.fileName << endl;

  // finding no of chunks
  uint64_t chunkSize = BUFFERSIZE;
  metadata.noOfChunks = metadata.fileSize / chunkSize;
  // adding 1 more chunk if chunk size do not divide filesize completely
  if ((metadata.fileSize % chunkSize) > 0) {
    metadata.noOfChunks++;
  }
  // cout << "No of chunks: " << metadata.noOfChunks << endl;

  // calculating sha1 hash

  int fd = open(filePath.c_str(), O_RDONLY);

  if (fd == -1) {
    perror("Unable to open file");
    close(fd);
    return;
  }

  // once file is opened, read the contents in small chunks of size 512
  // bytes
  unsigned char buffer[BUFFERSIZE];
  int bytesRead;
  int count = 0;
  // string concatenatedHash = "";
  while (count < metadata.noOfChunks) {
    // reading chunk
    bytesRead = read(fd, buffer, sizeof(buffer));
    if (bytesRead == -1) {
      perror("there was a error in reading the data from file");
      close(fd);
      return;
    }
    string chunkHash = calculateSHA1Hash(buffer, bytesRead);
    metadata.hashOfChunks.push_back(chunkHash);
    // concatenatedHash += chunkHash; // Concatenate the hash of all chunks
    // cout << "Sending chunk " << Count << " to client." << endl;
    // sleep(1);
    count++;
  }

  // converting string hash back to byte array hash
  // vector<unsigned char> concatenatedByteArray =
  //     hexStringToByteArray(concatenatedHash);
  // // using data() gives you a pointer that points to the first element of
  // // the array inside the vector
  // metadata.HashOfCompleteFile = calculateSHA1Hash(
  //     concatenatedByteArray.data(), concatenatedByteArray.size());

  lseek(fd, 0, SEEK_SET);
  unsigned char *fileData = (unsigned char *)mmap(
      NULL, metadata.fileSize, PROT_READ, MAP_PRIVATE, fd, 0);
  if (fileData == MAP_FAILED) {
    perror("mmap failed");
    close(fd);
    return;
  }

  metadata.HashOfCompleteFile = calculateSHA1Hash(fileData, metadata.fileSize);

  cout << "Hash of complete file while storing: " << metadata.HashOfCompleteFile
       << endl;
  munmap(fileData, metadata.fileSize);
  close(fd);

  command += " " + to_string(metadata.fileSize) + " " + metadata.fileName +
             " " + to_string(metadata.noOfChunks) + " " +
             metadata.HashOfCompleteFile;

  //[phase 4] appending all chunk hashes to command
  for (const auto &chunkHash : metadata.hashOfChunks) {
    cout << "appending chunk hash: " << chunkHash << endl;
    command += " " + chunkHash;
  }
  // cout<<command<<endl;

  // updating map to store file path for easy search when client ask for
  // that file
  //  cout<<filePath<<endl;

  fnameToPath[metadata.fileName] = filePath;
  cout << "File metadata sent for upload: " << command << endl;
}

string ConvertToHex(unsigned char c) {
  string hex;
  char chars[3]; // A char array of size 3 to hold 2 hexadecimal digits and
                 // a null-terminator
  snprintf(chars, sizeof(chars), "%02x",
           c); // snprintf prints the hexadecimal representation of c into chars
  hex.append(chars); // Appends the converted hexadecimal characters to the
                     // string hex
  return hex;
}

string calculateSHA1Hash(unsigned char *buffer, int size) {
  unsigned char hash[SHA_DIGEST_LENGTH]; // This array of size 20 bytes will
                                         // hold the SHA-1 hash

  // SHA1 calls the OpenSSL SHA1 function to calculate the hash of buffer of
  // length size, and store it in hash
  SHA1(buffer, size, hash);

  // SHA1() expects the data to be hashed to be of type const unsigned char*
  // it produces a hash in the form of a byte array, where each byte is an
  // unsigned 8-bit integer like 10010011 as it is binary data we convert to
  // to hwxadecimal representation as each byte (8 bits) can be represented
  // by exactly two hexadecimal digits

  // this will hold the final hexadecimal representation of the hash
  string hashOfChunk;
  for (int i = 0; i < SHA_DIGEST_LENGTH; i++) {
    hashOfChunk += ConvertToHex(hash[i]);
  }

  return hashOfChunk;
}

vector<unsigned char> hexStringToByteArray(string &hexString) {
  vector<unsigned char> byteArray;

  for (int i = 0; i < int(hexString.length()); i += 2) {
    string byteString = hexString.substr(i, 2);
    char byte = (char)strtol(byteString.c_str(), NULL, 16);
    byteArray.push_back(byte);
  }

  return byteArray;
}

void print(vector<string> &str) {
  for (auto v : str) {
    cout << v << endl;
  }
}

// when i type download command, i get response from tracker that contains
// userID and port number of clients that have that file, I tokenize it now
// every odd index in vector have port number of client having i file i want
// to download NOTE: every client as well as tracker is on local host
// 127.0.0.1 for downlloading first im implementing it without leecher
// concept, in my simple implementation multiple client can download files
// at once but from only 1 client only and that client will have complete
// file and once complete file is transfered then we check sha of complete
// file with the sha provided by the client and if its equal we update
// tracker that now we also have file,

// utility functions

bool isValidIdentifier(const string &str) {
  if (str.empty() || str.size() > 20)
    return false;
  for (char c : str) {
    if (!isalnum(c) && c != '_' && c != '-' && c != '.') {
      return false;
    }
  }
  return true;
}

vector<string> ExtractArguments(string &str)

{
  vector<string> arguments;
  string temp;
  for (auto c : str) {
    if (c == ' ') {
      if (temp.empty() == false) {
        arguments.push_back(temp);
        temp.clear();
      }
    } else {
      temp += c;
    }
  }
  if (temp.empty() == false) {
    arguments.push_back(temp);
  }
  return arguments;
}
vector<string> tokenizeVector(string &str) {
  vector<string> tokens;
  string temp;
  for (auto c : str) {
    if (c == ' ') {
      if (temp.empty() == false) {
        tokens.push_back(temp);
        temp.clear();
      }
    } else {
      temp += c;
    }
  }
  if (temp.empty() == false) {
    tokens.push_back(temp);
  }
  return tokens;
}

vector<string> tokenizePeers(string &str) {
  vector<string> tokens;
  string temp;
  for (auto c : str) {
    if (c == ' ') {
      if (temp.empty() == false) {
        tokens.push_back(temp);
        temp.clear();
      }
    } else {
      temp += c;
    }
  }
  if (temp.empty() == false) {
    tokens.push_back(temp);
  }
  return tokens;
}

bool isStrongPassword(const string &password) {
  if (password.length() < 5)
    return false;

  bool hasUpper = false, hasLower = false, hasDigit = false, hasSpecial = false;

  for (char c : password) {
    if (isspace(c))
      return false;
    if (isupper(c))
      hasUpper = true;
    else if (islower(c))
      hasLower = true;
    else if (isdigit(c))
      hasDigit = true;
    else
      hasSpecial = true;
  }

  return hasUpper && hasLower && hasDigit && hasSpecial;
}

////////////////////////////////////////////////

// 🔧 [GPT] handleUploadReq: Sends file requested by another peer
// void handleUploadReq(int peerSocket)
// {
//     char buffer[BUFFERSIZE] = {0};

//     // Receive filename
//     ssize_t bytesReceived = recv(peerSocket, buffer, sizeof(buffer) - 1,
//     0); if (bytesReceived <= 0)
//     {
//         perror("Failed to receive filename from peer");
//         shutdown(peerSocket, SHUT_RDWR);
//         close(peerSocket);
//         return;
//     }
//     buffer[bytesReceived] = '\0';
//     string requestedFile(buffer);

//     cout << "Peer requested file: " << requestedFile << endl;

//     // Call your existing SendFile() function
//     SendFile(peerSocket, requestedFile);

//     shutdown(peerSocket, SHUT_RDWR);
//     close(peerSocket);
// }

// // 🔧 [GPT] fileServerThread: Starts a listener for peer requests
// void fileServerThread(int port)
// {
//     int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
//     if (serverSocket == -1)
//     {
//         perror("Unable to create socket for peer file server");
//         return;
//     }

//     sockaddr_in address;
//     address.sin_family = AF_INET;
//     address.sin_port = htons(port);
//     address.sin_addr.s_addr = INADDR_ANY;

//     int opt = 1;
//     setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT,
//     &opt, sizeof(opt));

//     if (bind(serverSocket, (struct sockaddr *)&address, sizeof(address))
//     == -1)
//     {
//         perror("Bind failed for file server");
//         close(serverSocket);
//         return;
//     }

//     if (listen(serverSocket, 5) == -1)
//     {
//         perror("Listen failed for file server");
//         close(serverSocket);
//         return;
//     }

//     cout << "File server listening on port: " << port << endl;

//     while (true)
//     {
//         sockaddr_in peerAddr;
//         socklen_t addrLen = sizeof(peerAddr);
//         int peerSocket = accept(serverSocket, (struct sockaddr
//         *)&peerAddr, &addrLen); if (peerSocket < 0)
//         {
//             perror("Failed to accept connection from peer");
//             continue;
//         }

//         // 🔧 Detach thread to handle multiple peers
//         thread(handleUploadReq, peerSocket).detach();
//     }
// }

bool sendAll(int socket, const char *data, size_t totalBytes) {
  size_t totalSent = 0;
  while (totalSent < totalBytes) {
    ssize_t sent = send(socket, data + totalSent, totalBytes - totalSent, 0);
    if (sent <= 0) {
      return false;
    }
    totalSent += sent;
  }
  return true;
}

string DeserializePeerStats(const PeerStats &peer) {
  // Serialize peer stats
  string message = "update_peer_stats " + peer.ip + " " + to_string(peer.port) +
                   " " + to_string(peer.score) + " " +
                   to_string(peer.chunksServed) + " " +
                   to_string(peer.totalDownloadTime) + " ";

  return message;
}

void AssignChunksToPeers(int totalChunks) {
  int numPeers = peerStatsMap.size();
  assignedChunks.clear();

  if (numPeers == 0)
    return;

  // Step 1: Normalize all peer scores
  float maxScore = 0.0;
  for (const auto &[_, stats] : peerStatsMap)
    maxScore = max(maxScore, stats.score);

  unordered_map<string, float> normalizedScores;
  int threshold = ceil((float)totalChunks / numPeers * THRESHOLD_K); // ✅ Peer chunk cap

  // for (const auto &[key, stats] : peerStatsMap)
  //   normalizedScores[key] = (maxScore > 0) ? stats.score / maxScore : 0;

  // Step 2: Assign chunks based on score - load utility

  unordered_map<string, int> chunksAssigned;

  // }

  for (int i = 0; i < totalChunks; i++) {
    float bestScore = -1e9;
    string bestPeerID = "";

    for (const auto &[peerID, stats] : peerStatsMap) {
      int currentLoad = chunksAssigned[peerID];

      if (currentLoad >= threshold)
        continue;

      float penalty = (float)currentLoad / threshold;

      float finalScore = SCORE_ALPHA * stats.score - SCORE_BETA * penalty;

      if (finalScore > bestScore) {
        bestScore = finalScore;
        bestPeerID = peerID;
      }
    }

    if (!bestPeerID.empty()) {
      chunksAssigned[bestPeerID]++;
      assignedChunks[i] = ChunkAssignment(i, peerStatsMap[bestPeerID]);
    } else {
      cerr << "⚠️ No eligible peer found for chunk " << i << endl;
    }
  }

  // 📊 Final Summary
  cout << "\n📊 Final Chunk Distribution:\n";
  for (const auto &[key, count] : chunksAssigned)
    cout << " - " << key << ": " << count << " chunks\n";
}

char *MapFileToMemory(int fd, size_t totalSize) {
  void *mapped =
      mmap(NULL, totalSize, PROT_WRITE | PROT_READ, MAP_SHARED, fd, 0);
  if (mapped == MAP_FAILED) {
    perror("❌ mmap failed");
    return nullptr;
  }
  return (char *)mapped;
}

int PrepareFileForWriting(const string &filePath, size_t totalSize) {
  int fd = open(filePath.c_str(), O_RDWR | O_CREAT, 0666);
  if (fd < 0) {
    perror("❌ Failed to open destination file");
    return -1;
  }

  // Stretch the file size
  if (ftruncate(fd, totalSize) == -1) {
    perror("❌ Failed to preallocate file size");
    close(fd);
    return -1;
  }

  return fd;
}

void ChunkWorkerThread() {
  while (true) {
    int peerSocket;

    {
      unique_lock<mutex> lock(queueMutex);
      queueCV.wait(lock, [] { return !socketQueue.empty(); });

      peerSocket = socketQueue.front();
      socketQueue.pop();
    }

    cout << "🧵 Worker picked socket " << peerSocket << endl;
    ShareToClient(peerSocket); // Your existing function
  }
}

// Send heartbeat (PING) and wait for PONG
void StartHeartbeatMonitor(int clientSocket, const string &currentTrackerIP,
                           const string &currentTrackerPort) {
  // cout << "Heartbeat monitor started for tracker " << currentTrackerIP << ":"
  //  << currentTrackerPort << endl;
  // this_thread::sleep_for(chrono::seconds(1));

  while (true) {
    // cout<<"h1"<<endl;
    // Send PING
    string pingMsg = "PING";
    // cout << "\nSending PING to tracker " << currentTrackerIP << ":"
    //  << currentTrackerPort << endl;
    send(clientSocket, pingMsg.c_str(), pingMsg.size(), 0);
    // cout << "h2" << endl;
    // Wait for response with timeout using future
    char buffer[1024] = {0};
    future<ssize_t> responseFuture = async(launch::async, [&]() {
      return recv(clientSocket, buffer, sizeof(buffer), 0);
    });
    // cout << "h3" << endl;
    if (responseFuture.wait_for(chrono::seconds(5)) == future_status::ready) {
      ssize_t bytes = responseFuture.get();
      if (bytes > 0) {
        string response(buffer, bytes);
        if (response == "PONG") {
          // cout << "\n✅ PONG received from tracker " << currentTrackerIP <<
          //  ":" << currentTrackerPort << endl;
          // All good
          this_thread::sleep_for(chrono::seconds(10));
          continue;
        }
      }
    }

    // No valid response received in 5s
    cout << "\n⚠️ Tracker " << currentTrackerIP << ":" << currentTrackerPort
         << " is unresponsive." << endl;
    trackerAlive = false;
    break; // Stop the thread
  }
  cout << "Heartbeat monitor stopped for tracker " << currentTrackerIP << ":"
       << currentTrackerPort << endl;

  // Step 1: Notify Load Balancer to REMOVE the crasher tracker
  int lbSock = socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in lbAddr;
  lbAddr.sin_family = AF_INET;
  lbAddr.sin_port = htons(loadBalancerPort); // set this globally
  inet_pton(AF_INET, loadBalancerIP.c_str(),
            &lbAddr.sin_addr); // set globally

  if (connect(lbSock, (sockaddr *)&lbAddr, sizeof(lbAddr)) == 0) {
    string msg = "REMOVE " + currentTrackerIP + " " + currentTrackerPort;
    send(lbSock, msg.c_str(), msg.size(), 0);
    cout << "📉 Sent REMOVE for dead tracker to Load Balancer\n";
  } else {
    cerr << "❌ Failed to connect to Load Balancer for REMOVE\n";
  }

  // ✅ Wait for ACK or REMOVED
  char buffer[1024];
  ssize_t bytesRead = recv(lbSock, buffer, sizeof(buffer), 0);
  if (bytesRead > 0) {
    string response(buffer, bytesRead);
    cout << "📩 Load Balancer response: " << response;
  } else {
    cerr << "⚠️ No response from Load Balancer after sending update\n";
  }

  close(lbSock);

  // cout << "Enter commands:> ";
}

bool ReconnectToAnotherTracker(int &clientSocket) {
  cout << "Reconnecting to another tracker..." << endl;

  string trackerIP;
  int trackerPort;
  if (!GetBestTrackerFromLoadBalancer(trackerIP, trackerPort))
    return false;

  int newSocket = socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in serv_addr;
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(trackerPort);
  inet_pton(AF_INET, trackerIP.c_str(), &serv_addr.sin_addr);

  if (connect(newSocket, (sockaddr *)&serv_addr, sizeof(serv_addr)) == 0) {
    close(clientSocket); // close old socket
    clientSocket = newSocket;
    trackerAlive = true;

    // Restart heartbeat
    thread t(StartHeartbeatMonitor, clientSocket, trackerIP,
             to_string(trackerPort));
    t.detach();

    cout << "✅ Reconnected to tracker: " << trackerIP << ":" << trackerPort
         << endl;
    return true;
  }

  close(newSocket);
  return false;
}
// 🔁 [Multi-Tracker Load Balancing] Get best tracker from Load Balancer
bool GetBestTrackerFromLoadBalancer(string &trackerIP, int &trackerPort) {

  cout << "entering GetBestTrackerFromLoadBalancer" << endl;
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) {
    cerr << "❌ Failed to create socket to load balancer" << endl;
    return false;
  }

  sockaddr_in servAddr;
  servAddr.sin_family = AF_INET;
  servAddr.sin_port = htons(loadBalancerPort);

  if (inet_pton(AF_INET, loadBalancerIP.c_str(), &servAddr.sin_addr) <= 0) {
    cerr << "❌ Invalid load balancer IP" << endl;
    close(sock);
    return false;
  }

  if (connect(sock, (struct sockaddr *)&servAddr, sizeof(servAddr)) < 0) {
    cerr << "❌ Failed to connect to load balancer" << endl;
    close(sock);
    return false;
  }

  cout << "connected to load balancer" << endl;

  string request = "GET_TRACKER";
  send(sock, request.c_str(), request.size(), 0);
  cout << "sent GET_TRACKER request to load balancer" << endl;

  char buffer[BUFFERSIZE];
  ssize_t bytesRead = recv(sock, buffer, sizeof(buffer), 0);
  close(sock);

  if (bytesRead <= 0) {
    cerr << "❌ No response from load balancer" << endl;
    return false;
  }
  cout << "bytesRead: " << bytesRead << endl;

  string response(buffer, bytesRead);
  if (response == "NO_TRACKERS\n") {
    cerr << "⚠️ Load balancer reported no active trackers" << endl;
    return false;
  }

  cout << "response: " << response << endl;

  istringstream iss(response);
  iss >> trackerIP >> trackerPort;
  cout << "trackerIP: " << trackerIP << ", trackerPort: " << trackerPort
       << endl;
  return true;
}
