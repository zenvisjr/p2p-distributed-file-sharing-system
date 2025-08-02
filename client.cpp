#include <iostream>
#include <thread>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <arpa/inet.h> // Needed for inet_pton
#include <fcntl.h>     // For open()
#include <vector>
#include <cstdio> //for snprintf
#include <iomanip>
#include <openssl/sha.h> //for SHA1 hashing
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <unordered_map>
#include <mutex>

std::mutex globalMutex; // 🌐 [Phase 3] Protect shared maps

using namespace std;
#define BUFFERSIZE 512*1024 // 512 KB buffer size for file transfer
#define MAX_CONNECTION 50

class FileMetadata
{
public:
    int fileSize; // uint64_t is a 64-bit unsigned integer, which can represent a much larger range of values
    string fileName;
    int noOfChunks;
    vector<string> hashOfChunks;
    string HashOfCompleteFile;
};

unordered_map<string, string> fnameToPath;

vector<string> ExtractArguments(string &);
void checkAppendSendRecieve(vector<string> &arg, string &command, string ip, string port, int clientSocket);
string calculateSHA1Hash(unsigned char *buffer, int size);
string ConvertToHex(unsigned char c);
vector<unsigned char> hexStringToByteArray(string &hexString);
void FindFileMetadata(string filepath, string &command);
void print(vector<string> &str);
vector<string> tokenizePeers(string &str);
vector<string> tokenizeVector(string &str);
vector<string> ExtractArguments(string &str);

void RecieveFile(int clientSocket, string destinationPath, string fileName,
                 const vector<string> &chunkHashes, const string &expectedCompleteHash);
void DownloadFromClient(int clientPort, string destinationPath, string fileName,
                        const vector<string> &chunkHashes, const string &expectedCompleteHash);
void ShareToClient(int listeningSocket);
void SendFile(int socket, string fpath, string fname);
void DownloadHandler(string clientIP, string clientPort);

void fileServerThread(int port);
void handleUploadReq(int peerSocket);

bool isValidIdentifier(const string &str);
bool isStrongPassword(const string &password);

bool sendAll(int socket, const char *data, size_t totalBytes);

int main(int argc, char *argv[])
{
    // checking if correct no of arguments were passed
    if (argc != 3)
    {
        perror("Usage : ./client <IP>:<PORT> tracker_info.txt");
    }

    // extracting IP and port number of client passed as 2nd argument
    string socketInfo = argv[1];
    string cLientIP, clientPort;
    for (int i = 0; i < socketInfo.size(); i++)
    {
        if (socketInfo[i] == ':')
        {
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

    // extracting IP and port number of tracker from tracker_info.txt passed as 3rd argument

    // opening the file tracker_info.txt
    int fd = open(argv[2], O_RDONLY);
    if (fd < 0)
    {
        perror("tracker_info.txt cant be opened due to unexpected error");
        exit(1);
    }

    // reading its content into buffer
    char buffer[100];
    ssize_t bytesRead;
    string extract = "";
    while ((bytesRead = read(fd, buffer, sizeof(buffer) - 1)) > 0) // read() doesn't automatically null-terminate the buffer, so we create space for null
    {
        // adding null at end of buffer so that it is easy to work with string
        buffer[bytesRead] = '\0';
        extract += buffer;
    }

    if (bytesRead == -1)
    {
        perror("there was a error in reading tracker_info.txt");
        exit(1);
    }

    close(fd);
    // cout<<extract<<endl;

    // extracting IP and port of tracker
    string trackerIP, trackerPort;
    for (int i = 0; i < extract.size(); i++)
    {
        if (extract[i] == ':')
        {
            trackerPort = extract.substr(i + 1);
            break;
        }
        trackerIP += extract[i];
    }

    // cout<<IPAddress<<endl;
    // cout<<portNumber<<endl;
    // Create a new socket on client side
    int domain = AF_INET;
    int type = SOCK_STREAM;
    int protocol = 0;
    int clientSocket = socket(domain, type, protocol);
    if (clientSocket == -1)
    {
        perror("unable to create a socket at client side");
        exit(1);
    }
    // setting options for the serverSocket
    int option = 1;
    int setOption = setsockopt(clientSocket, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &option, sizeof(option));
    if (setOption == -1)
    {
        perror("unable to create a socket at server side");
        exit(1);
    }

    // Define the server's address structure
    int trackerPortNumber = atoi(trackerPort.c_str());

    sockaddr_in serverAddress;
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(trackerPortNumber);
    inet_pton(AF_INET, trackerIP.c_str(), &serverAddress.sin_addr); // Convert IP address to byte form

    // Connect to the server
    int connectFD = connect(clientSocket, (struct sockaddr *)&serverAddress, sizeof(serverAddress));
    if (connectFD == -1)
    {
        perror("unable to connect to the server");
        exit(1);
    }
    else
    {
        cout << "Client is connected with tracker on " << trackerIP << ":" << trackerPortNumber << endl;
    }
    if (clientPort == "6001")
    {
        vector<string> arguments;
        string command;
        command = "create_user alice password";
        arguments = ExtractArguments(command);
        checkAppendSendRecieve(arguments, command, cLientIP, clientPort, clientSocket);
        command = "login alice password";
        arguments = ExtractArguments(command);
        checkAppendSendRecieve(arguments, command, cLientIP, clientPort, clientSocket);
        command = "create_group g1";
        arguments = ExtractArguments(command);
        checkAppendSendRecieve(arguments, command, cLientIP, clientPort, clientSocket);
    }
    else if (clientPort == "6002")
    {
        vector<string> arguments;
        string command;
        command = "create_user bob password";
        arguments = ExtractArguments(command);
        checkAppendSendRecieve(arguments, command, cLientIP, clientPort, clientSocket);
        command = "login bob password";
        arguments = ExtractArguments(command);
        checkAppendSendRecieve(arguments, command, cLientIP, clientPort, clientSocket);
        command = "join_group g1";
        arguments = ExtractArguments(command);
        checkAppendSendRecieve(arguments, command, cLientIP, clientPort, clientSocket);
    }

    while (true)
    {
        cout << "Enter commands:> ";
        string command;
        getline(cin, command);
        if (command == "")
        {
            // If the command is empty, prompt the user to enter a valid command
            cout << "Command cannot be empty" << endl;
            continue;
        }
        vector<string> arguments = ExtractArguments(command);
        checkAppendSendRecieve(arguments, command, cLientIP, clientPort, clientSocket);
    }

    close(clientSocket);
    return 0;
}

void checkAppendSendRecieve(vector<string> &arg, string &command, string ip, string port, int clientSocket)
{
    int sizeOfArgs = arg.size();
    char bufferRecv[BUFFERSIZE];

    if (arg[0] == "create_user")
    {
        if (arg.size() != 3)
        {
            cout << "USAGE: create_user <user_id> <passwd>" << endl;
            return;
        }
        //  command = arg[0] + " " + arg[1] + " " + arg[2];

        // Validating user_id and password
        string userId = arg[1];
        string password = arg[2];
        if (!isValidIdentifier(userId))
        {
            cout << "Error: Invalid user_id. Only [a-zA-Z0-9_.-] allowed, max 20 characters." << endl;
            return;
        }
        // if (!isStrongPassword(password))
        // {
        //     cout << "Error: Password must be at least 8 characters long, contain uppercase, lowercase, digit, and special character." << endl;
        //     return;
        // }
        if (send(clientSocket, command.c_str(), command.length(), 0) == -1)
        {
            perror("there was a error in sending command to server");
            return;
        }

        // char bufferRecv[512];
        int bytesRecieved = recv(clientSocket, bufferRecv, sizeof(bufferRecv) - 1, 0);
        // cout<<"after recv"<<endl;
        if (bytesRecieved < 0)
        {
            perror("unable to recieve data from tracker");
        }
        bufferRecv[bytesRecieved] = '\0'; // Null-terminating the received acknowledge

        // Safely converting char array to string
        string recvAck(bufferRecv, bytesRecieved);

        // cout<<"hii"<<endl;
        cout << recvAck << endl;
    }
    else if (arg[0] == "login")
    {
        if (arg.size() != 3)
        {
            cout << "USAGE: login <user_id> <passwd>" << endl;
            return;
        }

        // Validating user_id and password
        string userId = arg[1];
        string password = arg[2];

        if (!isValidIdentifier(userId))
        {
            cout << "Error: Invalid user_id. Only [a-zA-Z0-9_.-] allowed, max 20 characters." << endl;
            return;
        }
        // if (!isStrongPassword(password))
        // {
        //     cout << "Error: Password must be at least 8 characters long, contain uppercase, lowercase, digit, and special character." << endl;
        //     return;
        // }

        command = arg[0] + " " + arg[1] + " " + arg[2] + " " + ip + " " + port;
        if (send(clientSocket, command.c_str(), command.length(), 0) == -1)
        {
            perror("there was a error in sending command to server");
            return;
        }

        // char bufferRecv[512];
        int bytesRecieved = recv(clientSocket, bufferRecv, sizeof(bufferRecv) - 1, 0);
        if (bytesRecieved < 0)
        {
            perror("unable to recieve data from tracker");
        }
        bufferRecv[bytesRecieved] = '\0';
        string recvAck(bufferRecv, bytesRecieved);
        cout << recvAck << endl;
    }
    else if (arg[0] == "logout")
    {
        if (arg.size() != 1)
        {
            cout << "USAGE: logout" << endl;
        }
        else
        {
            // command = arg[0];
            if (send(clientSocket, command.c_str(), command.length(), 0) == -1)
            {
                perror("there was a error in sending command to server");
                return;
            }

            // char bufferRecv[512];
            int bytesRecieved = recv(clientSocket, bufferRecv, sizeof(bufferRecv) - 1, 0);
            if (bytesRecieved < 0)
            {
                perror("unable to recieve data from tracker");
            }
            bufferRecv[bytesRecieved] = '\0';
            string recvAck(bufferRecv, bytesRecieved);
            cout << recvAck << endl;
        }
    }
    else if (arg[0] == "create_group")
    {
        if (arg.size() != 2)
        {
            cout << "USAGE: create_group <group_id>" << endl;
            return;
        }

        // Validating group_id
        string groupId = arg[1];
        if (!isValidIdentifier(groupId))
        {
            cout << "Error: Invalid group_id. Only [a-zA-Z0-9_.-] allowed, max 20 characters." << endl;
            return;
        }

        // command = arg[0];
        if (send(clientSocket, command.c_str(), command.length(), 0) == -1)
        {
            perror("there was a error in sending command to server");
            return;
        }

        // char bufferRecv[512];
        int bytesRecieved = recv(clientSocket, bufferRecv, sizeof(bufferRecv) - 1, 0);
        if (bytesRecieved < 0)
        {
            perror("unable to recieve data from tracker");
        }
        bufferRecv[bytesRecieved] = '\0';
        string recvAck(bufferRecv, bytesRecieved);
        cout << recvAck << endl;
    }
    else if (arg[0] == "join_group")
    {
        if (arg.size() != 2)
        {
            cout << "USAGE: join_group <group_id>" << endl;
            return;
        }
        string groupId = arg[1];

        if (!isValidIdentifier(groupId))
        {
            cout << "Error: Invalid group_id. Only [a-zA-Z0-9_.-] allowed, max 20 characters." << endl;
            return;
        }

        // command = arg[0];
        if (send(clientSocket, command.c_str(), command.length(), 0) == -1)
        {
            perror("there was a error in sending command to server");
            return;
        }

        // char bufferRecv[512];
        int bytesRecieved = recv(clientSocket, bufferRecv, sizeof(bufferRecv) - 1, 0);
        if (bytesRecieved < 0)
        {
            perror("unable to recieve data from tracker");
        }
        bufferRecv[bytesRecieved] = '\0';
        string recvAck(bufferRecv, bytesRecieved);
        cout << recvAck << endl;
    }
    else if (arg[0] == "leave_group")
    {
        if (arg.size() != 2)
        {
            cout << "USAGE: leave_group <group_id>" << endl;
            return;
        }

        string groupId = arg[1];
        if (!isValidIdentifier(groupId))
        {
            cout << "Error: Invalid group_id. Only [a-zA-Z0-9_.-] allowed, max 20 characters." << endl;
            return;
        }

        // command = arg[0];
        if (send(clientSocket, command.c_str(), command.length(), 0) == -1)
        {
            perror("there was a error in sending command to server");
            return;
        }

        // char bufferRecv[512];
        int bytesRecieved = recv(clientSocket, bufferRecv, sizeof(bufferRecv) - 1, 0);
        if (bytesRecieved < 0)
        {
            perror("unable to recieve data from tracker");
        }
        bufferRecv[bytesRecieved] = '\0';
        string recvAck(bufferRecv, bytesRecieved);
        cout << recvAck << endl;
    }
    else if (arg[0] == "list_requests")
    {
        if (arg.size() != 2)
        {
            cout << "USAGE: list_requests <group_id>" << endl;
            return;
        }
        string groupId = arg[1];
        if (!isValidIdentifier(groupId))
        {
            cout << "Error: Invalid group_id. Only [a-zA-Z0-9_.-] allowed, max 20 characters." << endl;
            return;
        }
        // command = arg[0];
        if (send(clientSocket, command.c_str(), command.length(), 0) == -1)
        {
            perror("there was a error in sending command to server");
            return;
        }

        // char bufferRecv[512];
        int bytesRecieved = recv(clientSocket, bufferRecv, sizeof(bufferRecv) - 1, 0);
        if (bytesRecieved < 0)
        {
            perror("unable to recieve data from tracker");
        }
        bufferRecv[bytesRecieved] = '\0';
        // cout << "Received " << bytesRecieved << " bytes: " << bufferRecv << endl;
        string recvAck(bufferRecv, bytesRecieved);

        // checking if 1st char of string is #
        if (recvAck[0] != '#')
        {
            cout << recvAck << endl;
        }
        else
        {
            // cout<<"Entering else"<<endl;
            string pendingList = recvAck;
            // cout<<pendingList<<endl;
            vector<string> pending = tokenizeVector(pendingList);
            for (auto list : pending)
            {
                cout << list << endl;
            }
            // int noOfGroups = stoi(recvAck.substr(1));
        }
    }
    else if (arg[0] == "accept_request")
    {
        if (arg.size() != 3)
        {
            cout << "USAGE: accept_request <group_id> <user_id>" << endl;
            return;
        }

        string groupId = arg[1];
        string userId = arg[2];

        if (!isValidIdentifier(groupId) || !isValidIdentifier(userId))
        {
            cout << "Error: Invalid group_id or user_id. Only [a-zA-Z0-9_.-] allowed, max 20 characters." << endl;
            return;
        }
        // command = arg[0];
        if (send(clientSocket, command.c_str(), command.length(), 0) == -1)
        {
            perror("there was a error in sending command to server");
            return;
        }

        // char bufferRecv[512];
        int bytesRecieved = recv(clientSocket, bufferRecv, sizeof(bufferRecv) - 1, 0);
        if (bytesRecieved < 0)
        {
            perror("unable to recieve data from tracker");
        }
        bufferRecv[bytesRecieved] = '\0';
        string recvAck(bufferRecv, bytesRecieved);
        cout << recvAck << endl;
    }
    else if (arg[0] == "list_groups")
    {
        if (arg.size() > 1)
        {
            cout << "USAGE: list_groups" << endl;
        }
        else
        {
            // command = arg[0];
            if (send(clientSocket, command.c_str(), command.length(), 0) == -1)
            {
                perror("there was a error in sending command to server");
                return;
            }

            // char bufferRecv[512];
            int bytesRecieved = recv(clientSocket, bufferRecv, sizeof(bufferRecv) - 1, 0);
            if (bytesRecieved < 0)
            {
                perror("unable to recieve data from tracker");
            }
            bufferRecv[bytesRecieved] = '\0';
            // cout << "Received " << bytesRecieved << " bytes: " << bufferRecv << endl;
            string recvAck(bufferRecv, bytesRecieved);

            // checking if 1st char of string is #
            if (recvAck[0] != '#')
            {
                cout << recvAck << endl;
            }
            else
            {
                // cout<<"Entering else"<<endl;
                string groupName = recvAck;
                // cout<<groupName<<endl;
                vector<string> groups = tokenizeVector(groupName);
                for (auto gr : groups)
                {
                    cout << gr << endl;
                }
                // int noOfGroups = stoi(recvAck.substr(1));
            }
        }
    }

    else if (arg[0] == "upload_file")
    {
        // FileMetadata metadata;
        if (arg.size() != 3)
        {
            cout << "upload_file <file_path> <group_id>" << endl;
            return;
        }
        string filePath = arg[1];
        string groupId = arg[2];

        // check if path is valid path of file
        struct stat fileStat;
        if (stat(filePath.c_str(), &fileStat) == -1)
        {
            perror("cannot access the given file: INVALID FILE PATH");
            return;
        }

        if (!S_ISREG(fileStat.st_mode))
        {
            perror("Invalid file: file is not a regular file");
            return;
        }

        if (!isValidIdentifier(groupId))
        {
            cout << "Error: Invalid group_id. Only [a-zA-Z0-9_.-] allowed, max 20 characters." << endl;
            return;
        }
        // check if file exists

        // final required metadata of file and append it in command
        FindFileMetadata(filePath, command);

        // command = arg[0];
        cout << "file metadata prepared for upload: " << command << endl;
        size_t len = command.size();
        string header = "@LARGE@";
        send(clientSocket, header.c_str(), header.size(), 0);
        send(clientSocket, &len, sizeof(len), 0);
        if (!sendAll(clientSocket, command.c_str(), command.length()))
        {
            perror("Failed to send full metadata to tracker");
            return;
        }
        int bytesRecieved = recv(clientSocket, bufferRecv, sizeof(bufferRecv) - 1, 0);
        if (bytesRecieved < 0)
        {
            perror("unable to recieve data from tracker");
        }
        bufferRecv[bytesRecieved] = '\0';
        // cout << "Received " << bytesRecieved << " bytes: " << bufferRecv << endl;
        string recvAck(bufferRecv, bytesRecieved);

        // checking if 1st char of string is #
        cout << recvAck << endl;
    }
    else if (arg[0] == "list_files")
    {
        if (arg.size() != 2)
        {
            cout << "USAGE: list_files <group_id>" << endl;
            return;
        }

        string groupId = arg[1];
        if (!isValidIdentifier(groupId))
        {
            cout << "Error: Invalid group_id. Only [a-zA-Z0-9_.-] allowed, max 20 characters." << endl;
            return;
        }
        // command = arg[0];
        if (send(clientSocket, command.c_str(), command.length(), 0) == -1)
        {
            perror("there was a error in sending command to server");
            return;
        }

        // char bufferRecv[512];
        int bytesRecieved = recv(clientSocket, bufferRecv, sizeof(bufferRecv) - 1, 0);
        if (bytesRecieved < 0)
        {
            perror("unable to recieve data from tracker");
        }
        bufferRecv[bytesRecieved] = '\0';
        // cout << "Received " << bytesRecieved << " bytes: " << bufferRecv << endl;
        string recvAck(bufferRecv, bytesRecieved);

        // cout<<recvAck<<endl;
        // checking if 1st char of string is #
        if (recvAck[0] != '#')
        {
            cout << recvAck << endl;
        }
        else
        {
            // cout<<"Entering else"<<endl;
            string fileName = recvAck;
            // cout<<fileName<<endl;
            vector<string> files = tokenizeVector(fileName);
            for (auto f : files)
            {
                cout << f << endl;
            }
            // int noOfGroups = stoi(recvAck.substr(1));
        }
    }
    else if (arg[0] == "stop_share")
    {
        if (arg.size() != 3)
        {
            cout << "USAGE: stop_share <group_id> <file_name>" << endl;
            return;
        }

        string groupId = arg[1];
        string fileName = arg[2];

        if (!isValidIdentifier(groupId) || !isValidIdentifier(fileName))
        {
            cout << "Error: Invalid group_id or file_name. Only [a-zA-Z0-9_.-] allowed, max 20 characters." << endl;
            return;
        }
        if (send(clientSocket, command.c_str(), command.length(), 0) == -1)
        {
            perror("there was a error in sending command to server");
            return;
        }

        // char bufferRecv[512];
        int bytesRecieved = recv(clientSocket, bufferRecv, sizeof(bufferRecv) - 1, 0);
        if (bytesRecieved < 0)
        {
            perror("unable to recieve data from tracker");
        }
        bufferRecv[bytesRecieved] = '\0';
        // cout << "Received " << bytesRecieved << " bytes: " << bufferRecv << endl;
        string recvAck(bufferRecv, bytesRecieved);

        cout << recvAck << endl;
    }
    else if (arg[0] == "download_file")
    {
        if (arg.size() != 4)
        {
            cout << "USAGE: download_file <group_id> <file_name> <destination_path>" << endl;
            return;
        }

        string groupId = arg[1];
        string fileName = arg[2];
        string destinationPath = arg[3];

        if (!isValidIdentifier(groupId) || !isValidIdentifier(fileName))
        {
            cout << "Error: Invalid group_id or file_name. Only [a-zA-Z0-9_.-] allowed, max 20 characters." << endl;
            return;
        }

        // checking if destination_path exist
        struct stat st;
        if (stat(destinationPath.c_str(), &st) != 0 || !S_ISDIR(st.st_mode))
        {
            perror("cannot access the given path: INVALID DESTINATION PATH");
            return;
        }

        if (send(clientSocket, command.c_str(), command.length(), 0) == -1)
        {
            perror("there was a error in sending command to server");
            return;
        }

        // Step 1: Receive size
        int metadataLen;
        int n = recv(clientSocket, &metadataLen, sizeof(int), 0);
        if (n <= 0)
        {
            perror("Error receiving metadata length");
            return;
        }
        cout << "Metadata size to receive: " << metadataLen << " bytes" << endl;

        // Step 2: Receive full metadata in chunks
        string recvAck;
        int totalReceived = 0;
        while (totalReceived < metadataLen)
        {
            char buffer[512];
            int bytes = recv(clientSocket, buffer, min(512, metadataLen - totalReceived), 0);
            if (bytes <= 0)
            {
                perror("Error receiving metadata content");
                return;
            }
            recvAck.append(buffer, bytes);
            totalReceived += bytes;
        }
        // cout << "Received " << bytesRecieved << " bytes: " << bufferRecv << endl;
        // recvAck(bufferRecv, totalReceived);

        vector<string> tokens = tokenizeVector(recvAck);

        cout << "Received metadata: " << recvAck << endl;
        // vector<string> tokens;
        // stringstream ss(recvAck);
        string word;
        // while (ss >> word)
        //     tokens.push_back(word);
        print(tokens);

        cout << "1st token: " << tokens[0] << endl;
        // Checking if the first token is "OK"

        if (tokens[0] != "OK")
        {
            cout << "Error: " << tokens[0] << endl;
            cout << recvAck << endl;
            cout << "File not found or you do not have permission to download it." << endl;
            return; // If the response is not OK, exit the function
        }

        cout << "File metadata received successfully." << endl;

        // Extracting file metadata from the response
        int fileSize = stoi(tokens[1]);
        int noOfChunks = stoi(tokens[2]);
        string completeFileHash = tokens[3];
        cout << "File Size: " << fileSize << endl;
        cout << "No of Chunks: " << noOfChunks << endl;

        vector<string> chunkHashes;
        for (int i = 4; i < 4 + noOfChunks; ++i)
        {
            chunkHashes.push_back(tokens[i]);
            cout << "Chunk " << i - 4 << " Hash: " << tokens[i] << endl;
        }

        vector<pair<string, int>> peers;
        for (int i = 4 + noOfChunks; i < tokens.size(); i += 2)
        {
            string peerID = tokens[i];
            int peerPort = stoi(tokens[i + 1]);
            peers.push_back({peerID, peerPort});
            cout << "Peer ID: " << peerID << ", Port: " << peerPort << endl;
        }

        cout << "Peers with file:" << endl;
        for (auto &p : peers)
            cout << p.first << " : " << p.second << endl;

        // Download from first peer for now (we'll improve this later with rarest-first)
        string fname = arg[2];
        // string destinationPath = arg[3];
        cout << "Downloading from peer: " << peers[0].first << endl;

        DownloadFromClient(peers[0].second, destinationPath, fname, chunkHashes, completeFileHash); // You need to update this function to verify each chunk using `chunkHashes`

        string response = "DownloadCompleteSuccessfully";
        send(clientSocket, response.c_str(), strlen(response.c_str()), 0);
        if (send(clientSocket, response.c_str(), strlen(response.c_str()), 0) < 0)
        {
            perror("Error in sending download status to tracker");
            return;
        }
    }
    else if (arg[0] == "show_downloads")
    {
        if (arg.size() != 1)
        {
            cout << "USAGE: show_downloads" << endl;
        }
        else
        {
            if (send(clientSocket, command.c_str(), command.length(), 0) == -1)
            {
                perror("there was a error in sending command to server");
                return;
            }

            // char bufferRecv[512];
            int bytesRecieved = recv(clientSocket, bufferRecv, sizeof(bufferRecv) - 1, 0);
            if (bytesRecieved < 0)
            {
                perror("unable to recieve data from tracker");
            }
            bufferRecv[bytesRecieved] = '\0';
            // cout << "Received " << bytesRecieved << " bytes: " << bufferRecv << endl;
            string recvAck(bufferRecv, bytesRecieved);
            vector<string> downloadInfo = tokenizeVector(recvAck);
            print(downloadInfo);
            // cout<<recvAck<<endl;
        }
    }
    else if (arg[0] == "quit")
    {
        if (arg.size() != 1)
        {
            cout << "USAGE: quit" << endl;
        }
        else
        {
            if (send(clientSocket, command.c_str(), command.length(), 0) == -1)
            {
                perror("there was a error in sending command to server");
                return;
            }

            int bytesRecieved = recv(clientSocket, bufferRecv, sizeof(bufferRecv) - 1, 0);
            if (bytesRecieved < 0)
            {
                perror("unable to recieve data from tracker");
            }
            bufferRecv[bytesRecieved] = '\0';
            // cout << "Received " << bytesRecieved << " bytes: " << bufferRecv << endl;
            string recvAck(bufferRecv, bytesRecieved);
            cout << recvAck << endl;
        }
    }

    else
    {
        cout << "WRONG COMMAND: The command u entered does not exists" << endl;
        // string response = "The command u entered does not exists";
        // send(clientSocket, response.c_str(), response.size(), 0);
    }
}

void DownloadFromClient(int clientPort, string destinationPath, string fileName,
                        const vector<string> &chunkHashes, const string &expectedCompleteHash)

{

    cout << "Download from Client" << endl;

    // Create a new socket on client side
    // int domain = AF_INET;
    // int type = SOCK_STREAM;
    // int protocol = 0;
    int clientSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (clientSocket == -1)
    {
        perror("unable to create a socket for downloading");
        return;
    }
    // setting options for the clientSocket
    int option = 1;
    int setOption = setsockopt(clientSocket, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &option, sizeof(option));
    if (setOption == -1)
    {
        perror("unable to create a socket at server side");
        // close the clientSocket if fail to set options
        close(clientSocket);
        exit(1);
    }

    // Define other client's address structure
    sockaddr_in serverAddress;
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(clientPort);
    inet_pton(AF_INET, "127.0.0.1", &serverAddress.sin_addr); // Convert IP address to byte form

    // Connect to the other client for downloading
    int connectFD = connect(clientSocket, (struct sockaddr *)&serverAddress, sizeof(serverAddress));
    if (connectFD == -1)
    {
        perror("unable to connect to the other client for downloading");
        close(clientSocket); // Close the socket if connection fails
        return;
    }
    else
    {
        cout << "Client is connected to other client on port: " << clientPort << endl;
    }

    // updated send to handle failure
    // Send request to peer for file
    string request = fileName;

    if (send(clientSocket, request.c_str(), request.size(), 0))
    {
        cout << "Request sent to peer for file: " << fileName << endl;
    }
    else
    {
        perror("Failed to send request to peer");
        shutdown(clientSocket, SHUT_RDWR); // Disables read/write on socket
        close(clientSocket);               // Releases file descriptor
        return;
    }

    // cout<<"hyyyyyyy"<<endl;
    RecieveFile(clientSocket, destinationPath, fileName, chunkHashes, expectedCompleteHash);
    // cout<<"sending response back to tracker"<<endl;
    string response = "DownloadCompleteSuccessfully";
    if (send(clientSocket, response.c_str(), response.size(), 0) == -1)
    {
        perror("Failed to send response to tracker");
    }

    // close the clientSocket after download is complete
    shutdown(clientSocket, SHUT_RDWR); // Disables read/write on socket
    close(clientSocket);               // Releases file descriptor
}

// Function to receive data from the client
void RecieveFile(int clientSocket, string destinationPath, string fileName,
                 const vector<string> &chunkHashes, const string &expectedCompleteHash)
{
    string fullPath = destinationPath + "/" + fileName;

    int fd = open(fullPath.c_str(), O_WRONLY | O_CREAT, 0644);
    if (fd == -1)
    {
        perror("Unable to reserve space for downloaded file");
        return;
    }

    long long totalFileSize = 0;
    int bytesRead = recv(clientSocket, &totalFileSize, sizeof(totalFileSize), 0);
    cout << "Total file size to receive: " << totalFileSize << " bytes" << endl;
    if (bytesRead != sizeof(totalFileSize))
    {
        perror("Failed to receive total file size");
        close(fd);
        return;
    }

    unsigned char buffer[BUFFERSIZE];
    string combinedChunkHashes;
    long long totalReceived = 0;
    int chunkIndex = 0;

    cout << "DOWNLOAD STARTED..." << endl;

    while (totalReceived < totalFileSize)
    {
        int bytesToRead = min(BUFFERSIZE, static_cast<int>(totalFileSize - totalReceived));
        int bytesReceived = 0;

        // 🛠️ Receive exactly bytesToRead in a loop
        while (bytesReceived < bytesToRead)
        {
            int r = recv(clientSocket, buffer + bytesReceived, bytesToRead - bytesReceived, 0);
            if (r <= 0)
            {
                perror("Failed to receive data from sender");
                close(fd);
                return;
            }
            bytesReceived += r;
        }

        // ✅ Hash verification
        string localHash = calculateSHA1Hash(buffer, bytesReceived);
        cout << "Local hash for chunk " << chunkIndex << ": " << localHash << endl;
        if (chunkIndex >= chunkHashes.size())
        {
            cerr << "❌ More chunks received than expected!" << endl;
            break;
        }
        if (localHash != chunkHashes[chunkIndex])
        {
            cerr << "❌ Hash mismatch in chunk " << (chunkIndex + 1) << endl;
        }
        else
        {
            cout << "✔️ Chunk " << (chunkIndex + 1) << " hash verified." << endl;
        }

        combinedChunkHashes += localHash;
        chunkIndex++;

        if (write(fd, buffer, bytesReceived) == -1)
        {
            perror("Error writing file to disk");
            close(fd);
            return;
        }

        totalReceived += bytesReceived;
        cout << "Received " << totalReceived << " bytes out of " << totalFileSize << " bytes." << endl;
    }

    // ✅ Final SHA1 hash of all chunks
    vector<unsigned char> concatenatedByteArray = hexStringToByteArray(combinedChunkHashes);

    string finalHash = calculateSHA1Hash((concatenatedByteArray.data()), concatenatedByteArray.size());
    cout << "Final combined hash: " << finalHash << endl;
    cout << "Expected complete file hash: " << expectedCompleteHash << endl;
    if (finalHash != expectedCompleteHash)
    {
        cerr << "❌ Final file hash mismatch! File may be corrupted." << endl;
    }
    else
    {
        cout << "✅ Final file hash verified successfully." << endl;
    }

    close(fd);
    cout << "✅ File is downloaded and stored at: " << fullPath << endl;
}

void DownloadHandler(string clientIP, string port)
{
    int clientPort = stoi(port);

    // cout<<"Entering download handler"<<endl;
    // Step 1: Create a new socket
    int listeningSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (listeningSocket == -1)
    {
        perror("Can't create listening socket");
        exit(1);
    }

    // Step 2: Bind the socket
    sockaddr_in listeningAddress;
    listeningAddress.sin_family = AF_INET;
    listeningAddress.sin_port = htons(clientPort);
    inet_pton(AF_INET, clientIP.c_str(), &listeningAddress.sin_addr);

    if (bind(listeningSocket, (struct sockaddr *)&listeningAddress, sizeof(listeningAddress)) == -1)
    {
        perror("Can't bind listening socket");
        exit(1);
    }

    // Step 3: Put the socket in listen mode
    if (listen(listeningSocket, MAX_CONNECTION) == -1)
    {
        perror("Can't listen on listening socket");
        exit(1);
    }
    else
    {
        cout << "Client is listening for request on " << clientIP << ":" << clientPort << endl;
    }

    // Step 4 and 5: Loop to accept and handle incoming connections
    while (true)
    {
        // cout<<"Enterinng accept phase"<<endl;
        sockaddr_in clientAddress;
        socklen_t clientSize = sizeof(clientAddress);
        int clientSocket = accept(listeningSocket, (struct sockaddr *)&clientAddress, &clientSize);

        if (clientSocket == -1)
        {
            perror("Can't accept client");
            continue; // go back to listening for the next client
        }
        else if (clientSocket > 0)
        {
            cout << "Accepted connection from other client" << endl;
        }

        // Spawn a new thread to handle this client
        thread downloadThread(ShareToClient, clientSocket);
        downloadThread.detach(); // detach the thread
    }
}

void ShareToClient(int listeningSocket)
{
    // while(1)
    // {
    // cout<<"Entering here"<<endl;
    char buffer[BUFFERSIZE];
    int bytesRecieved = recv(listeningSocket, buffer, sizeof(buffer) - 1, 0);

    if (bytesRecieved <= 0)
    {
        if (bytesRecieved < 0)
        {
            perror("Unable to read command");
        }
        else
        {
            // Client has closed the connection
            cout << "Client has closed the connection" << endl;
        }
        return;
    }
    buffer[bytesRecieved] = '\0'; // Null-terminating the received data

    // cout << "Data received: " << buffer << endl;

    // Safely converting char array to string
    string requestedFileName(buffer, bytesRecieved);
    // cout<<requestedFileName<<endl;

    if (fnameToPath.find(requestedFileName) != fnameToPath.end())
    {
        string localFilePath = fnameToPath[requestedFileName];

        // cout<<"2nd enter"<<endl;
        // Proceed to read and send the file located at 'localFilePath'
        SendFile(listeningSocket, localFilePath, requestedFileName);
    }
    else
    {
        cout << "File not found" << endl;
    }
    // }
}

void SendFile(int peerSocket, string fpath, string fname)
{
    // cout<<"entering 3rd"<<endl;
    // string fullPath = fpath + "/" + fname;
    string fullPath = fpath;
    // sending file to client
    int fd = open(fullPath.c_str(), O_RDONLY);

    if (fd == -1)
    {
        perror("Unable to open file");
        return;
    }

    struct stat fileStat;
    if (stat(fullPath.c_str(), &fileStat) == -1)
    {
        perror("Failed to stat file");
        close(fd);
        return;
    }
    off_t fileSize = fileStat.st_size;

    // Send file size first
    if (send(peerSocket, &fileSize, sizeof(fileSize), 0) == -1)
    {
        perror("Failed to send file size");
        close(fd);
        return;
    }

    cout << "FILE SHARING STARTED..." << endl;
    // once file is opened, read the contents in small chunks and send it throught the socket to client
    unsigned char buffer[BUFFERSIZE];
    int bytesRead, bytesSent;
    int chunkCount = 0;
    string finalHash = "";
    long long totalSizeSend = 0;

    while ((bytesRead = read(fd, buffer, sizeof(buffer))) > 0)
    {
        chunkCount++;
        // Loop to ensure all bytes are sent (handles partial send)
        ssize_t totalSent = 0;
        while (totalSent < bytesRead)
        {
            bytesSent = send(peerSocket, buffer + totalSent, bytesRead - totalSent, 0);
            if (bytesSent < 0)
            {
                perror("Failed to send file chunk to peer");
                close(fd); // 🔧 [GPT] Ensure file is closed
                return;
            }
            totalSent += bytesSent;
        }
        totalSizeSend += totalSent;

        // 🔐 [Phase 2] Send SHA1 hash right after each chunk
        // string hash = calculateSHA1Hash(buffer, bytesRead);
        // finalHash += hash; // Concatenate the first 10 char of hash of this chunk to the final hash

        // if (send(peerSocket, hash.c_str(), hash.size(), 0) == -1)
        // {
        //     perror("Failed to send chunk hash to peer");
        //     close(fd);
        //     return;
        // }

        // sleep(1);
    }

    // cout << "Final concatenated hash: " << finalHash << endl;

    if (bytesRead < 0)
    {
        perror("error reading file during send");
    }

    // string fileHash = calculateSHA1Hash((unsigned char *)finalHash.c_str(), finalHash.size());

    // Send it to peer
    // if (send(peerSocket, fileHash.c_str(), fileHash.size(), 0) == -1)
    // {
    //     perror("Failed to send final file hash");
    //     close(fd);
    //     return;
    // }
    // cout << "Final file hash sent to peer: " << fileHash << endl;
    close(fd);
    cout << "File is sent to client successfully" << endl;

    // cout << "Final concatenated hash of chunks send from server: " << finalHash << endl;
    cout << "Total chunks sent: " << chunkCount << endl;
    cout << "✅ Sent " << totalSizeSend << " bytes in total." << endl;
    cout << "File transfer complete to requesting peer." << endl;

    close(peerSocket); // Close the peer socket after sending the file
}

void FindFileMetadata(string filePath, string &command)
{
    FileMetadata metadata;

    struct stat fileStat;
    if (stat(filePath.c_str(), &fileStat) == -1)
    {
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
    if ((metadata.fileSize % chunkSize) > 0)
    {
        metadata.noOfChunks++;
    }
    // cout << "No of chunks: " << metadata.noOfChunks << endl;

    // calculating sha1 hash

    int fd = open(filePath.c_str(), O_RDONLY);

    if (fd == -1)
    {
        perror("Unable to open file");
        close(fd);
        return;
    }

    // once file is opened, read the contents in small chunks of size 512 bytes
    unsigned char buffer[BUFFERSIZE];
    int bytesRead;
    int count = 0;
    string concatenatedHash = "";
    while (count < metadata.noOfChunks)
    {
        // reading chunk
        bytesRead = read(fd, buffer, sizeof(buffer));
        if (bytesRead == -1)
        {
            perror("there was a error in reading the data from file");
            close(fd);
            return;
        }
        string chunkHash = calculateSHA1Hash(buffer, bytesRead);
        metadata.hashOfChunks.push_back(chunkHash);
        concatenatedHash += chunkHash; // Concatenate the hash of all chunks
        // cout << "Sending chunk " << Count << " to client." << endl;
        // sleep(1);
        count++;
    }

    // converting string hash back to byte array hash
    vector<unsigned char> concatenatedByteArray = hexStringToByteArray(concatenatedHash);
    // using data() gives you a pointer that points to the first element of the array inside the vector
    metadata.HashOfCompleteFile = calculateSHA1Hash(concatenatedByteArray.data(), concatenatedByteArray.size());

    cout << "Hash of complete file while storing: " << metadata.HashOfCompleteFile << endl;
    
    close(fd);

    command += " " + to_string(metadata.fileSize) + " " + metadata.fileName + " " + to_string(metadata.noOfChunks) + " " + metadata.HashOfCompleteFile;

    //[phase 4] appending all chunk hashes to command
    for (const auto &chunkHash : metadata.hashOfChunks)
    {
        cout << "appending chunk hash: " << chunkHash << endl;
        command += " " + chunkHash;
    }
    // cout<<command<<endl;

    // updating map to store file path for easy search when client ask for that file
    //  cout<<filePath<<endl;

    fnameToPath[metadata.fileName] = filePath;
    cout << "File metadata sent for upload: " << command << endl;
}

string ConvertToHex(unsigned char c)
{
    string hex;
    char chars[3];                             // A char array of size 3 to hold 2 hexadecimal digits and a null-terminator
    snprintf(chars, sizeof(chars), "%02x", c); // snprintf prints the hexadecimal representation of c into chars
    hex.append(chars);                         // Appends the converted hexadecimal characters to the string hex
    return hex;
}

string calculateSHA1Hash(unsigned char *buffer, int size)
{
    unsigned char hash[SHA_DIGEST_LENGTH]; // This array of size 20 bytes will hold the SHA-1 hash

    // SHA1 calls the OpenSSL SHA1 function to calculate the hash of buffer of length size, and store it in hash
    SHA1(buffer, size, hash);

    // SHA1() expects the data to be hashed to be of type const unsigned char*
    // it produces a hash in the form of a byte array, where each byte is an unsigned 8-bit integer like 10010011
    // as it is binary data we convert to to hwxadecimal representation as each byte (8 bits) can be represented by exactly two hexadecimal digits

    // this will hold the final hexadecimal representation of the hash
    string hashOfChunk;
    for (int i = 0; i < SHA_DIGEST_LENGTH; i++)
    {
        hashOfChunk += ConvertToHex(hash[i]);
    }

    return hashOfChunk;
}

vector<unsigned char> hexStringToByteArray(string &hexString)
{
    vector<unsigned char> byteArray;

    for (int i = 0; i < hexString.length(); i += 2)
    {
        string byteString = hexString.substr(i, 2);
        char byte = (char)strtol(byteString.c_str(), NULL, 16);
        byteArray.push_back(byte);
    }

    return byteArray;
}

void print(vector<string> &str)
{
    for (auto v : str)
    {
        cout << v << endl;
    }
}

// when i type download command, i get response from tracker that contains userID and port number of clients that have that file, I tokenize it now every odd index in vector have port number of client having i file i want to download
// NOTE: every client as well as tracker is on local host 127.0.0.1
// for downlloading first im implementing it without leecher concept, in my simple implementation multiple client can download files at once but from only 1 client only and that client will have complete file and once complete file is transfered then we check sha of complete file with the sha provided by the client and if its equal we update tracker that now we also have file,

// utility functions

bool isValidIdentifier(const string &str)
{
    if (str.empty() || str.size() > 20)
        return false;
    for (char c : str)
    {
        if (!isalnum(c) && c != '_' && c != '-' && c != '.')
        {
            return false;
        }
    }
    return true;
}

vector<string> ExtractArguments(string &str)

{
    vector<string> arguments;
    string temp;
    for (auto c : str)
    {
        if (c == ' ')
        {
            if (temp.empty() == false)
            {
                arguments.push_back(temp);
                temp.clear();
            }
        }
        else
        {
            temp += c;
        }
    }
    if (temp.empty() == false)
    {
        arguments.push_back(temp);
    }
    return arguments;
}
vector<string> tokenizeVector(string &str)
{
    vector<string> tokens;
    string temp;
    for (auto c : str)
    {
        if (c == ' ')
        {
            if (temp.empty() == false)
            {
                tokens.push_back(temp);
                temp.clear();
            }
        }
        else
        {
            temp += c;
        }
    }
    if (temp.empty() == false)
    {
        tokens.push_back(temp);
    }
    return tokens;
}

vector<string> tokenizePeers(string &str)
{
    vector<string> tokens;
    string temp;
    for (auto c : str)
    {
        if (c == ' ')
        {
            if (temp.empty() == false)
            {
                tokens.push_back(temp);
                temp.clear();
            }
        }
        else
        {
            temp += c;
        }
    }
    if (temp.empty() == false)
    {
        tokens.push_back(temp);
    }
    return tokens;
}

bool isStrongPassword(const string &password)
{
    if (password.length() < 5)
        return false;

    bool hasUpper = false, hasLower = false, hasDigit = false, hasSpecial = false;

    for (char c : password)
    {
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
//     ssize_t bytesReceived = recv(peerSocket, buffer, sizeof(buffer) - 1, 0);
//     if (bytesReceived <= 0)
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
//     setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));

//     if (bind(serverSocket, (struct sockaddr *)&address, sizeof(address)) == -1)
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
//         int peerSocket = accept(serverSocket, (struct sockaddr *)&peerAddr, &addrLen);
//         if (peerSocket < 0)
//         {
//             perror("Failed to accept connection from peer");
//             continue;
//         }

//         // 🔧 Detach thread to handle multiple peers
//         thread(handleUploadReq, peerSocket).detach();
//     }
// }

bool sendAll(int socket, const char *data, size_t totalBytes)
{
    size_t totalSent = 0;
    while (totalSent < totalBytes)
    {
        ssize_t sent = send(socket, data + totalSent, totalBytes - totalSent, 0);
        if (sent <= 0)
        {
            return false;
        }
        totalSent += sent;
    }
    return true;
}
