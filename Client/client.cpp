#include <iostream>
#include <thread>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <arpa/inet.h>  // Needed for inet_pton
#include <fcntl.h>  // For open()
#include <vector>
#include <cstdio> //for snprintf
#include <iomanip>    
#include <openssl/sha.h>   //for SHA1 hashing
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <unordered_map>
#include <mutex>


using namespace std;
#define BUFFERSIZE 512
# define MAX_CONNECTION 50

class FileMetadata 
{
public:
    int fileSize;    //uint64_t is a 64-bit unsigned integer, which can represent a much larger range of values
    string fileName;
    int noOfChunks;
    vector<string> hashOfChunks;
    string HashOfCompleteFile;
    
    
    // FileMetadata(int fsize, string& fname, int fchunks, vector<string>& chash, string& fhash)
    // {
    //     fileSize = fsize;
    //     fileName = fname;
    //     noOfChunks = fchunks;
    //     hashOfChunks = chash;
    //     HashOfCompleteFile = fhash;
    // }
};

unordered_map<string, string> fnameToPath;

// class DownloadInfo 
// {
// public:
//     enum Status {Downloading, Complete};
//     // string group_id;
//     string filename;
//     Status status;
// };

// vector<DownloadInfo> downloads;  // Holds the download statuses
// mutex downloadMutex;  // To make access to 'downloads' thread-safe



vector<string> ExtractArguments(string&); 
void checkAppendSendRecieve(vector<string>& arg, string& command, string ip, string port, int clientSocket);
string calculateSHA1Hash(unsigned char* buffer, int size);
string ConvertToHex(unsigned char c);
vector<unsigned char> hexStringToByteArray(string& hexString);
void FindFileMetadata(string filepath, string &command);
void print(vector<string>& str);
vector<string> tokenizePeers(string& str);
vector<string> tokenizeVector(string& str);
vector<string> ExtractArguments(string& str);

void RecieveFile(int clientSocket, string destinationPath, string fileName);
void DownloadFromClient(int clientPort,string dpath,string fname);
void ShareToClient(int listeningSocket);
void SendFile(int socket, string fpath, string fname);  
void DownloadHandler(string clientIP, string clientPort);






int main(int argc, char *argv[]) 
{
    //checking if correct no of arguments were passed
    if(argc != 3)
    {
        perror("Usage : ./client <IP>:<PORT> tracker_info.txt");
    }

    //extracting IP and port number of client passed as 2nd argument
    string socketInfo = argv[1];
    string IP, PORT;
    for(int i=0; i < socketInfo.size(); i++)
    {
        if(socketInfo[i] == ':')
        {
            PORT = socketInfo.substr(i+1);
            break;
        }

        IP += socketInfo[i];
    }
    // cout<<IP<<endl;
    // cout<<PORT<<endl;
    // int port = stoi(PORT);
    //creating a new thread for listening for new connection from another clients
    thread clientAsServerThread(DownloadHandler, IP, PORT);
    clientAsServerThread.detach();
    sleep(0.7);

    //moving forward with our main logic

    //extracting IP and port number of tracker from tracker_info.txt passed as 3rd argument

    //opening the file tracker_info.txt
    int fd = open(argv[2], O_RDONLY);
    if(fd < 0)
    {
        perror("tracker_info.txt cant be opened due to unexpected error");
        exit(1);
    }

    //reading its content into buffer
    char buffer[100];
    ssize_t bytesRead;
    string extract = "";
    while((bytesRead = read(fd, buffer, sizeof(buffer)-1)) > 0)   //read() doesn't automatically null-terminate the buffer, so we create space for null
    {
        //adding null at end of buffer so that it is easy to work with string
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
    
    //extracting IP and port of tracker
    string IPAddress, portNum;
    for(int i=0; i<extract.size(); i++)
    {
        if(extract[i] == ':')
        {
            portNum = extract.substr(i+1);
            break;
        } 
        IPAddress += extract[i];
    }

    int portNumber = atoi(portNum.c_str());

    // cout<<IPAddress<<endl;
    // cout<<portNumber<<endl;
    // Create a new socket on clinet side
    int domain = AF_INET;
    int type = SOCK_STREAM;
    int protocol = 0;
    int clientSocket = socket(domain, type, protocol);
    if(clientSocket == -1)
    {
        perror("unable to create a socket at client side");
        exit(1);
    }
    //setting options for the serverSocket
    int option = 1;
    int setOption = setsockopt(clientSocket, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &option, sizeof(option));
    if(setOption == -1)
    {
        perror("unable to create a socket at server side");
        exit(1);
    }

    // Define the server's address structure
    sockaddr_in serverAddress;
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(portNumber);  
    inet_pton(AF_INET, IPAddress.c_str(), &serverAddress.sin_addr);  // Convert IP address to byte form

    // Connect to the server
    int connectFD = connect(clientSocket, (struct sockaddr *)&serverAddress, sizeof(serverAddress));
        if(connectFD == -1)
    {
        perror("unable to connect to the server");
        exit(1);
    }
    else
    {
        cout<<"Client is connected with tracker on "<<IPAddress<<":"<<portNumber<<endl;
    }
    

    while(true)
    {
        // bool closeTracker = true;
        cout<<"Enter commands:> ";
        string command;
        getline(cin, command);
        // cout <<"command entered: "<<command<< endl ;
        // int bytesRead;
        // cout<<"command length: "<<command.length()<<endl;
        // if(command == "quit")
        // {
        //     closeTracker = false;
        //     break;
        // }

        //extracting command and its arguments and storing it in arguments
        vector<string> arguments = ExtractArguments(command);

        // if(command.substr(0, 11) == "login")
        // {
        //     command += " " + IPAddress + " " + portNum;
        // }

        //appending some more data to the existing arguments, sending the command to trtacker and recieving the response
        checkAppendSendRecieve(arguments, command, IP, PORT, clientSocket); 

        // //sending the updated command
        // if (send(clientSocket, command.c_str(), command.length(), 0) == -1) 
        // {
        //     perror("there was a error in sending file to server");
        //     continue;
        // }
        // cout<<"after send"<<endl;

        // // if (bytesRead == -1) 
        // // {
        // //     perror("there was a error in reading the data from file on client side");
        // // }

        // char bufferRecv[512];
        // int bytesRecieved = recv(clientSocket, bufferRecv, sizeof(bufferRecv)-1, 0);

        // cout<<"after recv"<<endl;
        // if(bytesRecieved < 0)
        // {
        //     perror("unable to recieve acknowledge from tracker");
        // }
        // bufferRecv[bytesRecieved] = '\0';  //Null-terminating the received acknowledge

        // //Safely converting char array to string
        // string recvAck(bufferRecv, bytesRecieved);

        // cout<<recvAck<<endl;
    }

    // Create threads for sending and receiving messages
    // thread threadSend(sendToServer, clientSocket);
    // thread threadReceive(receiveFromServer, clientSocket);

    // Wait for both threads to finish
    // threadSend.join();
    // threadReceive.join();

    // Close the socket
    // close(clientSocket);

    return 0;
}

vector<string> ExtractArguments(string& str) 
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
    if (temp.empty() == false) {
        arguments.push_back(temp);
    }
    return arguments;
}
vector<string> tokenizeVector(string& str) 
{
    vector<string> tokens;
    string temp;
    for (auto c : str) 
    {
        if (c == '#') 
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

vector<string> tokenizePeers(string& str) 
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
void checkAppendSendRecieve(vector<string>& arg, string& command, string ip, string port, int clientSocket)
{
    int sizeOfArgs=arg.size();
    char bufferRecv[BUFFERSIZE];

    if(arg[0] == "create_user")
    {
        if(arg.size() != 3)
        {
            cout<<"USAGE: create_user <user_id> <passwd>"<<endl;
        }
        else
        {
            //  command = arg[0] + " " + arg[1] + " " + arg[2];
             if (send(clientSocket, command.c_str(), command.length(), 0) == -1) 
            {
                perror("there was a error in sending command to server");
                return;
            }

            // char bufferRecv[512];
            int bytesRecieved = recv(clientSocket, bufferRecv, sizeof(bufferRecv)-1, 0);
            // cout<<"after recv"<<endl;
            if(bytesRecieved < 0)
            {
                perror("unable to recieve data from tracker");
            }
            bufferRecv[bytesRecieved] = '\0';  //Null-terminating the received acknowledge

            //Safely converting char array to string
            string recvAck(bufferRecv, bytesRecieved);

            // cout<<"hii"<<endl;
            cout<<recvAck<<endl;
            

        }
    }
    else if(arg[0] == "login")
    {
        if(arg.size() != 3)
        {
            cout<<"USAGE: login <user_id> <passwd>"<<endl;
        }
        else
        {
            command = arg[0] + " " + arg[1] + " " + arg[2] + " " + ip + " " + port;
            if (send(clientSocket, command.c_str(), command.length(), 0) == -1) 
            {
                perror("there was a error in sending command to server");
                return;
            }

            // char bufferRecv[512];
            int bytesRecieved = recv(clientSocket, bufferRecv, sizeof(bufferRecv)-1, 0);
            if(bytesRecieved < 0)
            {
                perror("unable to recieve data from tracker");
            }
            bufferRecv[bytesRecieved] = '\0'; 
            string recvAck(bufferRecv, bytesRecieved);
            cout<<recvAck<<endl;

        }
    }
    else if(arg[0] == "logout")
    {
        if(arg.size() != 1)
        {
            cout<<"USAGE: logout"<<endl;
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
            int bytesRecieved = recv(clientSocket, bufferRecv, sizeof(bufferRecv)-1, 0);
            if(bytesRecieved < 0)
            {
                perror("unable to recieve data from tracker");
            }
            bufferRecv[bytesRecieved] = '\0'; 
            string recvAck(bufferRecv, bytesRecieved);
            cout<<recvAck<<endl;

        }
        
    }
    else if(arg[0] == "create_group")
    {
        if(arg.size() != 2)
        {
            cout<<"USAGE: create_group <group_id>"<<endl;
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
            int bytesRecieved = recv(clientSocket, bufferRecv, sizeof(bufferRecv)-1, 0);
            if(bytesRecieved < 0)
            {
                perror("unable to recieve data from tracker");
            }
            bufferRecv[bytesRecieved] = '\0'; 
            string recvAck(bufferRecv, bytesRecieved);
            cout<<recvAck<<endl;

        }
    }
    else if(arg[0] == "join_group")
    {
        if(arg.size() != 2)
        {
            cout<<"USAGE: join_group <group_id>"<<endl;
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
            int bytesRecieved = recv(clientSocket, bufferRecv, sizeof(bufferRecv)-1, 0);
            if(bytesRecieved < 0)
            {
                perror("unable to recieve data from tracker");
            }
            bufferRecv[bytesRecieved] = '\0'; 
            string recvAck(bufferRecv, bytesRecieved);
            cout<<recvAck<<endl;

        }
    }
    else if(arg[0] == "leave_group")
    {
        if(arg.size() != 2)
        {
            cout<<"USAGE: leave_group <group_id>"<<endl;
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
            int bytesRecieved = recv(clientSocket, bufferRecv, sizeof(bufferRecv)-1, 0);
            if(bytesRecieved < 0)
            {
                perror("unable to recieve data from tracker");
            }
            bufferRecv[bytesRecieved] = '\0'; 
            string recvAck(bufferRecv, bytesRecieved);
            cout<<recvAck<<endl;
        }
    }
    else if(arg[0] == "list_requests")
    {
        if(arg.size() != 2)
        {
            cout<<"USAGE: list_requests <group_id>"<<endl;
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
            int bytesRecieved = recv(clientSocket, bufferRecv, sizeof(bufferRecv)-1, 0);
            if(bytesRecieved < 0)
            {
                perror("unable to recieve data from tracker");
            }
            bufferRecv[bytesRecieved] = '\0'; 
            // cout << "Received " << bytesRecieved << " bytes: " << bufferRecv << endl;
            string recvAck(bufferRecv, bytesRecieved);

            //checking if 1st char of string is #
            if(recvAck[0] != '#')
            {
                cout<<recvAck<<endl;
            }
            else
            {
                // cout<<"Entering else"<<endl;
                string pendingList = recvAck;
                // cout<<pendingList<<endl;
                vector<string> pending = tokenizeVector(pendingList);
                for(auto list : pending)
                {
                    cout<<list<<endl;
                }
                // int noOfGroups = stoi(recvAck.substr(1));
            }

        }
    }
    else if(arg[0] == "accept_request")
    {
        if(arg.size() != 3)
        {
            cout<<"USAGE: accept_request <group_id> <user_id>"<<endl;
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
            int bytesRecieved = recv(clientSocket, bufferRecv, sizeof(bufferRecv)-1, 0);
            if(bytesRecieved < 0)
            {
                perror("unable to recieve data from tracker");
            }
            bufferRecv[bytesRecieved] = '\0'; 
            string recvAck(bufferRecv, bytesRecieved);
            cout<<recvAck<<endl;

        }
    }
    else if(arg[0] == "list_groups")
    {
        if(arg.size() > 1)
        {
            cout<<"USAGE: list_groups"<<endl;
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
            int bytesRecieved = recv(clientSocket, bufferRecv, sizeof(bufferRecv)-1, 0);
            if(bytesRecieved < 0)
            {
                perror("unable to recieve data from tracker");
            }
            bufferRecv[bytesRecieved] = '\0'; 
            // cout << "Received " << bytesRecieved << " bytes: " << bufferRecv << endl;
            string recvAck(bufferRecv, bytesRecieved);

            //checking if 1st char of string is #
            if(recvAck[0] != '#')
            {
                cout<<recvAck<<endl;
            }
            else
            {
                // cout<<"Entering else"<<endl;
                string groupName = recvAck;
                // cout<<groupName<<endl;
                vector<string> groups = tokenizeVector(groupName);
                for(auto gr : groups)
                {
                    cout<<gr<<endl;
                }
                // int noOfGroups = stoi(recvAck.substr(1));
            }

        }
    }

    else if(arg[0] == "upload_file")
    {
        // FileMetadata metadata;
        if(arg.size() != 3)
        {
            cout<<"upload_file <file_path> <group_id>"<<endl;
        }
        else
        {
            string filePath = arg[1];
            
            //check if path is valid path of file
            struct stat fileStat;
            if (stat(filePath.c_str(), &fileStat) == -1)
            {
                perror ("cannot access the given file: INVALID FILE PATH");
                return;
            }
            //final required metadata of file and append it in command
            FindFileMetadata(filePath, command);

            // command = arg[0];
            if (send(clientSocket, command.c_str(), command.length(), 0) == -1) 
            {
                perror("there was a error in sending command to server");
                return;
            }

            // char bufferRecv[512];
            int bytesRecieved = recv(clientSocket, bufferRecv, sizeof(bufferRecv)-1, 0);
            if(bytesRecieved < 0)
            {
                perror("unable to recieve data from tracker");
            }
            bufferRecv[bytesRecieved] = '\0'; 
            // cout << "Received " << bytesRecieved << " bytes: " << bufferRecv << endl;
            string recvAck(bufferRecv, bytesRecieved);

            //checking if 1st char of string is #
            cout<<recvAck<<endl;
        }
    }
    else if(arg[0] == "list_files")
    {
        if(arg.size() != 2)
        {
            cout<<"USAGE: list_files <group_id>"<<endl;
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
            int bytesRecieved = recv(clientSocket, bufferRecv, sizeof(bufferRecv)-1, 0);
            if(bytesRecieved < 0)
            {
                perror("unable to recieve data from tracker");
            }
            bufferRecv[bytesRecieved] = '\0'; 
            // cout << "Received " << bytesRecieved << " bytes: " << bufferRecv << endl;
            string recvAck(bufferRecv, bytesRecieved);

            // cout<<recvAck<<endl;
                        //checking if 1st char of string is #
            if(recvAck[0] != '#')
            {
                cout<<recvAck<<endl;
            }
            else
            {
                // cout<<"Entering else"<<endl;
                string fileName = recvAck;
                // cout<<fileName<<endl;
                vector<string> files = tokenizeVector(fileName);
                for(auto f : files)
                {
                    cout<<f<<endl;
                }
                // int noOfGroups = stoi(recvAck.substr(1));
            }
        }
    }
    else if(arg[0] == "stop_share")
    {
        if(arg.size() != 3)
        {
            cout<<"USAGE: stop_share <group_id> <file_name>"<<endl;
        }
        else
        {
            string filePath = arg[1];
            // //finding filename
            // int index = filePath.find_last_of("/\\");
            // string fname = filePath.substr(index+1);

            // cout << "Filename: " << metadata.fileName << endl;

            // //appending filennamee to command
            // command += " " + fname;
            // cout<<command;
            // FindFileMetadata(filePath, command);
            // cout<<command<<endl;

            // command = arg[0];
            if (send(clientSocket, command.c_str(), command.length(), 0) == -1) 
            {
                perror("there was a error in sending command to server");
                return;
            }

            // char bufferRecv[512];
            int bytesRecieved = recv(clientSocket, bufferRecv, sizeof(bufferRecv)-1, 0);
            if(bytesRecieved < 0)
            {
                perror("unable to recieve data from tracker");
            }
            bufferRecv[bytesRecieved] = '\0'; 
            // cout << "Received " << bytesRecieved << " bytes: " << bufferRecv << endl;
            string recvAck(bufferRecv, bytesRecieved);

            cout<<recvAck<<endl;

        }
    }
    else if(arg[0] == "download_file")
    {
        if(arg.size() != 4)
        {
            cout<<"USAGE: download_file <group_id> <file_name> <destination_path>"<<endl;
        }
        else
        {
            string destinationPath = arg[3];

            //checking if destination_path exist
            struct stat st;
            if(stat(destinationPath.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) 
            {
                perror ("cannot access the given path: INVALID DESTINATION PATH");
                return;
                
            } 
            // //finding filename
            // int index = filePath.find_last_of("/\\");
            // string fname = filePath.substr(index+1);

            // cout << "Filename: " << metadata.fileName << endl;

            // //appending filennamee to command
            // command += " " + fname;
            // cout<<command;
            // FindFileMetadata(filePath, command);
            // cout<<command<<endl;

            // command = arg[0];
            if (send(clientSocket, command.c_str(), command.length(), 0) == -1) 
            {
                perror("there was a error in sending command to server");
                return;
            }

            // char bufferRecv[512];
            int bytesRecieved = recv(clientSocket, bufferRecv, sizeof(bufferRecv)-1, 0);
            if(bytesRecieved < 0)
            {
                perror("unable to recieve data from tracker");
            }
            bufferRecv[bytesRecieved] = '\0'; 
            // cout << "Received " << bytesRecieved << " bytes: " << bufferRecv << endl;
            string recvAck(bufferRecv, bytesRecieved);

            if(recvAck[0] != '#') cout<<recvAck<<endl;
            else
            {
                vector<string> peersWithFile = tokenizeVector(recvAck);
                cout<<"Peers list:"<<endl;
                // print(peersWithFile);
                print(peersWithFile);


                //actual download start from here
                for(int i=1 ;i<peersWithFile.size();i = i+2)
                {
                    int clientPort = stoi(peersWithFile[i]);
                    // cout<<clientPort<<endl;
                    string clientID = peersWithFile[i-1];
                    // cout<<clientID<<endl;
                    string fname = arg[2];
                    // cout<<fname<<endl;

                    cout<<"Downloading from peer : "<<clientID<<endl;
                    DownloadFromClient(clientPort, destinationPath, fname);
                    string response = "DownloadCompleteSuccessfully";
                    if(send(clientSocket,response.c_str(), strlen(response.c_str()), 0)<0)
                    {
                        perror("Error in sending download status to tracker");
                    }
                    break;
                    
                }
            }
            

        }
    }
    else if(arg[0] == "show_downloads")
    {
        if(arg.size() != 1)
        {
            cout<<"USAGE: show_downloads"<<endl;
        }
        else
        {
            if (send(clientSocket, command.c_str(), command.length(), 0) == -1) 
            {
                perror("there was a error in sending command to server");
                return;
            }

            // char bufferRecv[512];
            int bytesRecieved = recv(clientSocket, bufferRecv, sizeof(bufferRecv)-1, 0);
            if(bytesRecieved < 0)
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
    else if(arg[0] == "quit")
    {
        if(arg.size() != 1)
        {
            cout<<"USAGE: quit"<<endl;
        }
        else
        {
            if (send(clientSocket, command.c_str(), command.length(), 0) == -1) 
            {
                perror("there was a error in sending command to server");
                return;
            }

            int bytesRecieved = recv(clientSocket, bufferRecv, sizeof(bufferRecv)-1, 0);
            if(bytesRecieved < 0)
            {
                perror("unable to recieve data from tracker");
            }
            bufferRecv[bytesRecieved] = '\0'; 
            // cout << "Received " << bytesRecieved << " bytes: " << bufferRecv << endl;
            string recvAck(bufferRecv, bytesRecieved);
            cout<<recvAck<<endl;
        }
        
    }

    else
    {
        cout<<"WRONG COMMAND: The command u entered does not exists"<<endl;
        // string response = "The command u entered does not exists";
        // send(clientSocket, response.c_str(), response.size(), 0);
    }
}



void DownloadFromClient(int clientPort,string dpath,string fname)
{
    // cout<<"Download from Client"<<endl;

    // Create a new socket on client side
    // int domain = AF_INET;
    // int type = SOCK_STREAM;
    // int protocol = 0;
    int clientSocket = socket(AF_INET, SOCK_STREAM, 0);
    if(clientSocket == -1)
    {
        perror("unable to create a socket for downloading");
        return;
    }
    //setting options for the clientSocket
    int option = 1;
    int setOption = setsockopt(clientSocket, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &option, sizeof(option));
    if(setOption == -1)
    {
        perror("unable to create a socket at server side");
        exit(1);
    }

    // Define other client's address structure
    sockaddr_in serverAddress;
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(clientPort);  
    inet_pton(AF_INET, "127.0.0.1", &serverAddress.sin_addr);  // Convert IP address to byte form

    // Connect to the other client for downloading
    int connectFD = connect(clientSocket, (struct sockaddr *)&serverAddress, sizeof(serverAddress));
        if(connectFD == -1)
    {
        perror("unable to connect to the other client for downloading");
        return;
    }
    else
    {
        cout<<"Client is connected to other client on port: "<<clientPort<<endl;
    }

    send(clientSocket, fname.c_str(), fname.size(), 0);
    // cout<<"hyyyyyyy"<<endl;
    RecieveFile(clientSocket, dpath, fname);
    // cout<<"sending response back to tracker"<<endl;
    string response = "DownloadCompleteSuccessfully";
    send(clientSocket, fname.c_str(), fname.size(), 0);
    // close(clientSocket);
    // if(status == true)
    // {
    //     send(clientSocket, fname.c_str(), fname.size(), 0);
    // }
}

// Function to receive data from the client
void RecieveFile(int clientSocket, string destinationPath, string fileName) 
{
    // cout<<"entering recieve file"<<endl;
    string fullPath = destinationPath + "/" + fileName;
    //recieving file from client
    int fd = open(fullPath.c_str(), O_WRONLY | O_CREAT, 0644);
    if(fd == -1)
    {
        perror("Unable to reserve space for downloaded file");
        return;
    }


    //once file is opened, write the contents in small chunks recieved throught the socket
    unsigned char buffer[BUFFERSIZE];
    int bytesRecieved;
    int Count = 0;  
    string finalHash = "";

    cout<<"DOWNLOAD STARTED..."<<endl;
    while ((bytesRecieved = recv(clientSocket, buffer, sizeof(buffer), 0)) > 0) 
    {
        Count++;  // New: Increment the chunk counter
        string hash = calculateSHA1Hash(buffer, bytesRecieved);
        finalHash += hash;  // Concatenate the first 10 char of hash of this chunk to the final hash
    //    cout << "Receiving chunk " << Count << " from client." << endl; 

        if (write(fd, buffer, bytesRecieved) == -1) 
        {
            perror("There was an error in writing file on reserved space");
            break;
        }
        // sleep(1); 
    }
    if (bytesRecieved == -1) 
    {

        perror("there was a error in recieving the file data from client side");
    }
    close(fd);

    cout<<"File is downloaded from client and stored at destination path: "<<fullPath<<endl;

    close(clientSocket);

    // cout << "Final concatenated hash of chunks recieved from client: " << finalHash << endl;
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

    if (bind(listeningSocket, (struct sockaddr*)&listeningAddress, sizeof(listeningAddress)) == -1) 
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
        cout<<"Client is listening for request on "<<clientIP<<":"<<clientPort<<endl;

    }


    // Step 4 and 5: Loop to accept and handle incoming connections
    while (true) 
    {
        // cout<<"Enterinng accept phase"<<endl;
        sockaddr_in clientAddress;
        socklen_t clientSize = sizeof(clientAddress);
        int clientSocket = accept(listeningSocket, (struct sockaddr*)&clientAddress, &clientSize);
        
        if (clientSocket == -1) 
        {
            perror("Can't accept client");
            continue;  // go back to listening for the next client
        }
        else if(clientSocket > 0)
        {
            cout<<"Accepted connection from other client"<<endl;
        }
        
        // Spawn a new thread to handle this client
        thread downloadThread(ShareToClient, clientSocket);
        downloadThread.detach();  // detach the thread

        
    }

}

void ShareToClient(int listeningSocket)
{
    // while(1)
    // {
        // cout<<"Entering here"<<endl;
        char buffer[BUFFERSIZE];
        int bytesRecieved = recv(listeningSocket, buffer, sizeof(buffer)-1, 0);

        if(bytesRecieved <= 0)
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
        buffer[bytesRecieved] = '\0';  //Null-terminating the received data

        // cout << "Data received: " << buffer << endl;

        //Safely converting char array to string
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

void SendFile(int clientSocket, string fpath, string fname) 
{
    // cout<<"entering 3rd"<<endl;
    // string fullPath = fpath + "/" + fname;
    string fullPath = fpath;
    //sending file to client
    int fd = open(fullPath.c_str(), O_RDONLY);

    if(fd == -1)
    {
        perror("Unable to open file");
        return;
    }

    cout<<"FILE SHARING STARTED..."<<endl;
    //once file is opened, read the contents in small chunks and send it throught the socket to client
    unsigned char buffer[512];
    int bytesRead;
    int Count = 0; 
    string finalHash = "";

    while ((bytesRead = read(fd, buffer, sizeof(buffer))) > 0)
    {
        Count++; 
        string hash = calculateSHA1Hash(buffer, bytesRead);
        finalHash += hash;  // Concatenate the first 10 char of hash of this chunk to the final hash
        // cout << "Sending chunk " << Count << " to client." << endl;  

        if (send(clientSocket, buffer, bytesRead, 0) == -1) 
        {
            perror("There was an error in sending file to client");
            break;
        }
        // sleep(1);  
    }

    // cout << "Final concatenated hash: " << finalHash << endl;

    if (bytesRead == -1) 
    {
        perror("there was a error in reading the data from file");
    }
    close(fd);
    cout<<"File is sent to client successfully"<<endl;

    close(clientSocket);
    // cout.flush();
    cout<<endl;

    // cout << "Final concatenated hash of chunks send from server: " << finalHash << endl;

}




void FindFileMetadata(string filePath, string &command)
{
    FileMetadata metadata;

    struct stat fileStat;
    if (stat(filePath.c_str(), &fileStat) == -1)
    {
        perror ("cannot access the given file: INVALID FILE PATH");
        return;
    }
    //finding file size
    metadata.fileSize = fileStat.st_size;

    // cout << "size of file: " << metadata.fileSize << " byte"<< endl; 

    //finding filename
    int index = filePath.find_last_of("/\\");
    metadata.fileName = filePath.substr(index+1);

    // cout << "Filename: " << metadata.fileName << endl; 

    //finding no of chunks
    uint64_t chunkSize = BUFFERSIZE;
    metadata.noOfChunks = metadata.fileSize / chunkSize;
    //adding 1 more chunk if chunk size do not divide filesize completely
    if((metadata.fileSize % chunkSize) > 0 )
    {
        metadata.noOfChunks++;
    }
    // cout << "No of chunks: " << metadata.noOfChunks << endl; 

    //calculating sha1 hash

    int fd = open(filePath.c_str(), O_RDONLY);

    if(fd == -1)
    {
        perror("Unable to open file");
        return;
    }

    //once file is opened, read the contents in small chunks of size 512 bytes
    unsigned char buffer[BUFFERSIZE];
    int bytesRead;
    int count = 0; 
    string concatenatedHash = "";
    while (count < metadata.noOfChunks)
    {
        //reading chunk
        bytesRead = read(fd, buffer, sizeof(buffer));
        if (bytesRead == -1) 
        {
            perror("there was a error in reading the data from file");
            return;
        }
        string chunkHash = calculateSHA1Hash(buffer, bytesRead);
        metadata.hashOfChunks.push_back(chunkHash);
        concatenatedHash += chunkHash;  // Concatenate the hash of all chunks
        // cout << "Sending chunk " << Count << " to client." << endl;  
        // sleep(1);  
        count++;
    }

    //converting string hash back to byte array hash
    vector<unsigned char> concatenatedByteArray = hexStringToByteArray(concatenatedHash);
    //using data() gives you a pointer that points to the first element of the array inside the vector
    metadata.HashOfCompleteFile = calculateSHA1Hash(concatenatedByteArray.data(), concatenatedByteArray.size());

    close(fd);
    
    command += " " + to_string(metadata.fileSize) + " " + metadata.fileName + " " + to_string(metadata.noOfChunks) + " " + metadata.HashOfCompleteFile;

    // cout<<command<<endl;

    //updating map to store file path for easy search when client ask for that file
    // cout<<filePath<<endl;
    fnameToPath[metadata.fileName] = filePath;
}


string ConvertToHex(unsigned char c) 
{
    string hex;
    char chars[3];            //A char array of size 3 to hold 2 hexadecimal digits and a null-terminator
    snprintf(chars, sizeof(chars), "%02x", c);  //snprintf prints the hexadecimal representation of c into chars
    hex.append(chars);                    //Appends the converted hexadecimal characters to the string hex
    return hex;
}

string calculateSHA1Hash(unsigned char* buffer, int size) 
{
    unsigned char hash[SHA_DIGEST_LENGTH];            // This array of size 20 bytes will hold the SHA-1 hash

    //SHA1 calls the OpenSSL SHA1 function to calculate the hash of buffer of length size, and store it in hash
    SHA1(buffer, size, hash);
    
    //SHA1() expects the data to be hashed to be of type const unsigned char*
    //it produces a hash in the form of a byte array, where each byte is an unsigned 8-bit integer like 10010011
    //as it is binary data we convert to to hwxadecimal representation as each byte (8 bits) can be represented by exactly two hexadecimal digits
    
    //this will hold the final hexadecimal representation of the hash
    string hashOfChunk;
    for(int i=0; i<SHA_DIGEST_LENGTH; i++) 
    {
        hashOfChunk += ConvertToHex(hash[i]);
    }
    
    return hashOfChunk;  
}

vector<unsigned char> hexStringToByteArray(string& hexString)
{
    vector<unsigned char> byteArray;

    for (int i=0; i<hexString.length(); i += 2) 
    {
        string byteString = hexString.substr(i, 2);
        char byte = (char) strtol(byteString.c_str(), NULL, 16);
        byteArray.push_back(byte);
    }

    return byteArray;
}

void print(vector<string>& str)
{
    for(auto v : str)
    {
        cout<<v<<endl;
    }
}









//when i type download command, i get response from tracker that contains userID and port number of clients that have that file, I tokenize it now every odd index in vector have port number of client having i file i want to download
//NOTE: every client as well as tracker is on local host 127.0.0.1
//for downlloading first im implementing it without leecher concept, in my simple implementation multiple client can download files at once but from only 1 client only and that client will have complete file and once complete file is transfered then we check sha of complete file with the sha provided by the client and if its equal we update tracker that now we also have file,