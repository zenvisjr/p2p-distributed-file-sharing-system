#include <iostream>
#include <thread>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <fcntl.h>  //for open()
#include <openssl/sha.h> //for hashing
#include <iomanip>
#include <arpa/inet.h>  // Needed for inet_pton
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <mutex>


using namespace std;

# define BUFFERSIZE 512
# define MAX_CONNECTION 50
bool trackerRunning = true;

string currentUSerIP, currentUserPort;

class User 
{
public:
    string userID;
    string password;
    string IpAddress;     //The IP address from which the user has logged in, currently every user is running locally on localhost 127.0.0.1
    string portNumber;    //The port number on which the user's client is running, different client have different port number
    bool isLoggedIn;      //A flag to indicate whether the user is currently logged in or not.

    User(string ID, string pswd)
    {
        userID = ID;
        password = pswd;
        isLoggedIn = false;
    }
};

unordered_map<string, User*> userMap;  // Key: userID, Value: Pointer to User object

class FileMetadata 
{
public:
    int fileSize;    //uint64_t is a 64-bit unsigned integer, which can represent a much larger range of values
    string fileName;
    int noOfChunks;
    // vector<string> hashOfChunks;
    string HashOfCompleteFile;
    vector<User*> peersHavingFile;
    
    
    FileMetadata(uint64_t fsize, string& fname, int fchunks, string& fhash, User* fileOwner)
    {
        fileSize = fsize;
        fileName = fname;
        noOfChunks = fchunks;
        // hashOfChunks = chash;
        HashOfCompleteFile = fhash;
        peersHavingFile.push_back(fileOwner);

    }
};
// unordered_map<string, FileMetadata*> metadataMap;   //Key : HashOfCompleteFile , value : pointer to FileMetadata Object

class Group
{
    public:
    string groupID;
    User* owner;                     
    vector<User*> groupMembers;     
    vector<User*> pendingMembers;     
    unordered_map<string, FileMetadata* > sharedFiles;
    // unordered_map<string, vector<FileMetadata*>> sharedFiles;
    // vector<FileMetadata* > sharedFiles;


    Group(string ID, User* currentUser)
    {
        groupID = ID;
        owner = currentUser;
        groupMembers.push_back(currentUser);
    }
};

unordered_map<string, Group*> groupMap;  // Key: groupID, Value: Pointer to Group object

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

vector<DownloadInfo*> downloads;  // Holds the download statuses
mutex downloadMutex;  // To make access to 'downloads' thread-safe


bool CreateUser(string userID, string password);
bool LoginUser(string userID, string password, string currentUserIP, string currentUserPort, User*& currentThreadUser);
bool LogoutUser(User*& currentThreadUser);

void CreateGroup(string groupID, User* &currentThreadUser, int clientSocket, int check);
void JoinGroup(string groupID, User* &currentThreadUser, int clientSocket, int check);
void LeaveGroup(string groupID, User* &currentThreadUser, int clientSocket, int check);
void ListAllGroups(User* &currentThreadUser, int clientSocket, int check);
void ListPendingRequest(string groupID, User* &currentThreadUser, int clientSocket, int check);
void AcceptRequest(string groupID, string userID, User* &currentThreadUser, int clientSocket, int check);
void UploadFile(string filePath, string groupID, int fsize, string fname, int fchunks, string fhash, User* &currentThreadUser, int clientSocket, int check);
void ListSharableFiles(string groupID, User* &currentThreadUser, int clientSocket, int check);
void StopShare(string groupID, string fileName, User* &currentThreadUser, int clientSocket, int check);
void DownloadFile(string groupID, string fileName, string destinationPath, User* &currentThreadUser, int clientSocket, int check);
void ShowDownloadStatus(User* &currentThreadUser, int clientSocket, int check);
void QuitTracker(User* currentThreadUser, int clientSocket, int check);









    

bool CreateUser(string userID, string password)
{
    // To check if a user exists 
    if (userMap.find(userID) != userMap.end()) 
    {
        // User* existingUser = userMap["john_doe"];
        cout<<"User already exist, try another userID"<<endl;
        return false;
    }
    else
    {
        // Check for valid userId and password
    //     if (userId.empty() || password.empty())
    //     {
    //         std::cout << "UserID and password cannot be empty" << std::endl;
    //         return;
    //     }
    // }
        //adding a new user
        User *newUser = new User(userID, password);
        userMap[userID] = newUser;

        // User *newUser1 = new User("ayush", "1q2w");
        // User *newUser2 = new User("naruto", "1q2w");
        // userMap[userID] = newUser1;
        // userMap[userID] = newUser2;
        cout<<"New user created with ID : "<<userID<<endl;
        
        return true;
    }
}

bool LoginUser(string userID, string password, string currentUserIP, string currentUserPort, User*& currentThreadUser)
{
        // To check if a user exists or not
    if (userMap.find(userID) == userMap.end()) 
    {
        cout<<"User do not exist, try another userID"<<endl;
        return false;
    }
    else if(userMap[userID]->isLoggedIn == true)
    {
        cout<<"User is already logged in"<<endl;
        return false;
    }
    else if(userMap[userID]->password == password)
    {
        userMap[userID]->isLoggedIn = true;
        userMap[userID]->IpAddress = currentUserIP;
        userMap[userID]->portNumber = currentUserPort;

        // cout<<"After logging in"<<endl;
        // cout<<userMap[userID]->isLoggedIn<<endl;
        // cout<<userMap[userID]->IpAddress<<endl;
        // cout<<userMap[userID]->portNumber <<endl;

        //VERY IMPORTANT: mapping currentThreadUser to same userID so that we can track user of current client
        currentThreadUser = userMap[userID];

        cout<<userID<<" is logged in now"<<endl;
        return true;
    }
    else
    {
        cout<<"password do not match"<<endl;
        return false;
    }
}

bool LogoutUser(User*& currentThreadUser)
{
        // To check if a user exists or not
    if (currentThreadUser == NULL) 
    {
        cout<<"User is not logged in yet"<<endl;
        return false;
    }
    else if(currentThreadUser->isLoggedIn == false)
    {
        cout<<"User is already logged out"<<endl;
        return false;
    }
    else
    {
        currentThreadUser->isLoggedIn = false;
        currentThreadUser->IpAddress = "";
        currentThreadUser->portNumber = "";

        // cout<<"After logging out"<<endl;
        // cout<<currentThreadUser->isLoggedIn<<endl;
        // cout<<currentThreadUser->IpAddress<<endl;
        // cout<<currentThreadUser->portNumber <<endl;

        cout<<currentThreadUser->userID<<" is logged out"<<endl;

        //mapping currentThreadUser to NULL as the cuer of current client is logged out
        currentThreadUser = NULL;

        return true;
    }
}

void CreateGroup(string groupID, User* &currentThreadUser, int clientSocket, int check)
{
    string response;
    if(check==0)
    {
        response = "First create a user";
        cout<<response<<endl;
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    //checking if user is logged in or not
    else if(currentThreadUser == NULL || currentThreadUser->isLoggedIn == false)
    {
        response = "User is not logged in yet";
        cout<<response<<endl;
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    // if user is logged in we check if a group already exists 
    else if(groupMap.find(groupID) != groupMap.end()) 
    {
        response = "Group already exist, try another GroupID";
        cout<<response<<endl;
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    //if group do not exist we create one
    else 
    {
        Group* newGroup = new Group(groupID, currentThreadUser);
        groupMap[groupID] = newGroup;
        cout<<groupID<<" group created successfully"<<endl;

        response = "Group created successfully";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    
}

void JoinGroup(string groupID, User* &currentThreadUser, int clientSocket, int check)
{
    string response;
    Group *currentGroup = groupMap[groupID];    //currentGroup points to the same group that groupID points to 

    //checking if user exist or not
    if(check==0)
    {
        cout<<"No user exist"<<endl;
        response = "FIRST CREATE A USER";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    //checking if user is logged in or not
    else if(currentThreadUser == NULL || currentThreadUser->isLoggedIn == false)
    {
        cout<<"User is not logged in yet"<<endl;
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
    else if(currentGroup == NULL) 
    {
        response = "Group do not exist, try another GroupID";
        cout<<response<<endl;
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    //if group exist then check if member is already present
    else if(find(currentGroup->groupMembers.begin(), currentGroup->groupMembers.end(), currentThreadUser) != currentGroup->groupMembers.end())
    {
        cout<<"you are already part of the "<< groupID << " group"<<endl;
        response = "you are already part of the group";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    //if group exist then check if members is already in pending request
    else if(find(currentGroup->pendingMembers.begin(), currentGroup->pendingMembers.end(), currentThreadUser) != currentGroup->pendingMembers.end())
    {
        cout<<"you are currently in pending list of "<< groupID << " group"<<endl;
        response = "Already sent join request to this group";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
     //if member is not present in group with no pending request then we add it to pending list of join request
     else
     {
        currentGroup->pendingMembers.push_back(currentThreadUser);
        cout<<"Request sent to group owner for joining group"<<endl;
        response = "Request Sent! Waiting For Owner's Approval...";
        send(clientSocket, response.c_str(), response.size(), 0);
     }
    
}

void LeaveGroup(string groupID, User* &currentThreadUser, int clientSocket, int check)
{
    string response;
    Group *currentGroup = groupMap[groupID];    //currentGroup points to the same group that groupID points to 

    // if user is logged in we check if a group already exists or not
    if(currentGroup == NULL) 
    {
        response = "Group do not exist, try another GroupID";
        cout<<response<<endl;
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    //checking if user exist or not
    else if(check==0)
    {
        cout<<"No user exist"<<endl;
        response = "FIRST CREATE A USER";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    //checking if user is logged in or not
    else if(currentThreadUser == NULL || currentThreadUser->isLoggedIn == false)
    {
        cout<<"User is not logged in yet"<<endl;
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
    //if group exist then check if members is already in pending request
    else if(find(currentGroup->pendingMembers.begin(), currentGroup->pendingMembers.end(), currentThreadUser) != currentGroup->pendingMembers.end())
    {
        cout<<"you are currently in pending list of "<< groupID << " group"<<endl;
        response = "Your join request is still not accepted by group admin";
        send(clientSocket, response.c_str(), response.size(), 0);
    }

    //if group exist and not in pending list then check if member is already present
    else if(find(currentGroup->groupMembers.begin(), currentGroup->groupMembers.end(), currentThreadUser) == currentGroup->groupMembers.end())
    {
        cout<<"you are not part of the "<< groupID << " group"<<endl;
        response = "you do not belong to the group";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
     //if member is present in group with no pending request then we check if member is admin of that group or not
     else if(currentGroup->owner == NULL)
     {
        //finding index of the member in the list
        auto deleteIndex = find(currentGroup->groupMembers.begin(), currentGroup->groupMembers.end(), currentThreadUser);


        //deleting the index
        currentGroup->groupMembers.erase(deleteIndex);
        cout<<"You have been removed from the "<<groupID<< " group successfully."<<endl;
        response = "Removed from Group Successfully!";
        send(clientSocket, response.c_str(), response.size(), 0);
     }
     //you are the admin of the group and only member present
     else if(currentGroup->groupMembers.size() == 1)
     {
        delete currentGroup;
        groupMap.erase(groupID);
        cout<<"The admin left the group so it has been deleted!"<<endl;
        response = "Deleted Group Successfully!";
        send(clientSocket, response.c_str(), response.size(), 0);
     }
     //there are more than one members in your group
     else
     {
        for(auto member : currentGroup->groupMembers)
        {
            if(member->userID != currentGroup->owner->userID)
            {
                currentGroup->owner = member;

                //finding index of the member in the list
                auto deleteIndex = find(currentGroup->groupMembers.begin(), currentGroup->groupMembers.end(), currentThreadUser);


                //deleting the index
                currentGroup->groupMembers.erase(deleteIndex);

                // cout<<"current admin: "<<currentGroup->owner->userID<<endl;
                cout<<"Admin removed and ownership of group transferred to "<<currentGroup->owner->userID<<endl;
                response = "Admin removed from Group Successfully!";
                send(clientSocket, response.c_str(), response.size(), 0);
                break;
            }
        // }
        // Group* current = groupMap[groupID]
        // cout<<"current admin: "<<current->owner->userID<<endl;

        }
     }


    
}

void ListAllGroups(User* &currentThreadUser, int clientSocket, int check)
{
    string response = "";
    // //vector to store every group that exist
    // vector<string> allGroups;
    // cout<<"entering"<<endl;

    //checking if user exist or not
    if(check==0)
    {
        cout<<"No user exist"<<endl;
        response = "FIRST CREATE A USER";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    //checking if user is logged in or not
    else if(currentThreadUser == NULL || currentThreadUser->isLoggedIn == false)
    {
        cout<<"User is not logged in yet"<<endl;
        response = "LOGIN FIRST!!!!!!!";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    //checking if groupMap is empty or not
    else if(groupMap.empty())
    {
        cout<<"No group exist"<<endl;
        response = "No group created till now";
        send(clientSocket, response.c_str(), response.size(), 0);
        
    }
    //printing all groups ID sending it to client
    else
    {
        //sending size of the unordered map to client
        // int noOfGroups = groupMap.size();
        // response += "#" + to_string(noOfGroups);
        // cout<<response<<endl;

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

    void ListPendingRequest(string groupID, User* &currentThreadUser, int clientSocket, int check)
    {
        string response;
        Group *currentGroup = groupMap[groupID]; 

        //checking if user exist or not
        if(check==0)
        {
            cout<<"No user exist"<<endl;
            response = "FIRST CREATE A USER";
            send(clientSocket, response.c_str(), response.size(), 0);
        }
        //checking if user is logged in or not
        else if(currentThreadUser == NULL || currentThreadUser->isLoggedIn == false)
        {
            cout<<"User is not logged in yet"<<endl;
            response = "LOGIN FIRST!!!!!!!";
            send(clientSocket, response.c_str(), response.size(), 0);
        }
        //checking if groupMap is empty or not
        else if(currentGroup == NULL)
        {
            cout<<groupID <<" Group do not exist"<<endl;
            response = "Group do not exist";
            send(clientSocket, response.c_str(), response.size(), 0);
        }
        //seeing if the requesting user is the owner of the group
        else if(currentGroup->owner != currentThreadUser)
        {
            cout <<"Only Owner can see Pending Request!"<<endl;
            response= "Only admin of the group is authorised to see pending request";
            
        }
        //seeing if pendingMembers is empty or not
        else if(currentGroup->pendingMembers.size() == 0)
        {
            cout<<"There's no one waiting for approval!!"<<endl;
            response = "There's no one waiting for approval!!";
            send(clientSocket, response.c_str(), response.size(), 0);
        }
        //listing all pending request
        else
        {
            cout<<"Listing All Pending Requests..."<<endl;
            // //creating a new vector to store userID of pending request
            // vector<string> pendingUserId;
            int pendingListSize = currentGroup->pendingMembers.size();

            //iterating over the pending list and printing user ID 
            for (int i = 0; i < pendingListSize; i++)
            {
                cout << currentGroup->pendingMembers[i]->userID << endl;
                response += '#' + currentGroup->pendingMembers[i]->userID ;
                // pendingUserId.push_back(currentGroup->pendingMembers[i]->userID);
            }
            // cout<<response<<endl;
            send(clientSocket, response.c_str(), response.size(), 0);
        }

    }

void AcceptRequest(string groupID, string userID, User* &currentThreadUser, int clientSocket, int check)
{
    string response;
    Group *currentGroup = groupMap[groupID]; 

    //checking if group exist or not
    if(currentGroup == NULL)
    {
        cout<<groupID <<" Group do not exist"<<endl;
        response = "Group do not exist";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    //checking if user exist or not
    else if(check==0)
    {
        cout<<"No user exist"<<endl;
        response = "FIRST CREATE A USER";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    //checking if user is logged in or not
    else if(currentThreadUser == NULL || currentThreadUser->isLoggedIn == false)
    {
        cout<<"User is not logged in yet"<<endl;
        response = "LOGIN FIRST!!!!!!!";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    //seeing if the requesting user is the owner of the group
    else if(currentGroup->owner != currentThreadUser)
    {
        cout <<"Only Owner can accept Pending Request!"<<endl;
        response= "Only Admin of the group is authorised to accept pending request";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    else 
    {
        bool check = false;
        //finding userID in pending list
        for(auto pendingUser : currentGroup->pendingMembers)
        {
            if(pendingUser->userID == userID)
            {
                check = true;
                //add this user as a member to the group
                currentGroup -> groupMembers.push_back(pendingUser);

                //remove this user from pending list 
                auto pendingIndex = find(currentGroup->pendingMembers.begin() , currentGroup->pendingMembers.end() , pendingUser);
                currentGroup -> pendingMembers.erase (pendingIndex);

                cout<<"Accepted your request"<<endl;
                response="Added to group successfully! WELCOME!!";
                send(clientSocket,response.c_str(),response.size(),0);

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
        if(check == false)
        {
            cout<<userID<<" is not in pending list of "<<groupID<<endl;
            response="USER NOT IN PENDING LIST OF GROUP";
            send(clientSocket,response.c_str(),response.size(),0);
        }
    }
}

void UploadFile(string filePath, string groupID, int fsize, string fname, int fchunks, string fhash, User* &currentThreadUser, int clientSocket, int check)
{
    string response;
    Group *currentGroup = groupMap[groupID]; 

    //checking if user exist or not
    if(check==0)
    {
        cout<<"No user exist"<<endl;
        response = "FIRST CREATE A USER";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    //checking if user is logged in or not
    else if(currentThreadUser == NULL || currentThreadUser->isLoggedIn == false)
    {
        cout<<"User is not logged in yet"<<endl;
        response = "LOGIN FIRST!!!!!!!";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    //checking if group exist or not
    else if(currentGroup == NULL)
    {
        cout<<groupID <<" Group do not exist"<<endl;
        response = "Group do not exist";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    //checking if the user who sent the request is a member of the group
    else if(find(currentGroup->groupMembers.begin(), currentGroup->groupMembers.end(), currentThreadUser) == currentGroup->groupMembers.end())
    {
        cout<<"you are not part of the "<< groupID << " group"<<endl;
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
    //checking if u have already uploaded that file otherwise uploading it
    else
    {
        auto file = currentGroup->sharedFiles.find(fhash);

        //if file already exist then check if u have uploaded that file
        if(file != currentGroup->sharedFiles.end())
        {
            bool check = false;
            for(auto peer: file->second->peersHavingFile)
            {
               
                if(peer->userID == currentThreadUser->userID)
                {
                    check = true;
                    cout<<"You have already shared this file"<<endl;
                    response = fname + " is already shared by u";
                    send(clientSocket, response.c_str(), response.size(), 0);
                    return ;
                }
                
            }
            //the file already exist but you have not uploaded it yet so add ur entry in peers
            file->second->peersHavingFile.push_back(currentThreadUser);
            

        }
        //file do not exist then upload it
        else
        {
            FileMetadata* newfile = new FileMetadata(fsize, fname, fchunks, fhash, currentThreadUser);
            currentGroup->sharedFiles[fhash] = newfile;
            // currentGroup->sharedFiles[fhash].push_back(newfile);

        }
        cout<<"New file "<<fname<<" Uploaded by " <<currentThreadUser->userID<<" of Size "<<fsize<<" in "<<groupID<<" Group"<<endl;
        response = "File shared in group successfully";
        send(clientSocket,response.c_str(),response.size(),0 );

    }
}

void ListSharableFiles(string groupID, User* &currentThreadUser, int clientSocket, int check)
{
    string response;
    Group *currentGroup = groupMap[groupID]; 

    //checking if user exist or not
    if(check==0)
    {
        cout<<"No user exist"<<endl;
        response = "FIRST CREATE A USER";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    //checking if user is logged in or not
    else if(currentThreadUser == NULL || currentThreadUser->isLoggedIn == false)
    {
        cout<<"User is not logged in yet"<<endl;
        response = "LOGIN FIRST!!!!!!!";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    //checking if group exist or not
    else if(currentGroup == NULL)
    {
        cout<<groupID <<" Group do not exist"<<endl;
        response = "Group do not exist";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    //checking if the user who sent the request is a member of the group
    else if(find(currentGroup->groupMembers.begin(), currentGroup->groupMembers.end(), currentThreadUser) == currentGroup->groupMembers.end())
    {
        cout<<"you are not part of the "<< groupID << " group"<<endl;
        response = "UNAUTHORISED ACCESS!!! You do not belong to the group";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
            //seeing if there is any file  exist to share
    else if(currentGroup->sharedFiles.size() == 0)
    {
        cout<<groupID<<"group do not have any sharable file."<<endl;
        response = "There's no file sharable !!!!";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    else
    {
        string shareFiles;
        cout<<"LIST OF ALL SHARABLE FILES IN " <<groupID<<" GROUP"<<endl;
        for(auto fname: currentGroup->sharedFiles)
        {
            cout<<fname.second->fileName<<endl;
            shareFiles += "#" + fname.second->fileName;
        }
        // cout<<shareFiles<<endl;
        response = shareFiles;
        // response = "Here are all files that can be shared with you : ";
        send(clientSocket,response.c_str(),response.size(),0 );
    }
}

void StopShare(string groupID, string fname, User* &currentThreadUser, int clientSocket, int check)
{
    string response;
    Group *currentGroup = groupMap[groupID]; 

    //checking if user exist or not
    if(check==0)
    {
        cout<<"No user exist"<<endl;
        response = "FIRST CREATE A USER";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    //checking if user is logged in or not
    else if(currentThreadUser == NULL || currentThreadUser->isLoggedIn == false)
    {
        cout<<"User is not logged in yet"<<endl;
        response = "LOGIN FIRST!!!!!!!";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    //checking if group exist or not
    else if(currentGroup == NULL)
    {
        cout<<groupID <<" Group do not exist"<<endl;
        response = "Group do not exist";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    //checking if the user who sent the request is a member of the group
    else if(find(currentGroup->groupMembers.begin(), currentGroup->groupMembers.end(), currentThreadUser) == currentGroup->groupMembers.end())
    {
        cout<<"you are not part of the "<< groupID << " group"<<endl;
        response = "UNAUTHORISED ACCESS!!! You do not belong to the group";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    //seeing if sharedFiles list is empty
    else if(currentGroup->sharedFiles.size() == 0)
    {
        cout<<groupID<<"group do not have any sharable file."<<endl;
        response = "There's no file sharable !!!!";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    //checking if file that we want to stop share exist or not and if exist then deleting it
    // else if(currentGroup->sharedFiles.find(fhash) == currentGroup->sharedFiles.end())
    else
    {
        string fhash;
        bool check = false;
        for(auto index : currentGroup->sharedFiles)
        {
            if(index.second->fileName == fname)
            {
                check = true;
                fhash = index.first;
                //deleting the file 
                currentGroup->sharedFiles.erase(fhash);
                cout<<fname<<" removed from shared file list of the" <<groupID<<" group"<<endl;
                response = fname + " stopped sharing successfully";
                send(clientSocket, response.c_str(), response.size(), 0); 
                break;
            }
        }
        if(check == false)
        {
            cout<<fname<<" file do not exist in the group"<<endl;
            response = fname + " do not exist in the group";
            send(clientSocket, response.c_str(), response.size(), 0);
        }
    }
}

void DownloadFile(string groupID, string fname, string destinationPath, User* &currentThreadUser, int clientSocket, int check)
{
    string response;
    Group *currentGroup = groupMap[groupID]; 

    //checking if user exist or not
    if(check==0)
    {
        cout<<"No user exist"<<endl;
        response = "FIRST CREATE A USER";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    //checking if user is logged in or not
    else if(currentThreadUser == NULL || currentThreadUser->isLoggedIn == false)
    {
        cout<<"User is not logged in yet"<<endl;
        response = "LOGIN FIRST!!!!!!!";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    //checking if group exist or not
    else if(currentGroup == NULL)
    {
        cout<<groupID <<" Group do not exist"<<endl;
        response = "Group do not exist";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    //checking if the user who sent the request is a member of the group
    else if(find(currentGroup->groupMembers.begin(), currentGroup->groupMembers.end(), currentThreadUser) == currentGroup->groupMembers.end())
    {
        cout<<"you are not part of the "<< groupID << " group"<<endl;
        response = "UNAUTHORISED ACCESS!!! You do not belong to the group";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    //seeing if sharedFiles list is empty
    else if(currentGroup->sharedFiles.size() == 0)
    {
        cout<<groupID<<"group do not have any sharable file."<<endl;
        response = "There's no file to dowload from group " + groupID + " !!!!";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    //checking if filename exist in the sharable file list
    else
    {
        string fhash;
        bool check = false;
        for(auto index : currentGroup->sharedFiles)
        {
            if(index.second->fileName == fname)
            {
                check = true;
                fhash = index.first;

                //if file is found in the group, send all the peers that uploaded that file to the group
                string peers = "";
                for(auto p : index.second->peersHavingFile)
                {
                    cout<<p->userID<< " "<<p->portNumber<<endl;
                    response += "#" + p->userID + "#" + p->portNumber;
                }
                // cout<<peers;
                cout<<"Detail of peers send successfully"<<endl;
                // response = peers;

                //updating downloadinfo
                // lock_guard<mutex> lock(downloadMutex);
                DownloadInfo* newInfo = new DownloadInfo(groupID, fname);
                downloads.push_back(newInfo);

                send(clientSocket, response.c_str(), response.size(), 0); 

                //recieving response from client after file is downloaded successfully
                char buffer[BUFFERSIZE];
                int bytesReceived = recv(clientSocket, buffer, sizeof(buffer), 0);
                if(bytesReceived <= 0)
                {
                    perror("there was a error in reading the response of download status from client");
                }

                string receivedDownloadStatus(buffer, bytesReceived);
                if (receivedDownloadStatus == "DownloadCompleteSuccessfully") 
                {
                    // Updating the status
                    for(auto status: downloads)
                    {
                        if(status->fileName == fname)
                        {
                            status->status = 1;
                        }
                    }
                    
                }

                //update tracker that I have the file too
                for(auto file : currentGroup->sharedFiles) 
                {
                    if(file.second->fileName == fname)
                    {
                        auto it = find(file.second->peersHavingFile.begin(), file.second->peersHavingFile.end(), currentThreadUser);
                        if(it == file.second->peersHavingFile.end())
                        {
                            file.second->peersHavingFile.push_back(currentThreadUser);
                        }  
                    }
                }
                break;
            }
        }
        if(check == false)
        {
            cout<<fname<<" file do not exist in the group"<<endl;
            response = fname + " do not exist in the group" + groupID;
            send(clientSocket, response.c_str(), response.size(), 0);
        }
    }
 }

 void ShowDownloadStatus( User* &currentThreadUser, int clientSocket, int check) 
{
    string response;
    // Group *currentGroup = groupMap[groupID]; 

    //checking if user exist or not
    if(check==0)
    {
        cout<<"No user exist"<<endl;
        response = "FIRST CREATE A USER";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    //checking if user is logged in or not
    else if(currentThreadUser == NULL || currentThreadUser->isLoggedIn == false)
    {
        cout<<"User is not logged in yet"<<endl;
        response = "LOGIN FIRST!!!!!!!";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    else if(downloads.size() == 0)

    {
        response = "no downloads till now";
        cout<<response<<endl;
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    else
    {
        
        for (auto d : downloads) 
        {
            // string downloadInfo = (d.status ? "[D]" : "[C]") + " [" + d.groupID + "] " + d.fileName;
            string downloadInfo = string(d->status ? "D" : "C") + " " + d->groupID + " " + d->fileName;

            // cout<<downloadInfo<<endl;
            response +="#" + downloadInfo;
        }
        // cout<<response<<endl;
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    // lockGuard<mutex> lock(downloadMutex);
    
}

void QuitTracker(User* currentThreadUser, int clientSocket, int check)
{
    string response;
    // Group *currentGroup = groupMap[groupID]; 

    //checking if user exist or not
    if(check==0)
    {
        cout<<"No user exist"<<endl;
        response = "FIRST CREATE A USER";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    //checking if user is logged in or not
    else if(currentThreadUser == NULL || currentThreadUser->isLoggedIn == false)
    {
        cout<<"User is not logged in yet"<<endl;
        response = "LOGIN FIRST!!!!!!!";
        send(clientSocket, response.c_str(), response.size(), 0);
    }
    else
    {
        cout<<"Tracker is shutdown"<<endl;
        response = "Tracker is shutting down...";
        send(clientSocket, response.c_str(), response.size(), 0);
        trackerRunning = false;
        exit(0);
    }
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



void clientHandler(int clientSocket)
{
    //local thread varaible to track curent user that logs in
    User *currentThreadUser = NULL;
    int checkNoOfUser;
    int checkNoOfLogin;
    while(trackerRunning)
    {
        // cout<<"entering clientHandler"<<endl;
        char buffer[BUFFERSIZE];
        int bytesRecieved = recv(clientSocket, buffer, sizeof(buffer)-1, 0);
        if(bytesRecieved < 0)
        {
            perror("unable to read command");
        }
        buffer[bytesRecieved] = '\0';  //Null-terminating the received data

        // cout << "Data received: " << buffer << endl;

        //Safely converting char array to string
        string commandRecieved(buffer, bytesRecieved);

        //command and its arguments are stored in a vector named arguments
        vector<string> command = ExtractArguments(commandRecieved);
        // int i;
        // for(auto i: command)
        // {
        //     cout<<i<<endl;
        // }

        // string command = "create_user ayush 1q2w3e";
        string response;

        if(command[0] == "create_user")
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
            if(checkNoOfUser == 1)
            {
                cout<<"One user is already registered, creation of multiple users not allowed"<<endl;
                response = "USER EXIST: A client can have max 1 user";
                send(clientSocket, response.c_str(), response.size(), 0);
                continue;
            }

            bool status = CreateUser(id, pswd);
            

            //sending response to client 
            
            if(status == true)
            {
                response = "User created successfully";
                checkNoOfUser++;
            }
            else
            {
                response = "User already exists";
            }
            send(clientSocket, response.c_str(), response.size(), 0);
        }
        else if(command[0] == "login")
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

            //checking if more than one user is trying to log in
            if(checkNoOfLogin > 0)
            {
                cout<<"One user is already logged in, multiple users not allowed to log in"<<endl;
                response = "USER LOGGED IN: A client can have max 1 logged in user";
                send(clientSocket, response.c_str(), response.size(), 0);
                continue;
            }
            bool status = LoginUser(id, pswd, ip, port, currentThreadUser);
            

            //sending response to client 
            if(status == true)
            {
                response = "User login successfully";
                checkNoOfLogin++;
            }
            else
            {
                response = "Error logging in, try again";
            }
            //sending response back to client
            send(clientSocket, response.c_str(), response.size(), 0);
        }
        else if(command[0] == "logout")
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

            //sending response to client 
            if(status == true)
            {
                response = "User logged out successfully";
            }
            else
            {
                response = "Error logging in, try again";
            }
            //sending response back to client
            send(clientSocket, response.c_str(), response.size(), 0);
        }
        else if(command[0] == "create_group")
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

            //sending response to client 
            
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
        else if(command[0] == "join_group")
        {
            // cout<<"Command Received : "<<commandRecieved.substr(0, 10)<<endl;
            string id = command[1];
            JoinGroup(id, currentThreadUser, clientSocket, checkNoOfUser);
        }
         else if(command[0] == "leave_group")
        {
            // cout<<"Command Received : "<<commandRecieved.substr(0, 11)<<endl;
            string id = command[1];
            LeaveGroup(id, currentThreadUser, clientSocket, checkNoOfUser);
        }
       else if(command[0] == "list_groups")
        {
            // cout<<"Command Received : "<<commandRecieved.substr(0, 11)<<endl;
            
            ListAllGroups(currentThreadUser, clientSocket, checkNoOfUser);
        }
        else if(command[0] == "list_requests")
        {
            // cout<<"Command Received : "<<commandRecieved.substr(0, 13)<<endl;
            string id = command[1];
            ListPendingRequest(id, currentThreadUser, clientSocket, checkNoOfUser);
        }
        else if(command[0] == "accept_request")
        {
            // cout<<"Command Received : "<<commandRecieved.substr(0, command[0].length())<<endl;
            string gid = command[1];
            string uid = command[2];
            AcceptRequest(gid, uid, currentThreadUser, clientSocket, checkNoOfUser);
        }
        else if(command[0] == "upload_file")
        {
            // cout<<"Command Received : "<<commandRecieved.substr(0, command[0].length())<<endl;
            // for(auto v: command)
            // {
            //     cout<<v<<endl;
            // }
            string fpath = command[1];
            string gid = command[2];
            // try {
            //     int fsize = stoi(command[3]);
            //     } catch (invalid_argument& ia) {
            //         cerr << "Invalid argument: " << ia.what() << endl;
            //         // Handle exception
            //     } catch (out_of_range& oor) {
            //         cerr << "Out of range: " << oor.what() << endl;
            // }
            int fsize = stoi(command[3]);
            string fname = command[4];
            int fchunks = stoi(command[5]);
            //  try {
            //     int fchunks = stoi(command[5]);
            //     } catch (invalid_argument& ia) {
            //         cerr << "Invalid argument: " << ia.what() << endl;
            //         // Handle exception
            //     } catch (out_of_range& oor) {
            //         cerr << "Out of range: " << oor.what() << endl;
            // }
            string fhash = command[6];
            UploadFile(fpath, gid, fsize, fname, fchunks, fhash, currentThreadUser, clientSocket, checkNoOfUser);
        }
        else if(command[0] == "list_files")
        {
            // cout<<"Command Received : "<<commandRecieved.substr(0, command[0].length())<<endl;
            string gid = command[1];
            ListSharableFiles(gid, currentThreadUser, clientSocket, checkNoOfUser);
        }
        else if(command[0] == "stop_share")
        {
            // cout<<"Command Received : "<<commandRecieved.substr(0, command[0].length())<<endl;
            string gid = command[1];
            // string fpath = command[2];
            string fname = command[2];
            // string fhash = command[6];
            StopShare(gid, fname, currentThreadUser, clientSocket, checkNoOfUser);
        }
        else if(command[0] == "download_file")
        {
            // cout<<"Command Received : "<<commandRecieved.substr(0, command[0].length())<<endl;
            string gid = command[1];
            // string fpath = command[2];
            string fname = command[2];
             string dpath = command[3];
            DownloadFile(gid, fname, dpath, currentThreadUser, clientSocket, checkNoOfUser);
        }
        else if(command[0] == "show_downloads")
        {
            // cout<<"Command Received : "<<commandRecieved.substr(0, command[0].length())<<endl;

            ShowDownloadStatus(currentThreadUser, clientSocket, checkNoOfUser);

        }
        else if(command[0] == "quit")
        {
            // cout<<"Command Received : "<<commandRecieved.substr(0, command[0].length())<<endl;

            QuitTracker(currentThreadUser, clientSocket, checkNoOfUser);
            // trackerRunning = false;
            // exit(0);
            // break;
        }
        // else
        // {
        //     cout<<"wrong command"<<endl;
        //     string response = "The command u entered does not exists";
        //     send(clientSocket, response.c_str(), response.size(), 0);
        // }


            //Close the client socket
            // close(clientSocket);

        // return 0;
    }
    close(clientSocket);

}




int main(int argc, char *argv[]) 
{
    //checking if correct no of arguments were passed
    if(argc != 3)
    {
        perror("Usage : ./tracker tracker_info.txt tracker_no");
    }
    //extracting IP and port number of tracker from tracker_info.txt passed as argument

    //opening the file tracker_info.txt
    int fd = open(argv[1], O_RDONLY);
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
    }

    close(fd);
    // cout<<extract<<endl;
    
    //extracting IP and port from extract string
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


    // Create a socket and get its descriptor
    int domain = AF_INET;
    int type = SOCK_STREAM;
    int protocol = 0;
    int serverSocket = socket(domain, type, protocol);
    if(serverSocket == -1)
    {
        perror("unable to create a socket of tracker");
        exit(1);
    }

    //setting options for the serverSocket
    int option = 1;
    int setOption = setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &option, sizeof(option));
    if(setOption == -1)
    {
        perror("unable to set options for tracker socket");
        exit(1);
    }

    sockaddr_in serverAddress;

    //The server socket will listen for incoming connections only on the network interface IPaddress
    int validIP = inet_pton(AF_INET, IPAddress.c_str(), &serverAddress.sin_addr);
    if(validIP <= 0)
    {
        perror("Invalid IP address of tracker");
        exit(1);
    }

    //sockaddr_in structure is used in sockets for defining an endpoint address
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(portNumber);  // Convert port number to network byte order (Big-Endian)
    //The sin_port field of the sockaddr_in structure expects the port number to be in network byte order

    // Bind the socket to the specified IP and Port
    int bindFD = bind(serverSocket, (struct sockaddr *)&serverAddress, sizeof(serverAddress));
    if(bindFD == -1)
    {
        perror("unable to bind the socket of tracker");
        exit(1);
    }

    // Start listening for incoming connections, max connection allowed in queue is 50
    int listenFD = listen(serverSocket, MAX_CONNECTION);
    if(listenFD == -1)
    {
        perror("unable to listen to incomming connection on tracker");
        exit(1);
    }
    else
    {
        cout<<"Tracker is listening for request on "<<IPAddress<<":"<<portNumber<<endl;

    }

    //infinite loop to accept connection from various clients
    // bool trackerRunning = true
    while(trackerRunning)
    {
        // cout<<"again entering while loop for next command"<<endl;
        // Data structure to hold client's address information
        sockaddr_in clientAddress;
        socklen_t clientLength = sizeof(clientAddress);

        // Accept an incoming connection from a client
        int clientSocket = accept(serverSocket, (struct sockaddr *)&clientAddress, &clientLength);
        if(clientSocket == -1)
        {
            perror("unable to accept incomming connection from client");
            continue;
        }
        else if(clientSocket > 0)
        {
            cout<<"Accepted Connection from Client"<<endl;
        }
        

        try {
            //creating a new thread object everytime a new request from cllient is accepted
            thread clientThread(clientHandler, clientSocket);
            // cout<<"after making a thread and sending it to client handler"<<endl;

            // Detaches the thread from main() so it can run independently and clean up themselves automatically when they finish execution
            clientThread.detach(); 
        }
        catch(system_error& e)
        {
            cerr << "Unable to create thread for client request: " << e.what() << endl;

        }

        // cout<<"after completing command"<<endl;
    }

    // Close the server socket
    // close(clientSocket);
    // close(serverSocket);

    return 0;
}



