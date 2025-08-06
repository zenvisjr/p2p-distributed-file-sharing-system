#include <arpa/inet.h>
#include <iostream>
#include <mutex>
#include <netinet/in.h>
#include <queue>
#include <sstream>
#include <string>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <utility>
#include <vector>
#include <fcntl.h> // For open()

#define LOAD_BALANCER_PORT 9000
#define BUFFER_SIZE 512 * 1024
#define MAX_CONNECTIONS 50

using namespace std;

struct TrackerInfo {
  string ip;
  int port;
  int clientCount;
};

struct CompareByLoad {
  bool operator()(const TrackerInfo &a, const TrackerInfo &b) const {
    return a.clientCount > b.clientCount; // min-heap
  }
};

priority_queue<TrackerInfo, vector<TrackerInfo>, CompareByLoad> minHeap;
unordered_map<string, TrackerInfo> trackerMap; // For fast update
mutex heapMutex;

vector<string> tokenizeVector(string &str);


void handleConnection(int clientSocket) {
  char buffer[BUFFER_SIZE];
  int bytesRead = read(clientSocket, buffer, sizeof(buffer));
  if (bytesRead <= 0) {
    close(clientSocket);
    return;
  }
  cout << "[Load Balancer] received buffer from client: " << buffer << endl;
  string msg(buffer, bytesRead);
  cout << "[Load Balancer] received message from client: " << msg << endl;

  vector<string> arguments = tokenizeVector(msg);
  cout<<"argument[0]: "<<arguments[0]<<endl;

  lock_guard<mutex> lock(heapMutex);

  if (arguments[0] == "GET_TRACKER") {
    cout << "[Load Balancer] received GET_TRACKER command from client" << endl;

    if (minHeap.empty()) {
      string response = "NO_TRACKERS\n";
      cout << "[Load Balancer] sending NO_TRACKERS to client" << endl;
      send(clientSocket, response.c_str(), response.size(), 0);
    } else {
      TrackerInfo best = minHeap.top();
      string response = best.ip + " " + to_string(best.port) + "\n";
      cout << "[Load Balancer] sending tracker IP and port to client" << endl;
      send(clientSocket, response.c_str(), response.size(), 0);
    }

    cout << "[Load Balancer] closing client socket" << endl;
    close(clientSocket);
    return;
  }

  // For INCREMENT/DECREMENT, we need IP and port
  if(arguments[0] == "INCREMENT" || arguments[0] == "DECREMENT"){
  string ip;
  int port;
  ip = arguments[1];
  port = stoi(arguments[2]);
  string key = ip + ":" + to_string(port);

  if (trackerMap.find(key) == trackerMap.end()) {
    trackerMap[key] = {ip, port, 0};
  }

  if (arguments[0] == "INCREMENT") {
    trackerMap[key].clientCount++;
  } else if (arguments[0] == "DECREMENT") {
    trackerMap[key].clientCount--;
  }
    // Rebuild minHeap
    priority_queue<TrackerInfo, vector<TrackerInfo>, CompareByLoad> newHeap;
    for (auto &[_, info] : trackerMap) {
      newHeap.push(info);
    }
    minHeap = std::move(newHeap);

    cout << "[Load Balancer] " << arguments[0] << " from " << key
         << " → active clients: " << trackerMap[key].clientCount << endl;

    string response = "ACK\n";
    send(clientSocket, response.c_str(), response.size(), 0);
    close(clientSocket);
  }
}

void startLoadBalancer() {
  int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
  if (serverSocket == 0) {
    perror("❌ [Load Balancer] Socket failed");
    exit(EXIT_FAILURE);
  }

  // setting options for the serverSocket
  int option = 1;
  int setOption =
      setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &option,
                 sizeof(option));
  if (setOption == -1) {
    perror("❌ [Load Balancer] unable to create a socket at server side");
    exit(1);
  }

  sockaddr_in address;
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY; // Accept from any IP
  address.sin_port = htons(LOAD_BALANCER_PORT);

  if (bind(serverSocket, (struct sockaddr *)&address, sizeof(address)) < 0) {
    perror("❌ [Load Balancer] Bind failed");
    exit(EXIT_FAILURE);
  }

  if (listen(serverSocket, MAX_CONNECTIONS) < 0) {
    perror("❌ [Load Balancer] Listen failed");
    exit(EXIT_FAILURE);
  }

  cout << "🚦 [Load Balancer] listening on port " << LOAD_BALANCER_PORT << "..."
       << endl;

  while (true) {
    sockaddr_in clientAddr;
    socklen_t addrlen = sizeof(clientAddr);
    int clientSocket =
        accept(serverSocket, (struct sockaddr *)&clientAddr, &addrlen);
    if (clientSocket < 0) {
      perror("❌ [Load Balancer] Accept failed");
      continue;
    }
    cout << "[Load Balancer]accepted connection from client" << endl;

    thread(handleConnection, clientSocket).detach(); // Handle in new thread
  }
}

void preloadTrackersFromFile(const string &filename) {
  int fd = open(filename.c_str(), O_RDONLY);
  if (fd < 0) {
    perror("❌ [Load Balancer] tracker_info.txt can't be opened");
    exit(1);
  }

  char buffer[BUFFER_SIZE];
  ssize_t bytesRead;
  string extract = "";

  while ((bytesRead = read(fd, buffer, sizeof(buffer) - 1)) > 0) {
    buffer[bytesRead] = '\0';
    extract += buffer;
  }

  if (bytesRead == -1) {
    perror("❌ [Load Balancer] Error reading tracker_info.txt");
    exit(1);
  }

  close(fd);

  istringstream iss(extract);
  string line;

  lock_guard<mutex> lock(heapMutex);

  while (getline(iss, line)) {
    if (line.empty())
      continue;

    size_t delim = line.find(':');
    if (delim == string::npos)
      continue;

    string ip = line.substr(0, delim);
    int port = stoi(line.substr(delim + 1));
    string key = ip + ":" + to_string(port);

    trackerMap[key] = {ip, port, 0};
    minHeap.push({ip, port, 0});
  }

  cout << "📦 [Load Balancer] Preloaded trackers from file: " << filename
       << endl;
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    cerr << "Usage: " << argv[0] << " tracker_info.txt" << endl;
    return 1;
  }

  preloadTrackersFromFile(argv[1]);
  startLoadBalancer();
  return 0;
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