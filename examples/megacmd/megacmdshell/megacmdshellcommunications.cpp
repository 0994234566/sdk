#include "megacmdshell.h"
#include "megacmdshellcommunications.h"

#include <iostream>
#include <thread>
#include <sstream>

#ifdef _WIN32
#include <shlobj.h> //SHGetFolderPath
#include <Shlwapi.h> //PathAppend
#else
#include <fcntl.h>

#include <pwd.h>  //getpwuid_r
#include <signal.h>
#endif

#ifndef INVALID_SOCKET
#define INVALID_SOCKET -1
#endif

using namespace std;


bool MegaCmdShellCommunications::serverinitiatedfromshell;
bool MegaCmdShellCommunications::registerAgainRequired;
bool MegaCmdShellCommunications::confirmResponse;
bool MegaCmdShellCommunications::stopListener;
std::thread *MegaCmdShellCommunications::listenerThread;

bool MegaCmdShellCommunications::socketValid(int socket)
{
#ifdef _WIN32
    return socket != INVALID_SOCKET;
#else
    return socket >= 0;
#endif
}

void MegaCmdShellCommunications::closeSocket(int socket){
#ifdef _WIN32
    closesocket(socket);
#else
    close(socket);
#endif
}


string createAndRetrieveConfigFolder()
{
    string configFolder;

#ifdef _WIN32

   TCHAR szPath[MAX_PATH];
    if (!SUCCEEDED(GetModuleFileName(NULL, szPath , MAX_PATH)))
    {
        cerr << "Couldnt get EXECUTABLE folder" << endl;
    }
    else
    {
        if (SUCCEEDED(PathRemoveFileSpec(szPath)))
        {
            if (PathAppend(szPath,TEXT(".megaCmd")))
            {
                utf16ToUtf8(szPath, lstrlen(szPath), &configFolder); // TODO: resqueue utf16ToUtf8 function
            }
        }
    }
#else
    const char *homedir = NULL;

    homedir = getenv("HOME");
    if (!homedir)
    {
        struct passwd pd;
        struct passwd* pwdptr = &pd;
        struct passwd* tempPwdPtr;
        char pwdbuffer[200];
        int pwdlinelen = sizeof( pwdbuffer );

        if (( getpwuid_r(22, pwdptr, pwdbuffer, pwdlinelen, &tempPwdPtr)) != 0)
        {
            cerr << "Couldnt get HOME folder" << endl;
            return "/tmp";
        }
        else
        {
            homedir = pwdptr->pw_dir;
        }
    }
    stringstream sconfigDir;
    sconfigDir << homedir << "/" << ".megaCmd";
    configFolder = sconfigDir.str();
#endif

    return configFolder;

}

#ifdef _WIN32
int MegaCmdShellCommunications::createSocket(int number, bool net)
#else
int MegaCmdShellCommunications::createSocket(int number, bool net)
#endif
{
    if (net)
    {
        int thesock = socket(AF_INET, SOCK_STREAM, 0);
        if (!socketValid(thesock))
        {
            cerr << "ERROR opening socket: " << ERRNO << endl;
            return INVALID_SOCKET;
        }
        int portno=MEGACMDINITIALPORTNUMBER+number;

        struct sockaddr_in addr;

        memset(&addr, 0, sizeof( addr ));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
        addr.sin_port = htons(portno);

        if (::connect(thesock, (struct sockaddr*)&addr, sizeof( addr )) == SOCKET_ERROR)
        {
            cerr << "Unable to connect to " << (number?("response socket N "+number):"service") << ": error=" << ERRNO << endl;
            if (!number)
            {
#ifdef __linux__
                cerr << "Please ensure mega-cmd is running" << endl;
#else
                cerr << "Please ensure MegaCMD is running" << endl;
#endif

            }
            return INVALID_SOCKET;
        }
        return thesock;
    }

#ifndef _WIN32
    else
    {
        int thesock = socket(AF_UNIX, SOCK_STREAM, 0);
        char socket_path[60];
        if (!socketValid(thesock))
        {
            cerr << "ERROR opening socket: " << ERRNO << endl;
            return INVALID_SOCKET;
        }

        bzero(socket_path, sizeof( socket_path ) * sizeof( *socket_path ));
        if (number)
        {
            sprintf(socket_path, "/tmp/megaCMD_%d/srv_%d", getuid(), number);
        }
        else
        {
            sprintf(socket_path, "/tmp/megaCMD_%d/srv", getuid() );
        }

        struct sockaddr_un addr;

        memset(&addr, 0, sizeof( addr ));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, socket_path, sizeof( addr.sun_path ) - 1);


        if (::connect(thesock, (struct sockaddr*)&addr, sizeof( addr )) == SOCKET_ERROR)
        {
            if (!number)
            {
                //launch server
                #ifdef _WIN32
                        //TODO: implement this for windows
                #else
//                if (fork()) //fork() -> child is megacmdshell (debug megacmd server)
                if (!fork()) //!fork -> child is server. (debug megacmdshell)
                {
                    signal(SIGINT, SIG_IGN); //ignore Ctrl+C in the server

                    string pathtolog = createAndRetrieveConfigFolder()+"/megacmdserver.log";
                    sprintf(socket_path, "/tmp/megaCMD_%d/srv", getuid() );
                    OUTSTREAM << "Server not running. Initiating in the background." << endl;
                    OUTSTREAM << " The output will logged to " << pathtolog << endl;

                    close(0); //close stdin
                    dup2(1, 2);  //redirects stderr to stdout below this line.
                    freopen(pathtolog.c_str(),"w",stdout);

#ifndef NDEBUG //TODO: I fear this might not work via autotools
                    const char executable[] = "../MEGAcmdServer/MEGAcmd";
#else
                    const char executable[] = "mega-cmd"; //TODO: if changed to mega-cmd-server (and kept mega-cmd for the interactive shell, change it here)
#endif
                    char * args[] = {""};

                    int ret = execvp(executable,args);
                    if (ret)
                    {
                        if (errno == 2 )
                        {
                            cerr << "Couln't initiate MEGAcmd server: executable not found: " << executable << endl;

                        }
                        else
                        {
                            cerr << "MEGAcmd server exit with code " << ret << " . errno = " << errno << endl;
                        }
                    }
                    exit(0);
                }

                #endif

                //try again:
                int attempts = 12;
                int waitimet = 1500;
                while ( ::connect(thesock, (struct sockaddr*)&addr, sizeof( addr )) == SOCKET_ERROR && attempts--)
                {
#if _WIN32
                    Sleep(waittime/1000);
#else
                    usleep(waitimet);
#endif
                    waitimet=waitimet*2;
                }
                if (attempts<0)
                {

                    cerr << "Unable to connect to " << (number?("response socket N "+number):"service") << ": error=" << ERRNO << endl;
#ifdef __linux__
                    cerr << "Please ensure mega-cmd is running" << endl;
#else
                    cerr << "Please ensure MegaCMD is running" << endl;
#endif
                    return INVALID_SOCKET;
                }
                else
                {
                    serverinitiatedfromshell = true;
                    registerAgainRequired = true;
                }
            }
        }

        return thesock;
    }
    return INVALID_SOCKET;

#endif
}

MegaCmdShellCommunications::MegaCmdShellCommunications()
{
#ifdef _WIN32
    setlocale(LC_ALL, ""); // en_US.utf8 could do?
#endif


#if _WIN32
    WORD wVersionRequested;
    WSADATA wsaData;
    int err;

    /* Use the MAKEWORD(lowbyte, highbyte) macro declared in Windef.h */
    wVersionRequested = MAKEWORD(2, 2);

    err = WSAStartup(wVersionRequested, &wsaData);
    if (err != 0) {
        cerr << "ERROR initializing WSA" << endl;
    }
#endif

    serverinitiatedfromshell = false;
    registerAgainRequired = false;

    stopListener = false;
    listenerThread = NULL;
}

int MegaCmdShellCommunications::executeCommand(string command, std::ostream &output)
{
    int thesock = createSocket();
    if (thesock == INVALID_SOCKET)
    {
        return INVALID_SOCKET;
    }

    command="X"+command;
    int n = send(thesock,command.data(),command.size(), MSG_NOSIGNAL);
    if (n == SOCKET_ERROR)
    {
        cerr << "ERROR writing output Code to socket: " << ERRNO << endl;
        return -1;
    }

    int receiveSocket = SOCKET_ERROR ;

    n = recv(thesock, (char *)&receiveSocket, sizeof(receiveSocket), MSG_NOSIGNAL);
    if (n == SOCKET_ERROR)
    {
        cerr << "ERROR reading output socket" << endl;
        return -1;
    }

    int newsockfd =createSocket(receiveSocket);
    if (newsockfd == INVALID_SOCKET)
        return INVALID_SOCKET;

    int outcode = -1;

    n = recv(newsockfd, (char *)&outcode, sizeof(outcode), MSG_NOSIGNAL);
    if (n == SOCKET_ERROR)
    {
        cerr << "ERROR reading output code: " << ERRNO << endl;
        return -1;
    }

    while (outcode == MCMD_REQCONFIRM)
    {
        int BUFFERSIZE = 1024;
        char confirmQuestion[1025];
        do{
            n = recv(newsockfd, confirmQuestion, BUFFERSIZE, MSG_NOSIGNAL);
            if (n)
            {
    #ifdef _WIN32
                confirmQuestion[n]='\0';

                // determine the required confirmQuestion size
                size_t wbuffer_size;
                mbstowcs_s(&wbuffer_size, NULL, 0, confirmQuestion, _TRUNCATE);

                // do the actual conversion
                wchar_t *wbuffer = new wchar_t[wbuffer_size];
                mbstowcs_s(&wbuffer_size, wbuffer, wbuffer_size, confirmQuestion, _TRUNCATE);

                wcout << wbuffer; //TODO: use output
                delete [] wbuffer;
    #else
                confirmQuestion[n]='\0';

    #endif
            }
        } while(n == BUFFERSIZE && n !=SOCKET_ERROR);

        bool response = readconfirmationloop(confirmQuestion);

        n = send(newsockfd, (const char *) &response, sizeof(response), MSG_NOSIGNAL);
        if (n == SOCKET_ERROR)
        {
            cerr << "ERROR writing confirm response to to socket: " << ERRNO << endl;
            return -1;
        }

        n = recv(newsockfd, (char *)&outcode, sizeof(outcode), MSG_NOSIGNAL);
        if (n == SOCKET_ERROR)
        {
            cerr << "ERROR reading output code: " << ERRNO << endl;
            return -1;
        }
    }

    int BUFFERSIZE = 1024;
    char buffer[1025];
    do{
        n = recv(newsockfd, buffer, BUFFERSIZE, MSG_NOSIGNAL);
        if (n)
        {
#ifdef _WIN32
            buffer[n]='\0';

            // determine the required buffer size
            size_t wbuffer_size;
            mbstowcs_s(&wbuffer_size, NULL, 0, buffer, _TRUNCATE);

            // do the actual conversion
            wchar_t *wbuffer = new wchar_t[wbuffer_size];
            mbstowcs_s(&wbuffer_size, wbuffer, wbuffer_size, buffer, _TRUNCATE);

            wcout << wbuffer; //TODO: use output
            delete [] wbuffer;
#else
            buffer[n]='\0';
            output << buffer; //TODO: receive outout streams ??
#endif
        }
    } while(n == BUFFERSIZE && n !=SOCKET_ERROR);

    if (n == SOCKET_ERROR)
    {
        cerr << "ERROR reading output: " << ERRNO << endl;
        return -1;;
    }

    closeSocket(newsockfd);
    closeSocket(thesock);
    return outcode;
}

int MegaCmdShellCommunications::listenToStateChanges(int receiveSocket)
{
    int newsockfd = createSocket(receiveSocket);

    int timeout_notified_server_might_be_down = 0;
    while (!stopListener)
    {
        if (newsockfd == INVALID_SOCKET)
            return INVALID_SOCKET;

        string newstate;

        int BUFFERSIZE = 1024;
        char buffer[1025];
        int n = SOCKET_ERROR;
        do{
            n = recv(newsockfd, buffer, BUFFERSIZE, MSG_NOSIGNAL);
            if (n)
            {
#ifdef _WIN32
                buffer[n]='\0';

                // determine the required buffer size
                size_t wbuffer_size;
                mbstowcs_s(&wbuffer_size, NULL, 0, buffer, _TRUNCATE);

                // do the actual conversion
                wchar_t *wbuffer = new wchar_t[wbuffer_size];
                mbstowcs_s(&wbuffer_size, wbuffer, wbuffer_size, buffer, _TRUNCATE);

                //            wcout << wbuffer; //TODO: review windows version
                newstate += buffer;
                delete [] wbuffer;
#else
                buffer[n]='\0';
                newstate += buffer;
#endif
            }
        } while(n == BUFFERSIZE && n !=SOCKET_ERROR);

        if (n == SOCKET_ERROR)
        {
            cerr << "ERROR reading output: " << ERRNO << endl;
            return -1;;
        }

        if (!n)
        {
            if (!timeout_notified_server_might_be_down)
            {
                timeout_notified_server_might_be_down = 30;
                cerr << endl << "Server is probably down. Executing anything will try to respawn it.";
            }
            timeout_notified_server_might_be_down--;
            sleepSeconds(1);
            continue;
        }

        if (newstate.compare(0, strlen("prompt:"), "prompt:") == 0)
        {
            changeprompt(newstate.substr(strlen("prompt:")).c_str(),true);
        }
        else
        {
            cerr << "received unrecognized state change: " << newstate << endl;
            //sleep a while to avoid continuous looping
            sleepSeconds(1);
        }

    }

    closeSocket(newsockfd);
}

int MegaCmdShellCommunications::registerForStateChanges()
{
    int thesock = createSocket();
    if (thesock == INVALID_SOCKET)
    {
        return INVALID_SOCKET;
    }

    string command="registerstatelistener";
    int n = send(thesock,command.data(),command.size(), MSG_NOSIGNAL);
    if (n == SOCKET_ERROR)
    {
        cerr << "ERROR writing output Code to socket: " << ERRNO << endl;
        return -1;;
    }

    int receiveSocket = SOCKET_ERROR ;

    n = recv(thesock, (char *)&receiveSocket, sizeof(receiveSocket), MSG_NOSIGNAL);
    if (n == SOCKET_ERROR)
    {
        cerr << "ERROR reading output socket" << endl;
        return -1;;
    }

    if (listenerThread != NULL)
    {
        stopListener = true;
        listenerThread->join();
    }

    stopListener = false;

    listenerThread = new std::thread(listenToStateChanges,receiveSocket);


    closeSocket(thesock);
    return 0;
}

void MegaCmdShellCommunications::setResponseConfirmation(bool confirmation)
{
    confirmResponse = confirmation;
}

MegaCmdShellCommunications::~MegaCmdShellCommunications()
{
#if _WIN32
    WSACleanup();
#endif

    if (listenerThread != NULL)
    {
        stopListener = true;
        listenerThread->join();
    }

}