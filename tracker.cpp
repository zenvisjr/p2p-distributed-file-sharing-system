#include "env_utils.h"
#include <iostream>
#include <random>
#include <thread>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <fcntl.h>       //for open()
#include <openssl/sha.h> //for hashing
#include <arpa/inet.h> // Needed for inet_pton
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <mutex>
#include <fstream>
#include <sstream>

using namespace std;

string loadBalancerIP = envStr("LOAD_BALANCER_IP", "127.0.0.1");
int loadBalancerPort = envInt("LOAD_BALANCER_PORT", 9000);
int BUFFERSIZE = envInt("BUFFERSIZE", 512 * 1024);
int MAX_CONNECTION = envInt("MAX_CONNECTIONS", 50);
int TRACKER_RCV_TO = envInt("TRACKER_RECV_TIMEOUT_MS", 5000);
int TRACKER_SND_TO = envInt("TRACKER_SEND_TIMEOUT_MS", 5000);


mutex userMutex;      // 🔒 [Phase 3]
mutex groupMutex;     // 🔒 [Phase 3]
mutex downloadsMutex; // 🔒 [Phase 3]
mutex globalPeerStatsMutex; // 🔒 [Phase 3]

bool trackerRunning = true;
int myTrackerIndex;
vector<pair<string, int>> trackerList;
string currentUSerIP,
currentUserPort;

class User
{
public:
    string userID;
    string password;
    string IpAddress;  // The IP address from which the user has logged in, currently every user is running locally on localhost 127.0.0.1
    string portNumber; // The port number on which the user's client is running, different client have different port number
    bool isLoggedIn;   // A flag to indicate whether the user is currently logged in or not.

    User(string ID, string pswd)
    {
        userID = ID;
        password = pswd;
        isLoggedIn = false;
    }
};

unordered_map<string, User *> userMap; // Key: userID, Value: Pointer to User object

class FileMetadata
{
public:
    int fileSize; // uint64_t is a 64-bit unsigned integer, which can represent a much larger range of values
    string fileName;
    int noOfChunks;
    // vector<string> hashOfChunks;
    string HashOfCompleteFile;
    vector<User *> peersHavingFile;
    vector<string> hashOfChunks;
    FileMetadata(uint64_t fsize, string &fname, int fchunks, string &fhash, vector<string> &chash, User *fileOwner)
    {
        fileSize = fsize;
        fileName = fname;
        noOfChunks = fchunks;
        // hashOfChunks = chash;
        HashOfCompleteFile = fhash;
        peersHavingFile.push_back(fileOwner);
        hashOfChunks = chash; // Store the hashes of each chunk
    }
};
// unordered_map<string, FileMetadata*> metadataMap;   //Key : HashOfCompleteFile , value : pointer to FileMetadata Object

class Group
{
public:
    string groupID;
    User *owner;
    vector<User *> groupMembers;
    vector<User *> pendingMembers;
    unordered_map<string, FileMetadata *> sharedFiles;
    // unordered_map<string, vector<FileMetadata*>> sharedFiles;
    // vector<FileMetadata* > sharedFiles;

    Group(string ID, User *currentUser)
    {
        groupID = ID;
        owner = currentUser;
        groupMembers.push_back(currentUser);
    }
};

unordered_map<string, Group *> groupMap; // Key: groupID, Value: Pointer to Group object

class DownloadInfo
{
public:
    string groupID;
    string fileName;
    bool status;

    DownloadInfo(string gid, string fname)
    {
        groupID = gid;
        fileName = fname;
        status = 0;
    }
};

vector<DownloadInfo *> downloads; // Holds the download statuses


// 📦 [Piece Selection]
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

unordered_map<string, PeerStats> globalPeerStats; // key = "ip:port"

bool CreateUser(string userID, string password);
bool LoginUser(string userID, string password, string currentUserIP, string currentUserPort, User *&currentThreadUser);
bool LogoutUser(User *&currentThreadUser);

void CreateGroup(string groupID, User *&currentThreadUser, int clientSocket, int check);
void JoinGroup(string groupID, User *&currentThreadUser, int clientSocket, int check);
void LeaveGroup(string groupID, User *&currentThreadUser, int clientSocket, int check);
void ListAllGroups(User *&currentThreadUser, int clientSocket, int check);
void ListPendingRequest(string groupID, User *&currentThreadUser, int clientSocket, int check);
void AcceptRequest(string groupID, string userID, User *&currentThreadUser, int clientSocket, int check);
void UploadFile(string groupID, int fsize, string fname, int fchunks, string fhash, User *&currentThreadUser, int clientSocket, int check);
void ListSharableFiles(string groupID, User *&currentThreadUser, int clientSocket, int check);
void StopShare(string groupID, string fileName, User *&currentThreadUser, int clientSocket, int check);
void DownloadFile(string groupID, string fileName, User *&currentThreadUser, int clientSocket, int check);
void ShowDownloadStatus(User *&currentThreadUser, int clientSocket, int check);
void QuitTracker(User *currentThreadUser, int clientSocket, int check);
void updatePeerStatsFromClient(const string &ip, int port, float score,
                               int served, double time);
string serializePeerStats(const PeerStats &stats);
vector<string> tokenizeVector(string &str);
void syncListener(int syncPort);
void sendSyncToPeers(const string &message, int selfIndex);
void handleSyncConnection(int syncSocket);
void notifyLoadBalancer(const string &command);
void registerToLoadBalancer(string tracker_ip, int tracker_port);

bool CreateUser(string userID, string password) {
  lock_guard<mutex> lock(userMutex); // 🔒 [Phase 3]
  // To check if a user exists
  if (userMap.find(userID) != userMap.end()) {
    // User* existingUser = userMap["john_doe"];
    cout << "User already exist, try another userID" << endl;
    return false;
  } else {
    // Check for valid userId and password
    //     if (userId.empty() || password.empty())
    //     {
    //         std::cout << "UserID and password cannot be empty" << std::endl;
    //         return;
    //     }
    // }
    // adding a new user
    User *newUser = new User(userID, password);
    userMap[userID] = newUser;

    // User *newUser1 = new User("ayush", "1q2w");
    // User *newUser2 = new User("naruto", "1q2w");
    // userMap[userID] = newUser1;
    // userMap[userID] = newUser2;
    cout << "New user created with ID : " << userID << endl;

    return true;
  }
}

bool LoginUser(string userID, string password, string currentUserIP, string currentUserPort, User *&currentThreadUser)
{
    lock_guard<mutex> lock(userMutex); // 🔒 [Phase 3]

    // To check if a user exists or not
    if (userMap.find(userID) == userMap.end())
    {
        cout << "User do not exist, try another userID" << endl;
        return false;
    }
    else if (userMap[userID]->isLoggedIn == true)
    {
        cout << "User is already logged in" << endl;
        return false;
    }
    else if (userMap[userID]->password == password)
    {
        userMap[userID]->isLoggedIn = true;
        userMap[userID]->IpAddress = currentUserIP;
        userMap[userID]->portNumber = currentUserPort;

        // cout<<"After logging in"<<endl;
        // cout<<userMap[userID]->isLoggedIn<<endl;
        // cout<<userMap[userID]->IpAddress<<endl;
        // cout<<userMap[userID]->portNumber <<endl;

        // VERY IMPORTANT: mapping currentThreadUser to same userID so that we can track user of current client
        currentThreadUser = userMap[userID];

        // // replace userID with IP later when sending peer list to client
        string key = userID + ":" + userMap[userID]->portNumber;
        uniform_real_distribution<float> scoreDist(0.1, 1.0);
        default_random_engine gen;
        float score = scoreDist(gen);
        if (globalPeerStats.find(key) == globalPeerStats.end()) {
          globalPeerStats[key] =
              PeerStats(userID, stoi(userMap[userID]->portNumber));
          globalPeerStats[key].score = score;
        }

        // 🛰️ Sync login status to all trackers (now includes IP and port)
        string syncCommand = "SYNC_LOGIN_USER|" + userID + "|" + currentUserIP +
                             "|" + currentUserPort + "|" + to_string(score);
        sendSyncToPeers(syncCommand, myTrackerIndex);

        cout << userID << " is logged in now" << endl;
        return true;
    }
    else
    {
        cout << "password do not match" << endl;
        return false;
    }
}

bool LogoutUser(User *&currentThreadUser)
{
    // To check if a user exists or not
    if (currentThreadUser == NULL)
    {
        cout << "User is not logged in yet" << endl;
        return false;
    }
    else if (currentThreadUser->isLoggedIn == false)
    {
        cout << "User is already logged out" << endl;
        return false;
    } else {
      string userID = currentThreadUser->userID;
      string userKey = userID + ":" + currentThreadUser->portNumber;

      // Remove from globalPeerStats
      {
        lock_guard<mutex> lock(globalPeerStatsMutex); // 🔒 [Phase 3]
        if (globalPeerStats.find(userKey) != globalPeerStats.end()) {
          globalPeerStats.erase(userKey);
          cout << "🗑️ Removed " << userKey << " from globalPeerStats" << endl;
        }
      }

      // Remove from peersHavingFile in all groups
      {
        lock_guard<mutex> lock(groupMutex); // 🔒 [Phase 3]
        for (auto &[gid, group] : groupMap) {
          for (auto &[fhash, meta] : group->sharedFiles) {
            auto &peers = meta->peersHavingFile;
            peers.erase(remove_if(peers.begin(), peers.end(),
                                  [&](User *u) { return u->userID == userID; }),
                        peers.end());
          }
        }
        cout << "🗑️ Removed user " << userID
             << " from all shared files in groups" << endl;
      }
        currentThreadUser->isLoggedIn = false;
        currentThreadUser->IpAddress = "";
        currentThreadUser->portNumber = "";

        // cout<<"After logging out"<<endl;
        // cout<<currentThreadUser->isLoggedIn<<endl;
        // cout<<currentThreadUser->IpAddress<<endl;
        // cout<<currentThreadUser->portNumber <<endl;

        cout << currentThreadUser->userID << " is logged out" << endl;

        cout << "assessing currentThreadUser" << endl;
        string syncCommand = "SYNC_LOGOUT_USER|" + currentThreadUser->userID;
        sendSyncToPeers(syncCommand, myTrackerIndex);

        // mapping currentThreadUser to NULL as the cuer of current client is logged out
        currentThreadUser = NULL;

        return true;
    }
}

void CreateGroup(string groupID, User *&currentThreadUser, int clientSocket, int check)
{
    lock_guard<mutex> lock(groupMutex); // 🔒 [Phase 3]
    string response;
    if (check == 0)
    {
        response = "First create a user";
        cout << response << endl;
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    // checking if user is logged in or not
    else if (currentThreadUser == NULL || currentThreadUser->isLoggedIn == false)
    {
        response = "User is not logged in yet";
        cout << response << endl;
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    // if user is logged in we check if a group already exists
    else if (groupMap.find(groupID) != groupMap.end())
    {
        response = "Group already exist, try another GroupID";
        cout << response << endl;
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    // if group do not exist we create one
    else
    {
        Group *newGroup = new Group(groupID, currentThreadUser);
        groupMap[groupID] = newGroup;
        cout << groupID << " group created successfully" << endl;

        response = "Group created successfully";
        send(clientSocket, response.c_str(), response.size(), 0);

        // 🔄 [Phase 3] Sync to all trackers
        string syncCommand =
            "SYNC_CREATE_GROUP|" + groupID + "|" + currentThreadUser->userID;
        sendSyncToPeers(syncCommand, myTrackerIndex);
    }
}

void JoinGroup(string groupID, User *&currentThreadUser, int clientSocket, int check)
{
    lock_guard<mutex> lock(groupMutex); // 🔒 [Phase 3]
    string response;
    Group *currentGroup = groupMap[groupID]; // currentGroup points to the same group that groupID points to

    // checking if user exist or not
    if (check == 0)
    {
        cout << "No user exist" << endl;
        response = "FIRST CREATE A USER";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    // checking if user is logged in or not
    else if (currentThreadUser == NULL || currentThreadUser->isLoggedIn == false)
    {
        cout << "User is not logged in yet" << endl;
        response = "LOGIN FIRST!!!!!!!";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    // // cout<<"hyy"<<endl;
    // else if(groupMap.empty())
    // {
    //     cout<<"No groups available to join"<<endl;
    //     response = "Create a group to join";
    //     send(clientSocket, response.c_str(), response.size(), 0);
    // }
    // if user is logged in we check if a group already exists or not
    else if (currentGroup == NULL)
    {
        response = "Group do not exist, try another GroupID";
        cout << response << endl;
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    // if group exist then check if member is already present
    else if (find(currentGroup->groupMembers.begin(), currentGroup->groupMembers.end(), currentThreadUser) != currentGroup->groupMembers.end())
    {
        cout << "you are already part of the " << groupID << " group" << endl;
        response = "you are already part of the group";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    // if group exist then check if members is already in pending request
    else if (find(currentGroup->pendingMembers.begin(), currentGroup->pendingMembers.end(), currentThreadUser) != currentGroup->pendingMembers.end())
    {
        cout << "you are currently in pending list of " << groupID << " group" << endl;
        response = "Already sent join request to this group";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    // if member is not present in group with no pending request then we add it to pending list of join request
    else
    {
        currentGroup->pendingMembers.push_back(currentThreadUser);
        cout << "Request sent to group owner for joining group" << endl;
        response = "Request Sent! Waiting For Owner's Approval...";
        send(clientSocket, response.c_str(), response.size(), 0);

        // 🔄 [Phase 3] Sync join request to all trackers
        string syncCommand =
            "SYNC_JOIN_GROUP|" + groupID + "|" + currentThreadUser->userID;
        sendSyncToPeers(syncCommand, myTrackerIndex);
    }
}

void LeaveGroup(string groupID, User *&currentThreadUser, int clientSocket, int check)
{
    lock_guard<mutex> lock(groupMutex); // 🔒 [Phase 3]
    string response;
    Group *currentGroup = groupMap[groupID]; // currentGroup points to the same group that groupID points to

    // if user is logged in we check if a group already exists or not
    if (currentGroup == NULL)
    {
        response = "Group do not exist, try another GroupID";
        cout << response << endl;
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    // checking if user exist or not
    else if (check == 0)
    {
        cout << "No user exist" << endl;
        response = "FIRST CREATE A USER";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    // checking if user is logged in or not
    else if (currentThreadUser == NULL || currentThreadUser->isLoggedIn == false)
    {
        cout << "User is not logged in yet" << endl;
        response = "LOGIN FIRST!!!!!!!";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    // // cout<<"hyy"<<endl;
    // else if(groupMap.empty())
    // {
    //     cout<<"No groups available to join"<<endl;
    //     response = "Create a group to join";
    //     send(clientSocket, response.c_str(), response.size(), 0);
    // }
    // if group exist then check if members is already in pending request
    else if (find(currentGroup->pendingMembers.begin(), currentGroup->pendingMembers.end(), currentThreadUser) != currentGroup->pendingMembers.end())
    {
        cout << "you are currently in pending list of " << groupID << " group" << endl;
        response = "Your join request is still not accepted by group admin";
        send(clientSocket, response.c_str(), response.size(), 0);
    }

    // if group exist and not in pending list then check if member is already present
    else if (find(currentGroup->groupMembers.begin(), currentGroup->groupMembers.end(), currentThreadUser) == currentGroup->groupMembers.end())
    {
        cout << "you are not part of the " << groupID << " group" << endl;
        response = "you do not belong to the group";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    // if member is present in group with no pending request then we check if member is admin of that group or not
    else if (currentGroup->owner == NULL || currentGroup->owner->userID != currentThreadUser->userID)
    {
        // finding index of the member in the list
        auto deleteIndex = find(currentGroup->groupMembers.begin(), currentGroup->groupMembers.end(), currentThreadUser);

        // deleting the index
        currentGroup->groupMembers.erase(deleteIndex);

        sendSyncToPeers("SYNC_LEAVE|" + groupID + "|" +
                            currentThreadUser->userID,
                        myTrackerIndex);

        
        cout << "You have been removed from the " << groupID << " group successfully." << endl;
        response = "Removed from Group Successfully!";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    // you are the admin of the group and only member present
    else if (currentGroup->groupMembers.size() == 1)
    {
        delete currentGroup;
        groupMap.erase(groupID);

        sendSyncToPeers("SYNC_DELETE_GROUP|" + groupID, myTrackerIndex);


        cout << "The admin left the group so it has been deleted!" << endl;
        response = "Deleted Group Successfully!";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    // there are more than one members in your group
    else
    {
      // select new owner
      User *newOwner = nullptr;
      for (auto member : currentGroup->groupMembers) {
        if (member->userID != currentThreadUser->userID) {
          newOwner = member;
          break;
        }
      }

      currentGroup->owner = newOwner;

      auto deleteIndex =
          find(currentGroup->groupMembers.begin(),
               currentGroup->groupMembers.end(), currentThreadUser);
      currentGroup->groupMembers.erase(deleteIndex);

        sendSyncToPeers("SYNC_TRANSFER_ADMIN|" + groupID + "|" +
                            newOwner->userID + "|" + currentThreadUser->userID,
                        myTrackerIndex);

      cout << "Admin removed and ownership of group transferred to "
           << newOwner->userID << endl;
      response = "Admin removed from Group Successfully!";
      send(clientSocket, response.c_str(), response.size(), 0);
    }
}

void ListAllGroups(User *&currentThreadUser, int clientSocket, int check)
{
    lock_guard<mutex> lock(groupMutex); // 🔒 [Phase 3]
    string response = "";
    // //vector to store every group that exist
    // vector<string> allGroups;
    // cout<<"entering"<<endl;

    // checking if user exist or not
    if (check == 0)
    {
        cout << "No user exist" << endl;
        response = "FIRST CREATE A USER";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    // checking if user is logged in or not
    else if (currentThreadUser == NULL || currentThreadUser->isLoggedIn == false)
    {
        cout << "User is not logged in yet" << endl;
        response = "LOGIN FIRST!!!!!!!";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    // checking if groupMap is empty or not
    else if (groupMap.empty())
    {
        cout << "No group exist" << endl;
        response = "No group created till now";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    // printing all groups ID sending it to client
    else
    {
        // sending size of the unordered map to client
        //  int noOfGroups = groupMap.size();
        //  response += "#" + to_string(noOfGroups);
        //  cout<<response<<endl;

        for (auto key : groupMap)
        {
            cout << key.first << endl;
            // cout<<"response: "<<response<<endl;
            response += '#' + key.first;
        }
        // response += '#';
        // cout<<response<<endl;
        send(clientSocket, response.c_str(), response.size(), 0);

        // response = "All groups are displayed successfully";
        // send(clientSocket, response.c_str(), response.size(), 0);
    }
}

void ListPendingRequest(string groupID, User *&currentThreadUser, int clientSocket, int check)
{
    lock_guard<mutex> lock(groupMutex); // 🔒 [Phase 3]
    string response;
    Group *currentGroup = groupMap[groupID];

    // checking if user exist or not
    if (check == 0)
    {
        cout << "No user exist" << endl;
        response = "FIRST CREATE A USER";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    // checking if user is logged in or not
    else if (currentThreadUser == NULL || currentThreadUser->isLoggedIn == false)
    {
        cout << "User is not logged in yet" << endl;
        response = "LOGIN FIRST!!!!!!!";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    // checking if groupMap is empty or not
    else if (currentGroup == NULL)
    {
        cout << groupID << " Group do not exist" << endl;
        response = "Group do not exist";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    // seeing if the requesting user is the owner of the group
    else if (currentGroup->owner != currentThreadUser)
    {
        cout << "Only Owner can see Pending Request!" << endl;
        response = "Only admin of the group is authorised to see pending request";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    // seeing if pendingMembers is empty or not
    else if (currentGroup->pendingMembers.size() == 0)
    {
        cout << "There's no one waiting for approval!!" << endl;
        response = "There's no one waiting for approval!!";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    // listing all pending request
    else
    {
        cout << "Listing All Pending Requests..." << endl;
        // //creating a new vector to store userID of pending request
        // vector<string> pendingUserId;
        int pendingListSize = currentGroup->pendingMembers.size();

        // iterating over the pending list and printing user ID
        for (int i = 0; i < pendingListSize; i++)
        {
            cout << currentGroup->pendingMembers[i]->userID << endl;
            response += ' ' + currentGroup->pendingMembers[i]->userID;
            // pendingUserId.push_back(currentGroup->pendingMembers[i]->userID);
        }
        // cout<<response<<endl;
        send(clientSocket, response.c_str(), response.size(), 0);
    }
}

void AcceptRequest(string groupID, string userID, User *&currentThreadUser, int clientSocket, int check)
{
    lock_guard<mutex> lock(groupMutex); // 🔒 [Phase 3]
    string response;
    Group *currentGroup = groupMap[groupID];

    // checking if group exist or not
    if (currentGroup == NULL)
    {
        cout << groupID << " Group do not exist" << endl;
        response = "Group do not exist";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    // checking if user exist or not
    else if (check == 0)
    {
        cout << "No user exist" << endl;
        response = "FIRST CREATE A USER";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    // checking if user is logged in or not
    else if (currentThreadUser == NULL || currentThreadUser->isLoggedIn == false)
    {
        cout << "User is not logged in yet" << endl;
        response = "LOGIN FIRST!!!!!!!";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    // seeing if the requesting user is the owner of the group
    else if (currentGroup->owner != currentThreadUser)
    {
        cout << "Only Owner can accept Pending Request!" << endl;
        response = "Only Admin of the group is authorised to accept pending request";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    else
    {
        bool isPending = false;
        // finding userID in pending list
        for (auto pendingUser : currentGroup->pendingMembers)
        {
            if (pendingUser->userID == userID)
            {
                isPending = true;
                // add this user as a member to the group
                currentGroup->groupMembers.push_back(pendingUser);

                // remove this user from pending list
                auto pendingIndex = find(currentGroup->pendingMembers.begin(), currentGroup->pendingMembers.end(), pendingUser);
                currentGroup->pendingMembers.erase(pendingIndex);

                // 🔁 [Phase 2] Sync accepted request to other trackers
                string syncCommand =
                    "SYNC_ACCEPT_REQUEST|" + groupID + "|" + userID;
                sendSyncToPeers(syncCommand, myTrackerIndex); // ✨ already implemented

                cout << "Accepted your request" << endl;
                response = "Added to group successfully! WELCOME!!";
                send(clientSocket, response.c_str(), response.size(), 0);



                // replace userID with IP later when sending peer list to client
                // string key = userID + ":" + userMap[userID]->portNumber;
                // if (globalPeerStats.find(key) == globalPeerStats.end()) {
                //   globalPeerStats[key] =
                //       PeerStats(userID, stoi(userMap[userID]->portNumber));
                // }
                // //printing pending list user
                // ListPendingRequest(groupID, currentThreadUser, clientSocket, check);

                // //printing group members
                // cout<<"current group members:"<<endl;
                // for(auto gm : currentGroup->groupMembers)
                // {
                //     cout<<gm->userID<<endl;
                // }
                break;
            }
        }
        if (isPending == false)
        {
            cout << userID << " is not in pending list of " << groupID << endl;
            response = "USER NOT IN PENDING LIST OF GROUP";
            send(clientSocket, response.c_str(), response.size(), 0);
        }
    }
}

void UploadFile(string groupID, int fsize, string fname, int fchunks, string fhash, vector<string> &chunkHashes, User *&currentThreadUser, int clientSocket, int check)
{
    lock_guard<mutex> lock(groupMutex); // 🔒 [Phase 3]
    string response;
    Group *currentGroup = groupMap[groupID];

    // checking if user exist or not
    if (check == 0)
    {
        cout << "No user exist" << endl;
        response = "FIRST CREATE A USER";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    // checking if user is logged in or not
    else if (currentThreadUser == NULL || currentThreadUser->isLoggedIn == false)
    {
        cout << "User is not logged in yet" << endl;
        response = "LOGIN FIRST!!!!!!!";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    // checking if group exist or not
    else if (currentGroup == NULL)
    {
        cout << groupID << " Group do not exist" << endl;
        response = "Group do not exist";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    // checking if the user who sent the request is a member of the group
    else if (find(currentGroup->groupMembers.begin(), currentGroup->groupMembers.end(), currentThreadUser) == currentGroup->groupMembers.end())
    {
        cout << "you are not part of the " << groupID << " group" << endl;
        response = "UNAUTHORISED ACCESS!!! You do not belong to the group";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    // else if(currentGroup->sharedFiles.find(fhash) != currentGroup->sharedFiles.end())
    // {
    //     cout<<fname<<" file already exist in the group"<<endl;
    //     response = "File already exist in the group";
    //     send(clientSocket, response.c_str(), response.size(), 0);
    // }
    // else if(metadataMap.find(fhash) != metadataMap.end())
    // {
    //     cout<<fname<<" file already exist in the network"<<endl;
    //     response = "File already exist in the network";
    //     send(clientSocket, response.c_str(), response.size(), 0);
    // }
    // checking if u have already uploaded that file otherwise uploading it
    else
    {
        auto file = currentGroup->sharedFiles.find(fhash);

        // if file already exist then check if u have uploaded that file
        if (file != currentGroup->sharedFiles.end())
        {
            // bool check = false;
            for (auto peer : file->second->peersHavingFile)
            {

                if (peer->userID == currentThreadUser->userID)
                {
                    // check = true;
                    cout << "You have already shared this file" << endl;
                    response = fname + " is already shared by u";
                    send(clientSocket, response.c_str(), response.size(), 0);
                    return;
                }
            }
            // the file already exist but you have not uploaded it yet so add ur entry in peers
            file->second->peersHavingFile.push_back(currentThreadUser);
        }
        // file do not exist then upload it
        else
        {
            FileMetadata *newfile = new FileMetadata(fsize, fname, fchunks, fhash, chunkHashes, currentThreadUser);
            // newfile->hashOfChunks = chunkHashes;
            cout << "uploaded the file metadata" << endl;
            currentGroup->sharedFiles[fhash] = newfile;
            // currentGroup->sharedFiles[fhash].push_back(newfile);
        }

        // 🛰️ [SYNC] Send upload metadata to other trackers
        string syncMsg = "SYNC_UPLOAD_FILE|" + groupID + "|" +
                         to_string(fsize) + "|" + fname + "|" +
                         to_string(fchunks) + "|" + fhash + "|" +
                         currentThreadUser->userID;

        for (const string &chunkHash : chunkHashes) {
          syncMsg += "|" + chunkHash;
        }

        sendSyncToPeers(syncMsg, myTrackerIndex); // 🌐
        cout << "New file " << fname << " Uploaded by " << currentThreadUser->userID << " of Size " << fsize << " in " << groupID << " Group" << endl;
        response = "File shared in group successfully";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
}

void ListSharableFiles(string groupID, User *&currentThreadUser, int clientSocket, int check)
{
    lock_guard<mutex> lock(groupMutex); // 🔒 [Phase 3]
    string response;
    Group *currentGroup = groupMap[groupID];

    // checking if user exist or not
    if (check == 0)
    {
        cout << "No user exist" << endl;
        response = "FIRST CREATE A USER";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    // checking if user is logged in or not
    else if (currentThreadUser == NULL || currentThreadUser->isLoggedIn == false)
    {
        cout << "User is not logged in yet" << endl;
        response = "LOGIN FIRST!!!!!!!";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    // checking if group exist or not
    else if (currentGroup == NULL)
    {
        cout << groupID << " Group do not exist" << endl;
        response = "Group do not exist";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    // checking if the user who sent the request is a member of the group
    else if (find(currentGroup->groupMembers.begin(), currentGroup->groupMembers.end(), currentThreadUser) == currentGroup->groupMembers.end())
    {
        cout << "you are not part of the " << groupID << " group" << endl;
        response = "UNAUTHORISED ACCESS!!! You do not belong to the group";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    // seeing if there is any file  exist to share
    else if (currentGroup->sharedFiles.size() == 0)
    {
        cout << groupID << "group do not have any sharable file." << endl;
        response = "There's no file sharable !!!!";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    else
    {
        string shareFiles;
        cout << "LIST OF ALL SHARABLE FILES IN " << groupID << " GROUP" << endl;
        for (auto fname : currentGroup->sharedFiles)
        {
            cout << fname.second->fileName << endl;
            shareFiles += " " + fname.second->fileName;
        }
        // cout<<shareFiles<<endl;
        response = shareFiles;
        // response = "Here are all files that can be shared with you : ";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
}

void StopShare(string groupID, string fname, User *&currentThreadUser, int clientSocket, int check)
{
    lock_guard<mutex> lock(groupMutex); // 🔒 [Phase 3]
    string response;
    Group *currentGroup = groupMap[groupID];

    // checking if user exist or not
    if (check == 0)
    {
        cout << "No user exist" << endl;
        response = "FIRST CREATE A USER";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    // checking if user is logged in or not
    else if (currentThreadUser == NULL || currentThreadUser->isLoggedIn == false)
    {
        cout << "User is not logged in yet" << endl;
        response = "LOGIN FIRST!!!!!!!";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    // checking if group exist or not
    else if (currentGroup == NULL)
    {
        cout << groupID << " Group do not exist" << endl;
        response = "Group do not exist";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    // checking if the user who sent the request is a member of the group
    else if (find(currentGroup->groupMembers.begin(), currentGroup->groupMembers.end(), currentThreadUser) == currentGroup->groupMembers.end())
    {
        cout << "you are not part of the " << groupID << " group" << endl;
        response = "UNAUTHORISED ACCESS!!! You do not belong to the group";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    // seeing if sharedFiles list is empty
    else if (currentGroup->sharedFiles.size() == 0)
    {
        cout << groupID << "group do not have any sharable file." << endl;
        response = "There's no file sharable !!!!";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    // checking if file that we want to stop share exist or not and if exist then deleting it
    //  else if(currentGroup->sharedFiles.find(fhash) == currentGroup->sharedFiles.end())
    else
    {
        string fhash;
        bool check = false;
        for (auto index : currentGroup->sharedFiles)
        {
            if (index.second->fileName == fname)
            {
                check = true;
                fhash = index.first;
                // deleting the file
                currentGroup->sharedFiles.erase(fhash);

                sendSyncToPeers("SYNC_STOP_SHARE|" + groupID + "|" + fhash,
                                myTrackerIndex); // ✅ [Sync]

                
                cout << fname << " removed from shared file list of the" << groupID << " group" << endl;
                response = fname + " stopped sharing successfully";
                send(clientSocket, response.c_str(), response.size(), 0);
                break;
            }
        }
        if (check == false)
        {
            cout << fname << " file do not exist in the group" << endl;
            response = fname + " do not exist in the group";
            send(clientSocket, response.c_str(), response.size(), 0);
        }
    }
}

void DownloadFile(string groupID, string fname, User *&currentThreadUser, int clientSocket, int check)
{
    lock_guard<mutex> lock(groupMutex); // 🔒 [Phase 3]
    string response;
    Group *currentGroup = groupMap[groupID];

    // checking if user exist or not
    if (check == 0)
    {
        cout << "No user exist" << endl;
        response = "FIRST CREATE A USER";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    // checking if user is logged in or not
    else if (currentThreadUser == NULL || currentThreadUser->isLoggedIn == false)
    {
        cout << "User is not logged in yet" << endl;
        response = "LOGIN FIRST!!!!!!!";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    // checking if group exist or not
    else if (currentGroup == NULL)
    {
        cout << groupID << " Group do not exist" << endl;
        response = "Group do not exist";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    // checking if the user who sent the request is a member of the group
    else if (find(currentGroup->groupMembers.begin(), currentGroup->groupMembers.end(), currentThreadUser) == currentGroup->groupMembers.end())
    {
        cout << "you are not part of the " << groupID << " group" << endl;
        response = "UNAUTHORISED ACCESS!!! You do not belong to the group";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    // seeing if sharedFiles list is empty
    else if (currentGroup->sharedFiles.size() == 0)
    {
        cout << groupID << "group do not have any sharable file." << endl;
        response = "There's no file to dowload from group " + groupID + " !!!!";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    // checking if filename exist in the sharable file list
    else
    {
        string fhash;
        bool check = false;
        for (auto index : currentGroup->sharedFiles)
        {
            if (index.second->fileName == fname)
            {
                check = true;
                
                fhash = index.first;

                FileMetadata *meta = index.second;

                response = "OK ";
                response += to_string(meta->fileSize) + " ";
                response += to_string(meta->noOfChunks) + " ";
                response += meta->HashOfCompleteFile + " ";
                

                // Append all chunk hashes
                for (const string &h : meta->hashOfChunks)
                {
                    cout << "Entering chunk hash loop" << endl;
                    cout << "h: " << h << endl;
                    response += h + " ";
                }

                // if file is found in the group, send all the peers that uploaded that file to the group
                string peers = "";
                for (auto p : meta->peersHavingFile) {

                  //replace userID with IP later
                  string key = p->userID + ":" + p->portNumber;
                //   PeerStats peerStats = globalPeerStats[key];
                  auto it = globalPeerStats.find(key);
                  if (it != globalPeerStats.end()) {
                    response += serializePeerStats(it->second) + " ";
                  }
                  cout << "peer: " << p->userID << " " << p->portNumber << endl;
                  //   response += " " + p->userID + " " + p->portNumber;
                }
                cout << response << endl;
                // response = peers;

                // updating downloadinfo
                //  lock_guard<mutex> lock(downloadMutex);
                DownloadInfo *newInfo = new DownloadInfo(groupID, fname);

                lock_guard<mutex> lock(downloadsMutex); // 🔒 [Phase 3]
                downloads.push_back(newInfo);

                string syncCreate = "SYNC_DOWNLOAD_INIT|" + groupID + "|" +
                                    fname + "|" + currentThreadUser->userID;
                sendSyncToPeers(syncCreate,
                                myTrackerIndex); // 🔄 Sync download init

                cout << "Sending metadata of size: " << response.size() << " bytes" << endl;

                // Send the size first (as fixed 4-byte int)
                int metadataLen = response.size();
                send(clientSocket, &metadataLen, sizeof(int), 0);


                // Then send the full metadata
                int totalSent = 0;
                while (totalSent < metadataLen)
                {
                    int sent = send(clientSocket, response.c_str() + totalSent, metadataLen - totalSent, 0);
                    if (sent <= 0)
                    {
                        perror("Failed to send full metadata");
                        break;
                    }
                    totalSent += sent;
                }
                cout << "Detail of peers and metadata send successfully"
                     << endl;

                // recieving response from client after file is downloaded successfully
                

                // recieve peer status
                uint32_t lenNet;
                int received = recv(clientSocket, &lenNet, sizeof(lenNet), 0);
                if (received != sizeof(lenNet)) {
                  cerr << "❌ Incomplete length field: expected "
                       << sizeof(lenNet) << " bytes, got " << received
                       << " bytes" << endl;
                  return;
                }
                int statsLen = ntohl(lenNet);

                cout << "recieve peer stats length: " << received << endl;
                cout << "recieve peer stats length in network byte order: "
                     << lenNet << endl;
                cout << "recieve peer stats length in host byte order: " << statsLen
                     << endl;
                // Step 2: Receive complete message of that length
                string statsData;
                int totalReceived = 0;
                char buffer[BUFFERSIZE];
                while (totalReceived < statsLen) {
                  int toRead =
                      min((int)sizeof(buffer),
                          statsLen - totalReceived); // ❗️ limit to remaining
                  int bytes =
                      recv(clientSocket, buffer, toRead, 0); // ✅ bounded recv
                  if (bytes <= 0) {
                    cerr << "❌ Connection closed while receiving stats"
                         << endl;
                    return;
                  }
                  statsData.append(buffer, bytes);
                  totalReceived += bytes;
                }
                cout << "total length received: " << totalReceived << endl;
                cout << "statsData: " << statsData << endl;

                if (totalReceived != statsLen) {
                  cerr << "❌ Incomplete stats message from client" << endl;
                  return;
                }

                // Step 3: Tokenize the entire response string
                vector<string> tokens = tokenizeVector(statsData);

                // Step 4: Extract stats in groups of 6
                bool downloadComplete = false;
                for (int i = 0; i + 5 < int(tokens.size()); i += 6) {
                  string command = tokens[i];
                  if (command != "update_peer_stats") {
                    cerr << "❌ Invalid peer stats command received: "
                         << command << endl;
                    continue;
                  }
                  

                  string peerIP = tokens[i + 1];
                  int peerPort = stoi(tokens[i + 2]);
                  float score = stof(tokens[i + 3]);
                  int chunksServed = stoi(tokens[i + 4]);
                  double totalTime = stod(tokens[i + 5]);

                  string key = peerIP + ":" + to_string(peerPort);

                  if (globalPeerStats.find(key) == globalPeerStats.end()) {
                    globalPeerStats[key] = PeerStats(peerIP, peerPort);
                  }

                  PeerStats &peer = globalPeerStats[key];
                  peer.score = score;
                  peer.chunksServed = chunksServed;
                  peer.totalDownloadTime = totalTime;

                  cout << "📥 Updated tracker stats for " << key
                       << ": score=" << score << ", chunks=" << chunksServed
                       << ", time=" << totalTime << endl;
                  downloadComplete = true;
                }

                // ❗️Step 5: Receive final download status (fix: don't use
                // leftover `buffer`)
                // char statusBuf[64];
                // int statusBytes =
                //     recv(clientSocket, statusBuf, sizeof(statusBuf), 0);
                // if (statusBytes > 0) {
                //   string receivedDownloadStatus(statusBuf, statusBytes);
                //   if (receivedDownloadStatus.find(
                //           "DownloadCompleteSuccessfully") != string::npos) {
                if (downloadComplete)
                {
                    lock_guard<mutex> lock(downloadsMutex); // 🔒 [Phase 3]
                    for (auto s : downloads) {
                      if (s->fileName == fname) {
                        s->status = 1;
                      }
                    }
                    string syncMsg = "SYNC_DOWNLOAD|" + groupID + "|" + fname +
                                     "|" + currentThreadUser->userID;
                    sendSyncToPeers(syncMsg,
                                    myTrackerIndex); // 🔄 Sync download status

                    cout << "✅ Tracker updated download status to complete "
                            "for file: "
                         << fname << endl;
                }
                // update tracker that I have the file too
                for (auto file : currentGroup->sharedFiles)
                {
                    if (file.second->fileName == fname)
                    {
                        auto it = find(file.second->peersHavingFile.begin(), file.second->peersHavingFile.end(), currentThreadUser);
                        if (it == file.second->peersHavingFile.end())
                        {
                          file.second->peersHavingFile.push_back(
                              currentThreadUser);

                          string sync = "SYNC_ADD_PEER|" + groupID + "|" +
                                        file.first + "|" +
                                        currentThreadUser->userID;
                          sendSyncToPeers(sync,
                                          myTrackerIndex); // ✅ Sync peer add
                        }
                    }
                }
                cout << "✅ Tracker updated file in the list" << endl;
                break;
            }
        }
        if (check == false)
        {
            cout << fname << " file do not exist in the group" << endl;
            response = fname + " do not exist in the group" + groupID;
            send(clientSocket, response.c_str(), response.size(), 0);
        }
    }
}

void ShowDownloadStatus(User *&currentThreadUser, int clientSocket, int check)
{
    string response;
    // Group *currentGroup = groupMap[groupID];

    // checking if user exist or not
    if (check == 0)
    {
        cout << "No user exist" << endl;
        response = "FIRST CREATE A USER";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    // checking if user is logged in or not
    else if (currentThreadUser == NULL || currentThreadUser->isLoggedIn == false)
    {
        cout << "User is not logged in yet" << endl;
        response = "LOGIN FIRST!!!!!!!";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    else if (downloads.size() == 0)

    {
        response = "no downloads till now";
        cout << response << endl;
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    else
    {

        for (auto d : downloads)
        {
            // string downloadInfo = (d.status ? "[D]" : "[C]") + " [" + d.groupID + "] " + d.fileName;
            string downloadInfo = string(d->status ? "D" : "C") + " " + d->groupID + " " + d->fileName;

            // cout<<downloadInfo<<endl;
            response += " " + downloadInfo;
        }
        // cout<<response<<endl;
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    // lockGuard<mutex> lock(downloadMutex);
}

void QuitTracker(User *currentThreadUser, int clientSocket, int check)
{
    string response;
    // Group *currentGroup = groupMap[groupID];

    // checking if user exist or not
    if (check == 0)
    {
        cout << "No user exist" << endl;
        response = "FIRST CREATE A USER";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    // checking if user is logged in or not
    else if (currentThreadUser == NULL || currentThreadUser->isLoggedIn == false)
    {
        cout << "User is not logged in yet" << endl;
        response = "LOGIN FIRST!!!!!!!";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    else
    {
        cout << "Tracker is shutdown" << endl;
        response = "Tracker is shutting down...";
        send(clientSocket, response.c_str(), response.size(), 0);
        trackerRunning = false;
        exit(0);
    }
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

void clientHandler(int clientSocket)
{
    // local thread varaible to track curent user that logs in
    User *currentThreadUser = NULL;
    int checkNoOfUser;
    int checkNoOfLogin;
    vector<string> command;

    while (trackerRunning)
    {
        cout<<"tracker is running"<<endl;

        char prefixBuffer[8] = {0};
        int prefixBytes = recv(clientSocket, prefixBuffer, 7, MSG_PEEK); // Look ahead

        if (prefixBytes == 7 && strncmp(prefixBuffer, "@LARGE@", 7) == 0)
        {
            cout << "Received a large message." << endl;
            // Consume prefix
            recv(clientSocket, prefixBuffer, 7, 0);

            // Read length
            size_t length;
            int lenBytes = recv(clientSocket, &length, sizeof(length), 0);
            if (lenBytes != sizeof(length))
            {
                cerr << "Failed to receive full message length." << endl;
                close(clientSocket);
                return;
            }

            // Receive full payload
            string payload(length, 0);
            size_t totalReceived = 0;
            while (totalReceived < length)
            {
                int bytes = recv(clientSocket, &payload[totalReceived], length - totalReceived, 0);
                if (bytes <= 0)
                {
                    perror("Failed to receive full large message");
                    close(clientSocket);
                    return;
                }
                totalReceived += bytes;
            }

            // Now handle the command normally
            command = ExtractArguments(payload);
            // ... (handle as usual)
        } else {
          cout<<"small message"<<endl;
            // Default case: normal small command
            char buffer[BUFFERSIZE];
            int bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
            if (bytesReceived <= 0)
            {
                cerr << "Client disconnected or error." << endl;
                close(clientSocket);
                return;
            }

            buffer[bytesReceived] = '\0'; // Only access buffer after successful recv

            // cout << "Data received: " << buffer << endl;

            // Safely converting char array to string
            string commandRecieved(buffer, bytesReceived);
            cout<<"commandRecieved: "<<commandRecieved<<endl;
            command = ExtractArguments(commandRecieved);
        }

        // command and its arguments are stored in a vector named arguments

        // int i;
        // for(auto i: command)
        // {
        //     cout<<i<<endl;
        // }

        // string command = "create_user ayush 1q2w3e";
        string response;

        if (command[0] == "PING") {
          //   cout<<"\nPING received from client "<<endl;
          string response = "PONG";
          //   cout << "\nPONG sent to client "<< endl;
          send(clientSocket, response.c_str(), response.size(), 0);
          continue;
        } else if (command[0] == "create_user")
        {
            // if(command.size() != 3)
            // {
            //     cout<<"USAGE: create_user <user_id> <passwd>"<<endl;
            //     response = "WRONG NUMBER OF ARGUMENTS: ENTER AGAIN ";
            //     send(clientSocket, response.c_str(), response.size(), 0);
            //     continue;

            // }
            // cout<<"Command Received : "<<commandRecieved.substr(0, 11)<<endl;

            string id = command[1];
            string pswd = command[2];

            // cout<<id<<endl;
            // cout<<pswd<<endl;
            if (checkNoOfUser == 1)
            {
                cout << "One user is already registered, creation of multiple users not allowed" << endl;
                response = "USER EXIST: A client can have max 1 user";
                send(clientSocket, response.c_str(), response.size(), 0);
                continue;
            }

            bool status = CreateUser(id, pswd);

            // sending response to client

            if (status == true)
            {
                response = "User created successfully";
                checkNoOfUser++;

                string syncMsg = "SYNC_CREATE_USER|" + id + "|" + pswd;
                sendSyncToPeers(syncMsg,
                                myTrackerIndex); // index of this tracker
            }
            else
            {
                response = "User already exists";
            }
            send(clientSocket, response.c_str(), response.size(), 0);

            // now sync it with other trackers
            // 📤 Send sync message

        }
        else if (command[0] == "login")
        {
            // if(command.size() != 3)
            // {
            //     cout<<"USAGE: login <user_id> <passwd>"<<endl;
            //     response = "WRONG NUMBER OF ARGUMENTS: ENTER AGAIN ";
            //     send(clientSocket, response.c_str(), response.size(), 0);
            //     continue;

            // }
            // cout<<"Command Received : "<<commandRecieved.substr(0, 5)<<endl;

            string id = command[1];
            string pswd = command[2];
            string ip = command[3];
            string port = command[4];

            // cout<<id<<endl;
            // cout<<pswd<<endl;
            // cout<<ip<<endl;
            // cout<<port<<endl;

            // checking if more than one user is trying to log in
            if (checkNoOfLogin > 0)
            {
                cout << "One user is already logged in, multiple users not allowed to log in" << endl;
                response = "USER LOGGED IN: A client can have max 1 logged in user";
                send(clientSocket, response.c_str(), response.size(), 0);
                continue;
            }
            bool status = LoginUser(id, pswd, ip, port, currentThreadUser);

            // sending response to client
            if (status == true)
            {
                response = "User login successfully";
                checkNoOfLogin++;

              
            }
            else
            {
                response = "Error logging in, try again";
            }
            // sending response back to client
            send(clientSocket, response.c_str(), response.size(), 0);
        }
        else if (command[0] == "logout")
        {
            // if(command.size() != 1)
            // {
            //     cout<<"USAGE: logout"<<endl;
            //     response = "WRONG NUMBER OF ARGUMENTS: ENTER AGAIN ";
            //     send(clientSocket, response.c_str(), response.size(), 0);
            //     continue;

            // }
            // cout<<"Command Received : "<<commandRecieved.substr(0, 6)<<endl;

            bool status = LogoutUser(currentThreadUser);

            // sending response to client
            if (status == true)
            {
                response = "User logged out successfully";

                // 🔄 [Phase 3] Sync logout status to all trackers
                // cout << "assessing currentThreadUser"<<endl;
                // string syncCommand = "SYNC_LOGOUT_USER|" + currentThreadUser->userID;
                // sendSyncToPeers(syncCommand, myTrackerIndex);
            }
            else
            {
                response = "Error logging in, try again";
            }
            // sending response back to client
            send(clientSocket, response.c_str(), response.size(), 0);
        }
        else if (command[0] == "create_group")
        {
            // cout<<"Command Received : "<<commandRecieved.substr(0, 12)<<endl;

            string id = command[1];

            // cout<<id<<endl;
            // cout<<pswd<<endl;
            // if(checkNoOfGroup == 1)
            // {
            //     cout<<"One group is already registered, creation of multiple group not allowed"<<endl;
            //     response = "GROUP EXIST: A user can be part of max 1 group";
            //     send(clientSocket, response.c_str(), response.size(), 0);
            //     continue;
            // }

            CreateGroup(id, currentThreadUser, clientSocket, checkNoOfUser);
            // checkNoOfGroup++;

            // sending response to client

            // if(status == true)
            // {
            //     response = "Group created successfully";
            // }
            // else
            // {
            //     response = "Group already exists";
            // }
            // send(clientSocket, response.c_str(), response.size(), 0);
        }
        else if (command[0] == "join_group")
        {
            // cout<<"Command Received : "<<commandRecieved.substr(0, 10)<<endl;
            string id = command[1];
            JoinGroup(id, currentThreadUser, clientSocket, checkNoOfUser);
        }
        else if (command[0] == "leave_group")
        {
            // cout<<"Command Received : "<<commandRecieved.substr(0, 11)<<endl;
            string id = command[1];
            LeaveGroup(id, currentThreadUser, clientSocket, checkNoOfUser);
        }
        else if (command[0] == "list_groups")
        {
            // cout<<"Command Received : "<<commandRecieved.substr(0, 11)<<endl;

            ListAllGroups(currentThreadUser, clientSocket, checkNoOfUser);
        }
        else if (command[0] == "list_requests")
        {
            // cout<<"Command Received : "<<commandRecieved.substr(0, 13)<<endl;
            string id = command[1];
            ListPendingRequest(id, currentThreadUser, clientSocket, checkNoOfUser);
        }
        else if (command[0] == "accept_request")
        {
            // cout<<"Command Received : "<<commandRecieved.substr(0, command[0].length())<<endl;
            string gid = command[1];
            string uid = command[2];
            AcceptRequest(gid, uid, currentThreadUser, clientSocket, checkNoOfUser);
        }
        else if (command[0] == "upload_file")
        {
            cout << "Entering upload_file command" << endl;

            string fpath = command[1];
            string gid = command[2];
            int fsize = stoi(command[3]);
            string fname = command[4];
            int fchunks = stoi(command[5]);
            string fhash = command[6];

            // 🚀 [Phase 4] Extract chunk hashes from remaining command elements
            vector<string> chunkHashes;
            for (int i = 7; i < int(command.size()); i++)
            {
                // cout << "each chunk hash: " << command[i] << endl;
                chunkHashes.push_back(command[i]);
            }

            UploadFile(gid, fsize, fname, fchunks, fhash, chunkHashes, currentThreadUser, clientSocket, checkNoOfUser);
        }
        else if (command[0] == "list_files")
        {
            // cout<<"Command Received : "<<commandRecieved.substr(0, command[0].length())<<endl;
            string gid = command[1];
            ListSharableFiles(gid, currentThreadUser, clientSocket, checkNoOfUser);
        }
        else if (command[0] == "stop_share")
        {
            // cout<<"Command Received : "<<commandRecieved.substr(0, command[0].length())<<endl;
            string gid = command[1];
            // string fpath = command[2];
            string fname = command[2];
            // string fhash = command[6];
            StopShare(gid, fname, currentThreadUser, clientSocket, checkNoOfUser);
        }
        else if (command[0] == "download_file")
        {
            // cout<<"Command Received : "<<commandRecieved.substr(0, command[0].length())<<endl;
            string gid = command[1];
            // string fpath = command[2];
            string fname = command[2];
            DownloadFile(gid, fname, currentThreadUser, clientSocket, checkNoOfUser);
        }
        else if (command[0] == "show_downloads")
        {
            // cout<<"Command Received : "<<commandRecieved.substr(0, command[0].length())<<endl;

            ShowDownloadStatus(currentThreadUser, clientSocket, checkNoOfUser);
        }
        else if (command[0] == "quit")
        {
            // cout<<"Command Received : "<<commandRecieved.substr(0, command[0].length())<<endl;

            QuitTracker(currentThreadUser, clientSocket, checkNoOfUser);
            // trackerRunning = false;
            // exit(0);
            // break;
        } 
        else
        {
            cout<<"wrong command"<<endl;
            string response = "The command u entered does not exists";
            send(clientSocket, response.c_str(), response.size(), 0);
        }

        // Close the client socket
        //  close(clientSocket);

        // return 0;
    }
    close(clientSocket);
    notifyLoadBalancer("DECREMENT");
}

int main(int argc, char *argv[])
{
    // checking if correct no of arguments were passed
    if (argc != 3)
    {
        perror("Usage : ./tracker tracker_info.txt tracker_no");
    }
    // extracting IP and port number of tracker from tracker_info.txt passed as argument
    string trackerInfoFile = argv[1];
     myTrackerIndex =
        atoi(argv[2]); // This tracker's index (line number) in tracker_info.txt

    // 🔁 Read all trackers from tracker_info.txt
    // vector<pair<string, int>> allTrackers; // Stores (IP, Port)
    ifstream file(trackerInfoFile);
    string line;

    while (getline(file, line)) {
      size_t colonPos = line.find(':');
      if (colonPos != string::npos) {
        string ip = line.substr(0, colonPos);
        int port = stoi(line.substr(colonPos + 1));
        trackerList.emplace_back(ip, port);
      }
    }
    file.close();

    // ❌ Sanity check
    if (myTrackerIndex < 0 || myTrackerIndex > int(trackerList.size())) {
      cerr << "Invalid tracker index provided." << endl;
      exit(1);
    }

    string IPAddress = trackerList[myTrackerIndex-1].first;
    int portNumber = trackerList[myTrackerIndex-1].second;

    // opening the file tracker_info.txt
    // int fd = open(argv[1], O_RDONLY);
    // if (fd < 0)
    // {
    //     perror("tracker_info.txt cant be opened due to unexpected error");
    //     exit(1);
    // }

    // // reading its content into buffer
    // char buffer[100];
    // ssize_t bytesRead;
    // string extract = "";
    // while ((bytesRead = read(fd, buffer, sizeof(buffer) - 1)) > 0) // read() doesn't automatically null-terminate the buffer, so we create space for null
    // {
    //     // adding null at end of buffer so that it is easy to work with string
    //     buffer[bytesRead] = '\0';
    //     extract += buffer;
    // }

    // if (bytesRead == -1)
    // {
    //     perror("there was a error in reading tracker_info.txt");
    // }

    // close(fd);
    // cout<<extract<<endl;

    // extracting IP and port from extract string
    // string IPAddress, portNum;
    // for (int i = 0; i < extract.size(); i++)
    // {
    //     if (extract[i] == ':')
    //     {
    //         portNum = extract.substr(i + 1);
    //         break;
    //     }
    //     IPAddress += extract[i];
    // }

    // int portNumber = atoi(portNum.c_str());

    // cout<<IPAddress<<endl;
    // cout<<portNumber<<endl;

    // Create a socket and get its descriptor
    int domain = AF_INET;
    int type = SOCK_STREAM;
    int protocol = 0;
    int serverSocket = socket(domain, type, protocol);
    if (serverSocket == -1)
    {
        perror("unable to create a socket of tracker");
        exit(1);
    }

    // setting options for the serverSocket
    int option = 1;
    int setOption = setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &option, sizeof(option));
    if (setOption == -1)
    {
        perror("unable to set options for tracker socket");
        exit(1);
    }

    sockaddr_in serverAddress;

    // The server socket will listen for incoming connections only on the network interface IPaddress
    int validIP = inet_pton(AF_INET, IPAddress.c_str(), &serverAddress.sin_addr);
    if (validIP <= 0)
    {
        perror("Invalid IP address of tracker");
        exit(1);
    }

    // sockaddr_in structure is used in sockets for defining an endpoint address
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(portNumber); // Convert port number to network byte order (Big-Endian)
    // The sin_port field of the sockaddr_in structure expects the port number to be in network byte order

    // Bind the socket to the specified IP and Port
    int bindFD = bind(serverSocket, (struct sockaddr *)&serverAddress, sizeof(serverAddress));
    if (bindFD == -1)
    {
        perror("unable to bind the socket of tracker");
        exit(1);
    }

    // Start listening for incoming connections, max connection allowed in queue
    // is 50 ✅ Start listening for connections
    if (listen(serverSocket, MAX_CONNECTION) == -1) {
      perror("Unable to listen for incoming connections");
      exit(1);
    }

    cout << "🛰️  Tracker is listening at " << IPAddress << ":" << portNumber
         << endl;

    // 🧵 Launch tracker-to-tracker sync listener
    int syncPort = portNumber + 1000;
    thread(syncListener, syncPort).detach();

    registerToLoadBalancer(IPAddress, portNumber);

    // infinite loop to accept connection from various clients
    //  bool trackerRunning = true

    while (true)
    // signal(SIGINT, SIG_IGN); // Ignore Ctrl+C
    {
      // cout<<"again entering while loop for next command"<<endl;
      // Data structure to hold client's address information
      sockaddr_in clientAddress;
      socklen_t clientLength = sizeof(clientAddress);

      // Accept an incoming connection from a client
      int clientSocket = accept(serverSocket, (struct sockaddr *)&clientAddress,
                                &clientLength);
      if (clientSocket == -1) {
        perror("unable to accept incomming connection from client");
        continue;
      } else if (clientSocket > 0) {

        notifyLoadBalancer("INCREMENT");
        cout << "Accepted Connection from Client" << endl;
      }

      try {
        // creating a new thread object everytime a new request from cllient is
        // accepted
        thread clientThread(clientHandler, clientSocket);
        cout<<"after making a thread and sending it to client handler"<<endl;

        // Detaches the thread from main() so it can run independently and clean
        // up themselves automatically when they finish execution
        clientThread.detach();
      } catch (system_error &e) {
        cerr << "Unable to create thread for client request: " << e.what()
             << endl;
      }

      // cout<<"after completing command"<<endl;
    }

    // Close the server socket
    // close(clientSocket);
    // close(serverSocket);

    return 0;
}

void updatePeerStatsFromClient(const string &ip, int port, float score,
                               int served, double time) {
  string key = ip + ":" + to_string(port);
  globalPeerStats[key] = PeerStats(ip, port);
  globalPeerStats[key].score = score;
  globalPeerStats[key].chunksServed = served;
  globalPeerStats[key].totalDownloadTime = time;
}

string serializePeerStats(const PeerStats &stats) {
  return stats.ip + " " + to_string(stats.port) + " " + to_string(stats.score) +
         " " + to_string(stats.chunksServed) + " " +
         to_string(stats.totalDownloadTime);
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

void syncListener(int syncPort) {
  int syncSocket = socket(AF_INET, SOCK_STREAM, 0);
  if (syncSocket == -1) {
    perror("❌ Failed to create sync socket");
    exit(1);
  }

  int opt = 1;
  setsockopt(syncSocket, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt,
             sizeof(opt));

  sockaddr_in syncAddr{};
  syncAddr.sin_family = AF_INET;
  syncAddr.sin_addr.s_addr = INADDR_ANY;
  syncAddr.sin_port = htons(syncPort);

  if (bind(syncSocket, (struct sockaddr *)&syncAddr, sizeof(syncAddr)) == -1) {
    perror("❌ Failed to bind sync socket");
    exit(1);
  }

  if (listen(syncSocket, 10) == -1) {
    perror("❌ Failed to listen on sync port");
    exit(1);
  }

  cout << "🔁 Sync listener running on port " << syncPort << endl;

  while (true) {
    sockaddr_in peerAddr{};
    socklen_t len = sizeof(peerAddr);
    int peerSock = accept(syncSocket, (struct sockaddr *)&peerAddr, &len);
    if (peerSock == -1) {
      perror("❌ Sync accept failed");
      continue;
    }

    // 🧵 Spawn thread to handle this sync message
    thread([peerSock]() {
      handleSyncConnection(peerSock); // 🔄 Delegate to handler
    }).detach();
  }
}

// 🔄 [Sync] Send sync message to all other trackers
void sendSyncToPeers(const string &message, int selfIndex) {
  for (int i = 0; i < int(trackerList.size()); ++i) {
    if (i == selfIndex-1)
      continue; // Skip self

    string peerIP = trackerList[i].first;
    int peerPort = trackerList[i].second + 1000; // sync port

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
      cerr << "❌ [Sync] Failed to create socket to " << peerIP << ":"
           << peerPort << endl;
      continue;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(peerPort);
    inet_pton(AF_INET, peerIP.c_str(    ), &addr.sin_addr);

    // setting options for the serverSocket
    int option = 1;
    int setOption =
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT,
                   &option, sizeof(option));
    if (setOption == -1) {
      perror("unable to set options for sync tracker socket");
      exit(1);
    }

    if (connect(sock, (sockaddr *)&addr, sizeof(addr)) < 0) {
      cerr << "❌ [Sync] Failed to connect to " << peerIP << ":" << peerPort
           << endl;
      close(sock);
      continue;
    }

    send(sock, message.c_str(), message.size(), 0); // no \n, just raw data
    close(sock);
    cout << "📤 [Sync] Sent to " << peerIP << ":" << peerPort << " → "
         << message << endl;
  }
}

// 🔄 [Sync Handler] Parse and apply sync command
void handleSyncConnection(int syncSocket) {
  char buffer[BUFFERSIZE];
  int bytesRead = read(syncSocket, buffer, sizeof(buffer));
  if (bytesRead <= 0) {
    close(syncSocket);
    return;
  }

  string syncMsg(buffer, bytesRead);
  cout << "📥 Received sync command: " << syncMsg << endl;

  vector<string> tokens;
  stringstream ss(syncMsg);
  string part;
  while (getline(ss, part, '|'))
    tokens.push_back(part);

  if (tokens.empty()) {
    close(syncSocket);
    return;
  }

  string command = tokens[0];

  if (command == "SYNC_CREATE_USER" && tokens.size() == 3) {
    string userID = tokens[1];
    string password = tokens[2];

    lock_guard<mutex> lock(userMutex); // 🔒 [Phase 3]
    if (userMap.find(userID) == userMap.end()) {
      User *newUser = new User(userID, password);
      userMap[userID] = newUser;
      cout << "✅ [Sync] User created via sync: " << userID << endl;
    }
  } else if (command == "SYNC_LOGIN_USER" && tokens.size() == 5) {
    string userID = tokens[1];
    string ip = tokens[2];
    string port = tokens[3];
    float score = stof(tokens[4]);

    lock_guard<mutex> lock(userMutex); // 🔒 [Phase 3]
    if (userMap.find(userID) != userMap.end()) {
      userMap[userID]->isLoggedIn = true;
      userMap[userID]->IpAddress = ip;
      userMap[userID]->portNumber = port;

      cout << "[Sync] User login synced: " << userID << " (" << ip << ":"
           << port << ")" << endl;

      // Update peer stats
      string key = userID + ":" + port;
      if (globalPeerStats.find(key) == globalPeerStats.end()) {
        globalPeerStats[key] = PeerStats(userID, stoi(port));
      }
      globalPeerStats[key].score = score;

      cout << "[Sync] PeerStats updated for: " << key << " with score " << score
           << endl;
    } else {
      cout << "[Sync] Cannot sync login, user not found: " << userID << endl;
    }
  } else if (command == "SYNC_LOGOUT_USER" && tokens.size() == 2) {
    string userID = tokens[1];
    string userKey = "";

    {
      lock_guard<mutex> lock(userMutex); // 🔒 [Phase 3]
      if (userMap.find(userID) != userMap.end()) {
        User *user = userMap[userID];
        user->isLoggedIn = false;
        user->IpAddress = "";
        user->portNumber = "";
        userKey = userID + ":" + user->portNumber;
        cout << "🟡 [Sync] User logout synced: " << userID << endl;
      } else {
        cout << "⚠️ [Sync] Cannot sync logout, user not found: " << userID
             << endl;
        return;
      }
    }

    {
      lock_guard<mutex> lock(globalPeerStatsMutex); // 🔒 [Phase 3]
      if (globalPeerStats.erase(userKey) > 0) {
        cout << "🗑️ [Sync] Removed peerStats for " << userKey << endl;
      }
    }

    {
      lock_guard<mutex> lock(groupMutex); // 🔒 [Phase 3]
      for (auto &[gid, group] : groupMap) {
        for (auto &[fhash, meta] : group->sharedFiles) {
          auto &peers = meta->peersHavingFile;
          peers.erase(remove_if(peers.begin(), peers.end(),
                                [&](User *u) { return u->userID == userID; }),
                      peers.end());
        }
      }
      cout << "🗑️ [Sync] Removed user " << userID
           << " from all shared file peer lists" << endl;
    }
  } else if (command == "SYNC_CREATE_GROUP" && tokens.size() == 3) {
    string groupID = tokens[1];
    string ownerID = tokens[2];

    //   lock_guard<mutex> userLock(userMutex);   // 🔒 To access userMap
      lock_guard<mutex> groupLock(groupMutex); // 🔒 To modify groupMap

    //   if (userMap.find(ownerID) == userMap.end()) {
    //     cout << "❌ [Sync] Cannot create group, owner user not found: "
    //          << ownerID << endl;
    //   } else if (groupMap.find(groupID) != groupMap.end()) {
    //     cout << "⚠️ [Sync] Group already exists: " << groupID << endl;
    //   } else {
        User *owner = userMap[ownerID];
        Group *newGroup = new Group(groupID, owner);
        groupMap[groupID] = newGroup;
        cout << "✅ [Sync] Group created via sync: " << groupID << " by "
             << ownerID << endl;
    //   }
  } else if (command == "SYNC_JOIN_GROUP" && tokens.size() == 3) {
    string groupID = tokens[1];
    string userID = tokens[2];

    // 🔒 [Phase 3] Just directly update state assuming all validations passed
    // on sender
    lock_guard<mutex> groupLock(groupMutex);
    lock_guard<mutex> userLock(userMutex);

    Group *group = groupMap[groupID];
    User *user = userMap[userID];

    group->pendingMembers.push_back(user);
    cout << "✅ [Sync] Join request synced for user " << userID << " to group "
         << groupID << endl;

  } else if (command == "SYNC_ACCEPT_REQUEST") {
    string groupID = tokens[1];
    string userID = tokens[2];

    lock_guard<mutex> lock(groupMutex); // 🔒 [Phase 3]
    Group *grp = groupMap[groupID];
    if (!grp)
      return;

    User *pendingUser = userMap[userID];
    if (!pendingUser)
      return;

    // Remove from pending
    auto it = find(grp->pendingMembers.begin(), grp->pendingMembers.end(),
                   pendingUser);
    if (it != grp->pendingMembers.end())
      grp->pendingMembers.erase(it);

    // Add to members
    grp->groupMembers.push_back(pendingUser);
    cout << "✅ [Sync] Accept request synced: " << userID << " added to group "
         << groupID << endl;

  } else if (tokens[0] == "SYNC_LEAVE") {
    string gid = tokens[1];
    string uid = tokens[2];
    Group *g = groupMap[gid];
    if (g != nullptr) {
      auto it = find_if(g->groupMembers.begin(), g->groupMembers.end(),
                        [&](User *u) { return u->userID == uid; });
      if (it != g->groupMembers.end())
        g->groupMembers.erase(it);
      cout << "✅ [Sync] User " << uid << " left group " << gid << endl;
    }

  } else if (tokens[0] == "SYNC_DELETE_GROUP") {
    string gid = tokens[1];
    if (groupMap.find(gid) != groupMap.end()) {
      delete groupMap[gid];
      groupMap.erase(gid);
      cout << "✅ [Sync] Group " << gid << " deleted successfully" << endl;
    }
  } else if (tokens[0] == "SYNC_TRANSFER_ADMIN") {
    string gid = tokens[1];
    string newOwnerID = tokens[2];
    string oldOwnerID = tokens[3];

    Group *g = groupMap[gid];
    if (g != nullptr) {
      g->owner = userMap[newOwnerID];

      auto it = find_if(g->groupMembers.begin(), g->groupMembers.end(),
                        [&](User *u) { return u->userID == oldOwnerID; });
      if (it != g->groupMembers.end())
        g->groupMembers.erase(it);
      cout << "✅ [Sync] Admin transferred from " << oldOwnerID << " to "
           << newOwnerID << " in group " << gid << endl;
    }
  } else if (tokens[0] == "SYNC_UPLOAD_FILE") {
    cout << "SYNC_UPLOAD_FILE" << endl;
    string groupID = tokens[1];
    int fileSize = stoi(tokens[2]);
    string fileName = tokens[3];
    int totalChunks = stoi(tokens[4]);
    string fileHash = tokens[5];
    string uploaderID = tokens[6];

    // get uploader pointer
    User *uploader = userMap[uploaderID];

    vector<string> chunkHashes;
    for (int i = 7; i < int(tokens.size()); ++i) {
      chunkHashes.push_back(tokens[i]);
    }

    Group *group = groupMap[groupID];
    if (group == NULL || uploader == NULL)
      return; // basic safety

    auto file = group->sharedFiles.find(fileHash);
    if (file != group->sharedFiles.end()) {
      // File already exists, just add uploader if not present
      for (User *peer : file->second->peersHavingFile) {
        if (peer->userID == uploaderID)
          return; // already added
      }
      file->second->peersHavingFile.push_back(uploader);
      cout << "✅ [Sync] Existing file updated with new uploader " << uploaderID
           << " in group " << groupID << endl;
    } else {
      FileMetadata *meta = new FileMetadata(fileSize, fileName, totalChunks,
                                            fileHash, chunkHashes, uploader);
      group->sharedFiles[fileHash] = meta;
      cout << "✅ [Sync] New file " << fileName << " uploaded by " << uploaderID
           << " synced in group " << groupID << endl;
    }

  } else if (tokens[0] == "SYNC_STOP_SHARE") {
    string gid = tokens[1];
    string fileHash = tokens[2];

    lock_guard<mutex> lock(groupMutex); // 🔒 [Phase 3]
    Group *g = groupMap[gid];
    if (g != nullptr) {
      if (g->sharedFiles.erase(fileHash) > 0) {
        cout << "🧹 [Sync] File with hash " << fileHash
             << " removed from group " << gid << endl;
      }
    }
  } else if (tokens[0] == "SYNC_ADD_PEER") {
    string gid = tokens[1];
    string fileHash = tokens[2];
    string userID = tokens[3];

    lock_guard<mutex> lock(groupMutex); // 🔒 [Phase 3]
    Group *g = groupMap[gid];
    if (!g)
      return;

    auto it = g->sharedFiles.find(fileHash);
    if (it == g->sharedFiles.end())
      return;

    FileMetadata *meta = it->second;
    User *peer = userMap[userID];
    if (!peer)
      return;

    auto it2 =
        find(meta->peersHavingFile.begin(), meta->peersHavingFile.end(), peer);
    if (it2 == meta->peersHavingFile.end()) {
      meta->peersHavingFile.push_back(peer);
      cout << "✅ [Sync] Peer " << userID << " added to file " << fileHash
           << " in group " << gid << endl;
    }
  } else if (command == "SYNC_DOWNLOAD_INIT" && tokens.size() == 4) {
    string groupID = tokens[1];
    string fname = tokens[2];
    string userID = tokens[3];

    DownloadInfo *d = new DownloadInfo(groupID, fname);
    lock_guard<mutex> lock(downloadsMutex); // 🔒 [Phase 3]
    downloads.push_back(d);
    cout << "📥 Synced DownloadInfo for " << fname << " in group " << groupID
         << " by " << userID << endl;
  } else if (command == "SYNC_DOWNLOAD" && tokens.size() == 4) {
    string groupID = tokens[1];
    string fname = tokens[2];
    string userID = tokens[3];

    // No need for lock here if downloads is protected by external lock when
    // used
    lock_guard<mutex> lock(downloadsMutex); // 🔒

    for (auto d : downloads) {
      if (d->groupID == groupID && d->fileName == fname) {
        d->status = 1;
        cout << "✅ Synced download status for " << fname << " in group "
             << groupID << " by " << userID << endl;
        break;
      }
    }

    // Optional: If user doesn't exist in downloads list yet, add it
    // (Useful for tracker recovery)
  }

        // Future: handle other sync types here...

        close(syncSocket);
}

void notifyLoadBalancer(const string &command) {
    string trackerIP = trackerList[myTrackerIndex-1].first;
    int trackerPort = trackerList[myTrackerIndex-1].second;

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
    perror("❌ [Tracker] Failed to create socket for Load Balancer");
    return;
    }

    sockaddr_in lbAddr{};
    lbAddr.sin_family = AF_INET;
    lbAddr.sin_port = htons(loadBalancerPort);
    if (inet_pton(AF_INET, loadBalancerIP.c_str(), &lbAddr.sin_addr) <= 0) {
    perror("❌ [Tracker] Invalid Load Balancer address");
    close(sock);
    return;
    }

    if (connect(sock, (struct sockaddr *)&lbAddr, sizeof(lbAddr)) < 0) {
    perror("❌ [Tracker] Failed to connect to Load Balancer");
    close(sock);
    return;
    }

    string message = command + " " + trackerIP + " " + to_string(trackerPort);
    send(sock, message.c_str(), message.size(), 0);

    char buffer[1024];
    int bytes = recv(sock, buffer, sizeof(buffer) - 1, 0);
    if (bytes > 0) {
    buffer[bytes] = '\0';
    cout << "✅ [Tracker] LB Response: " << buffer << endl;
    }

    close(sock);
}

// tracker.cpp
void registerToLoadBalancer(string tracker_ip, int tracker_port) {
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) {
    cerr << "❌ Failed to create socket to " << loadBalancerIP << ":"
         << loadBalancerPort << endl;
    return;
  }

  // Optional but good to have — prevent TIME_WAIT socket reuse issues
  int option = 1;
  if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &option,
                 sizeof(option)) == -1) {
    cerr << "❌ [Tracker] unable to set socket options" << endl;
    close(sock); // close before returning
    return;
  }

  sockaddr_in lb_addr;
  lb_addr.sin_family = AF_INET;
  lb_addr.sin_port = htons(loadBalancerPort);
  if (inet_pton(AF_INET, loadBalancerIP.c_str(), &lb_addr.sin_addr) <= 0) {
    cerr << "❌ [Tracker] Invalid LB IP address format" << endl;
    close(sock);
    return;
  }

  if (connect(sock, (struct sockaddr *)&lb_addr, sizeof(lb_addr)) < 0) {
    cerr << "❌ [Tracker] Failed to connect to Load Balancer" << endl;
    close(sock);
    return;
  }

  // Construct and send REGISTER message
  string message = "REGISTER " + tracker_ip + " " + to_string(tracker_port) +
                   "\n"; // add newline for delimiter
  send(sock, message.c_str(), message.size(), 0);

  // Read response
  char buffer[1024] = {0};
  int bytes = recv(sock, buffer, sizeof(buffer) - 1, 0);
  if (bytes > 0) {
    buffer[bytes] = '\0';
    string response(buffer);
    cout << "📨 [Tracker] LB Response: " << response << endl;

    if (response.find("SUCCESS") != string::npos) {
      cout << "✅ [Tracker] Registered successfully" << endl;
    } else {
      cout << "❌ [Tracker] Registration failed" << endl;
    }
  } else {
    cerr << "❌ [Tracker] No response from Load Balancer" << endl;
  }

  close(sock);
}
