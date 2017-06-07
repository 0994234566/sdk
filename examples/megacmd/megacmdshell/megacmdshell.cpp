/**
 * @file examples/megacmd/megacmdshell.cpp
 * @brief MegaCMD: Interactive CLI and service application
 * This is the shell application
 *
 * (c) 2013-2016 by Mega Limited, Auckland, New Zealand
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * Applications using the MEGA API must present a valid application key
 * and comply with the the rules set forth in the Terms of Service.
 *
 * The MEGA SDK is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * @copyright Simplified (2-clause) BSD License.
 *
 * You should have received a copy of the license along with this
 * program.
 */

#include "megacmdshell.h"
#include "megacmdshellcommunications.h"


//#include "megacmd.h"

//#include "megacmdsandbox.h"
//#include "megacmdexecuter.h"
//#include "megacmdutils.h"
//#include "configurationmanager.h"
//#include "megacmdlogger.h"
//#include "comunicationsmanager.h"
//#include "listeners.h"

//#include "megacmdplatform.h"
//#include "megacmdversion.h"

#define USE_VARARGS
#define PREFER_STDARG

#include <readline/readline.h>
#include <readline/history.h>
#include <iomanip>
#include <string>
#include <set>
#include <map>
#include <vector>
#include <sstream>
#include <algorithm>

//TODO: check includes ok in windows:
#include <sys/types.h>
#include <unistd.h>
#include <termios.h>


#ifndef _WIN32
#include <signal.h>
#endif


//TODO: implement propper loggin
#define LOG_verbose COUT
#define LOG_debug COUT
#define LOG_err COUT
#define LOG_warn COUT
#define LOG_fatal COUT

using namespace std;


//TODO: move utils functions somewhere else (common with megacmdserver?)


vector<string> getlistOfWords(char *ptr)
{
    vector<string> words;

    char* wptr;

    // split line into words with quoting and escaping
    for (;; )
    {
        // skip leading blank space
        while (*ptr > 0 && *ptr <= ' ')
        {
            ptr++;
        }

        if (!*ptr)
        {
            break;
        }

        // quoted arg / regular arg
        if (*ptr == '"')
        {
            ptr++;
            wptr = ptr;
            words.push_back(string());

            for (;; )
            {
                if (( *ptr == '"' ) || ( *ptr == '\\' ) || !*ptr)
                {
                    words[words.size() - 1].append(wptr, ptr - wptr);

                    if (!*ptr || ( *ptr++ == '"' ))
                    {
                        break;
                    }

                    wptr = ptr - 1;
                }
                else
                {
                    ptr++;
                }
            }
        }
        else if (*ptr == '\'') // quoted arg / regular arg
        {
            ptr++;
            wptr = ptr;
            words.push_back(string());

            for (;; )
            {
                if (( *ptr == '\'' ) || ( *ptr == '\\' ) || !*ptr)
                {
                    words[words.size() - 1].append(wptr, ptr - wptr);

                    if (!*ptr || ( *ptr++ == '\'' ))
                    {
                        break;
                    }

                    wptr = ptr - 1;
                }
                else
                {
                    ptr++;
                }
            }
        }
        else
        {
            wptr = ptr;

            char *prev = ptr;
            //while ((unsigned char)*ptr > ' ')
            while ((*ptr != '\0') && !(*ptr ==' ' && *prev !='\\'))
            {
                if (*ptr == '"')
                {
                    while (*++ptr != '"' && *ptr != '\0')
                    { }
                }
                prev=ptr;
                ptr++;
            }

            words.push_back(string(wptr, ptr - wptr));
        }
    }

    return words;
}

bool stringcontained(const char * s, vector<string> list)
{
    for (int i = 0; i < (int)list.size(); i++)
    {
        if (list[i] == s)
        {
            return true;
        }
    }

    return false;
}
char * dupstr(char* s)
{
    char *r;

    r = (char*)malloc(sizeof( char ) * ( strlen(s) + 1 ));
    strcpy(r, s);
    return( r );
}


bool replace(std::string& str, const std::string& from, const std::string& to)
{
    size_t start_pos = str.find(from);
    if (start_pos == std::string::npos)
    {
        return false;
    }
    str.replace(start_pos, from.length(), to);
    return true;
}

void replaceAll(std::string& str, const std::string& from, const std::string& to)
{
    if (from.empty())
    {
        return;
    }
    size_t start_pos = 0;
    while (( start_pos = str.find(from, start_pos)) != std::string::npos)
    {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length();
    }
}

bool hasWildCards(string &what)
{
    return what.find('*') != string::npos || what.find('?') != string::npos;
}

bool isRegExp(string what)
{
#ifdef USE_PCRE
    if (( what == "." ) || ( what == ".." ) || ( what == "/" ))
    {
        return false;
    }

    while (true){
        if (what.find("./") == 0)
        {
            what=what.substr(2);
        }
        else if(what.find("../") == 0)
        {
            what=what.substr(3);
        }
        else if(what.size()>=3 && (what.find("/..") == what.size()-3))
        {
            what=what.substr(0,what.size()-3);
        }
        else if(what.size()>=2 && (what.find("/.") == what.size()-2))
        {
            what=what.substr(0,what.size()-2);
        }
        else if(what.size()>=2 && (what.find("/.") == what.size()-2))
        {
            what=what.substr(0,what.size()-2);
        }
        else
        {
            break;
        }
    }
    replaceAll(what, "/../", "/");
    replaceAll(what, "/./", "/");
    replaceAll(what, "/", "");

    string s = pcrecpp::RE::QuoteMeta(what);
    string ns = s;
    replaceAll(ns, "\\\\\\", "\\");
    bool isregex = strcmp(what.c_str(), ns.c_str());
    return isregex;

#elif __cplusplus >= 201103L
    //TODO??
#endif
    return hasWildCards(what);
}


string getCurrentThreadLine() //TODO: rename to sth more sensefull
{
    char *saved_line = rl_copy_text(0, rl_point);
    string toret(saved_line);
    free(saved_line);
    return toret;

}


/// end utily functions




#ifdef _WIN32
// convert UTF-8 to Windows Unicode wstring
void stringtolocalw(const char* path, std::wstring* local)
{
    // make space for the worst case
    local->resize((strlen(path) + 1) * sizeof(wchar_t));

    int wchars_num = MultiByteToWideChar(CP_UTF8, 0, path,-1, NULL,0);
    local->resize(wchars_num);

    int len = MultiByteToWideChar(CP_UTF8, 0, path,-1, (wchar_t*)local->data(), wchars_num);

    if (len)
    {
        local->resize(len-1);
    }
    else
    {
        local->clear();
    }
}

//override << operators for wostream for string and const char *

std::wostream & operator<< ( std::wostream & ostr, std::string const & str )
{
    std::wstring toout;
    stringtolocalw(str.c_str(),&toout);
    ostr << toout;

return ( ostr );
}

std::wostream & operator<< ( std::wostream & ostr, const char * str )
{
    std::wstring toout;
    stringtolocalw(str,&toout);
    ostr << toout;
    return ( ostr );
}

//override for the log. This is required for compiling, otherwise SimpleLog won't compile. FIXME
std::ostringstream & operator<< ( std::ostringstream & ostr, std::wstring const &str)
{
    //TODO: localtostring
    //std::wstring toout;
    //stringtolocalw(str,&toout);
    //ostr << toout;
    return ( ostr );
}


#endif

//#ifdef _WIN32
//#include "comunicationsmanagerportsockets.h"
//#define COMUNICATIONMANAGER ComunicationsManagerPortSockets
//#else
//#include "comunicationsmanagerfilesockets.h"
//#define COMUNICATIONMANAGER ComunicationsManagerFileSockets
//#include <signal.h>
//#endif

//using namespace mega;

//MegaCmdExecuter *cmdexecuter;
//MegaCmdSandbox *sandboxCMD;

//MegaSemaphore semaphoreClients; //to limit max parallel petitions

//MegaApi *api;

////api objects for folderlinks
//std::queue<MegaApi *> apiFolders;
//std::vector<MegaApi *> occupiedapiFolders;
//MegaSemaphore semaphoreapiFolders;
//MegaMutex mutexapiFolders;

//MegaCMDLogger *loggerCMD;

//MegaMutex mutexEndedPetitionThreads;
//std::vector<MegaThread *> petitionThreads;
//std::vector<MegaThread *> endedPetitionThreads;


////Comunications Manager
//ComunicationsManager * cm;

//// global listener
//MegaCmdGlobalListener* megaCmdGlobalListener;

//MegaCmdMegaListener* megaCmdMegaListener;

//bool loginInAtStartup = false;

string validGlobalParameters[] = {"v", "help"};

string alocalremotefolderpatterncommands [] = {"sync"};
vector<string> localremotefolderpatterncommands(alocalremotefolderpatterncommands, alocalremotefolderpatterncommands + sizeof alocalremotefolderpatterncommands / sizeof alocalremotefolderpatterncommands[0]);

string aremotepatterncommands[] = {"export", "find", "attr"};
vector<string> remotepatterncommands(aremotepatterncommands, aremotepatterncommands + sizeof aremotepatterncommands / sizeof aremotepatterncommands[0]);

string aremotefolderspatterncommands[] = {"cd", "share"};
vector<string> remotefolderspatterncommands(aremotefolderspatterncommands, aremotefolderspatterncommands + sizeof aremotefolderspatterncommands / sizeof aremotefolderspatterncommands[0]);

string amultipleremotepatterncommands[] = {"ls", "mkdir", "rm", "du"};
vector<string> multipleremotepatterncommands(amultipleremotepatterncommands, amultipleremotepatterncommands + sizeof amultipleremotepatterncommands / sizeof amultipleremotepatterncommands[0]);

string aremoteremotepatterncommands[] = {"mv", "cp"};
vector<string> remoteremotepatterncommands(aremoteremotepatterncommands, aremoteremotepatterncommands + sizeof aremoteremotepatterncommands / sizeof aremoteremotepatterncommands[0]);

string aremotelocalpatterncommands[] = {"get", "thumbnail", "preview"};
vector<string> remotelocalpatterncommands(aremotelocalpatterncommands, aremotelocalpatterncommands + sizeof aremotelocalpatterncommands / sizeof aremotelocalpatterncommands[0]);

string alocalpatterncommands [] = {"lcd"};
vector<string> localpatterncommands(alocalpatterncommands, alocalpatterncommands + sizeof alocalpatterncommands / sizeof alocalpatterncommands[0]);

string aemailpatterncommands [] = {"invite", "signup", "ipc", "users"};
vector<string> emailpatterncommands(aemailpatterncommands, aemailpatterncommands + sizeof aemailpatterncommands / sizeof aemailpatterncommands[0]);

string avalidCommands [] = { "login", "signup", "confirm", "session", "mount", "ls", "cd", "log", "debug", "pwd", "lcd", "lpwd", "import",
                             "put", "get", "attr", "userattr", "mkdir", "rm", "du", "mv", "cp", "sync", "export", "share", "invite", "ipc",
                             "showpcr", "users", "speedlimit", "killsession", "whoami", "help", "passwd", "reload", "logout", "version", "quit",
                             "history", "thumbnail", "preview", "find", "completion", "clear", "https", "transfers"
#ifdef _WIN32
                             ,"unicode"
#endif
                           };
vector<string> validCommands(avalidCommands, avalidCommands + sizeof avalidCommands / sizeof avalidCommands[0]);


// password change-related state information
string oldpasswd;
string newpasswd;

bool doExit = false;
bool consoleFailed = false;

bool handlerinstalled = false;

static char dynamicprompt[128];

static char* line;

static prompttype prompt = COMMAND;

static char pw_buf[256];
static int pw_buf_pos;

// communications with megacmdserver:
MegaCmdShellCommunications *comms;


//// local console
//Console* console; //TODO: use MEGA console // repeat sources??? // include them as in the sdk.pri

//MegaMutex mutexHistory;

//map<int, string> threadline;

void printWelcomeMsg();

//string getCurrentThreadLine()
//{
//    uint64_t currentThread = MegaThread::currentThreadId();
//    if (threadline.find(currentThread) == threadline.end())
//    {
//        char *saved_line = rl_copy_text(0, rl_point);
//        string toret(saved_line);
//        free(saved_line);
//        return toret;
//    }
//    else
//    {
//        return threadline[currentThread];
//    }
//}

//void setCurrentThreadLine(string s)
//{
//    threadline[MegaThread::currentThreadId()] = s;
//}

//void setCurrentThreadLine(const vector<string>& vec)
//{
//   setCurrentThreadLine(joinStrings(vec));
//}

void sigint_handler(int signum)
{
//    LOG_verbose << "Received signal: " << signum;

//    if (loginInAtStartup)
//    {
//        exit(-2);
//    }

    if (prompt != COMMAND)
    {
        setprompt(COMMAND);
    }

    // reset position and print prompt
    rl_replace_line("", 0); //clean contents of actual command
    rl_crlf(); //move to nextline

    if (RL_ISSTATE(RL_STATE_ISEARCH) || RL_ISSTATE(RL_STATE_ISEARCH) || RL_ISSTATE(RL_STATE_ISEARCH))
    {
        RL_UNSETSTATE(RL_STATE_ISEARCH);
        RL_UNSETSTATE(RL_STATE_NSEARCH);
        RL_UNSETSTATE( RL_STATE_SEARCH);
        history_set_pos(history_length);
        rl_restore_prompt(); // readline has stored it when searching
    }
    else
    {
        rl_reset_line_state();
    }
    rl_redisplay();
}

#ifdef _WIN32
BOOL CtrlHandler( DWORD fdwCtrlType )
{
  LOG_verbose << "Reached CtrlHandler: " << fdwCtrlType;

  switch( fdwCtrlType )
  {
    // Handle the CTRL-C signal.
    case CTRL_C_EVENT:
       sigint_handler((int)fdwCtrlType);
      return( TRUE );

    default:
      return FALSE;
  }
}
#endif

prompttype getprompt()
{
    return prompt;
}

void setprompt(prompttype p, string arg)
{
    prompt = p;

    if (p == COMMAND)
    {
//        console->setecho(true); //TODO: use console
    }
    else
    {
        pw_buf_pos = 0;
        if (arg.size())
        {
            OUTSTREAM << arg << flush;
        }
        else
        {
            OUTSTREAM << prompts[p] << flush;
        }

//        console->setecho(false);//TODO: use console
    }
}


// readline callback - exit if EOF, add to history unless password
static void store_line(char* l)
{
    if (!l)
    {
#ifndef _WIN32 // to prevent exit with Supr key
        doExit = true;
        rl_set_prompt("(CTRL+D) Exiting ...\n");
        if (comms->serverinitiatedfromshell)
        {
            comms->executeCommand("exit");
        }
#endif
        return;
    }

    if (*l && ( prompt == COMMAND ))
    {
//        mutexHistory.lock(); //TODO: use mutex!
        add_history(l);
//        mutexHistory.unlock();
    }

    line = l;
}

void changeprompt(const char *newprompt, bool redisplay)
{
    strncpy(dynamicprompt, newprompt, sizeof( dynamicprompt ));

    if (redisplay)
    {
        rl_crlf();
//        rl_redisplay();
//        rl_forced_update_display(); //this is not enough.  The problem is that the prompt is actually printed when calling rl_callback_handeler_install or after an enter
        rl_callback_handler_install(*dynamicprompt ? dynamicprompt : prompts[COMMAND], store_line);
        handlerinstalled = true;

        //TODO: problems:
        // - concurrency issues?
        // - current command being written is lost here //play with save_line. Again, deal with concurrency
        // - whenever I fix the hang when I was the one that triggered the state change. If I was in a new line and handler_install was already called none of this should be
        //    required. But what if the external communication was faster or slower than me. Think and test

    }
}

void insertValidParamsPerCommand(set<string> *validParams, string thecommand, set<string> *validOptValues = NULL)
{
    if (!validOptValues)
    {
        validOptValues = validParams;
    }
    if ("ls" == thecommand)
    {
        validParams->insert("R");
        validParams->insert("r");
        validParams->insert("l");
    }
    else if ("du" == thecommand)
    {
        validParams->insert("h");
    }
    else if ("help" == thecommand)
    {
        validParams->insert("f");
        validParams->insert("non-interactive");
        validParams->insert("upgrade");
    }
    else if ("version" == thecommand)
    {
        validParams->insert("l");
        validParams->insert("c");
    }
    else if ("rm" == thecommand)
    {
        validParams->insert("r");
        validParams->insert("f");
    }
    else if ("speedlimit" == thecommand)
    {
        validParams->insert("u");
        validParams->insert("d");
        validParams->insert("h");
    }
    else if ("whoami" == thecommand)
    {
        validParams->insert("l");
    }
    else if ("log" == thecommand)
    {
        validParams->insert("c");
        validParams->insert("s");
    }
    else if ("sync" == thecommand)
    {
        validParams->insert("d");
        validParams->insert("s");
    }
    else if ("export" == thecommand)
    {
        validParams->insert("a");
        validParams->insert("d");
        validOptValues->insert("expire");
    }
    else if ("share" == thecommand)
    {
        validParams->insert("a");
        validParams->insert("d");
        validParams->insert("p");
        validOptValues->insert("with");
        validOptValues->insert("level");
        validOptValues->insert("personal-representation");
    }
    else if ("find" == thecommand)
    {
        validOptValues->insert("pattern");
        validOptValues->insert("l");
    }
    else if ("mkdir" == thecommand)
    {
        validParams->insert("p");
    }
    else if ("users" == thecommand)
    {
        validParams->insert("s");
        validParams->insert("h");
        validParams->insert("d");
        validParams->insert("n");
    }
    else if ("killsession" == thecommand)
    {
        validParams->insert("a");
    }
    else if ("invite" == thecommand)
    {
        validParams->insert("d");
        validParams->insert("r");
        validOptValues->insert("message");
    }
    else if ("signup" == thecommand)
    {
        validParams->insert("name");
    }
    else if ("logout" == thecommand)
    {
        validParams->insert("keep-session");
    }
    else if ("attr" == thecommand)
    {
        validParams->insert("d");
        validParams->insert("s");
    }
    else if ("userattr" == thecommand)
    {
        validOptValues->insert("user");
        validParams->insert("s");
    }
    else if ("ipc" == thecommand)
    {
        validParams->insert("a");
        validParams->insert("d");
        validParams->insert("i");
    }
    else if ("thumbnail" == thecommand)
    {
        validParams->insert("s");
    }
    else if ("preview" == thecommand)
    {
        validParams->insert("s");
    }
    else if ("put" == thecommand)
    {
        validParams->insert("c");
        validParams->insert("q");
        validParams->insert("ignore-quota-warn");
    }
    else if ("get" == thecommand)
    {
        validParams->insert("m");
        validParams->insert("q");
        validParams->insert("ignore-quota-warn");
    }
    else if ("transfers" == thecommand)
    {
        validParams->insert("show-completed");
        validParams->insert("only-uploads");
        validParams->insert("only-completed");
        validParams->insert("only-downloads");
        validParams->insert("show-syncs");
        validParams->insert("c");
        validParams->insert("a");
        validParams->insert("p");
        validParams->insert("r");
        validOptValues->insert("limit");
        validOptValues->insert("path-display-size");
    }
}

void escapeEspace(string &orig)
{
    replaceAll(orig," ", "\\ ");
}

void unescapeEspace(string &orig)
{
    replaceAll(orig,"\\ ", " ");
}


char* empty_completion(const char* text, int state)
{
    // we offer 2 different options so that it doesn't complete (no space is inserted)
    if (state == 0)
    {
        return strdup(" ");
    }
    if (state == 1)
    {
        return strdup(text);
    }
    return NULL;
}

//char* generic_completion(const char* text, int state, vector<string> validOptions)
//{
//    static size_t list_index, len;
//    static bool foundone;
//    string name;
//    if (!validOptions.size()) // no matches
//    {
//        return empty_completion(text,state); //dont fall back to filenames
//    }
//    if (!state)
//    {
//        list_index = 0;
//        foundone = false;
//        len = strlen(text);
//    }
//    while (list_index < validOptions.size())
//    {
//        name = validOptions.at(list_index);
//        if (!rl_completion_quote_character) {
////        if (!rl_completion_quote_character && interactiveThread()) {
//            escapeEspace(name);
//        }

//        list_index++;

//        if (!( strcmp(text, "")) || (( name.size() >= len ) && ( strlen(text) >= len ) && ( name.find(text) == 0 )))
//        {
//            if (name.size() && (( name.at(name.size() - 1) == '=' ) || ( name.at(name.size() - 1) == '/' )))
//            {
//                rl_completion_suppress_append = 1;
//            }
//            foundone = true;
//            return dupstr((char*)name.c_str());
//        }
//    }

//    if (!foundone)
//    {
//        return empty_completion(text,state); //dont fall back to filenames
//    }

//    return((char*)NULL );
//}

//char* commands_completion(const char* text, int state)
//{
//    return generic_completion(text, state, validCommands);
//}

//char* local_completion(const char* text, int state)
//{
//    return((char*)NULL );  //matches will be NULL: readline will use local completion
//}

//void addGlobalFlags(set<string> *setvalidparams)
//{
//    for (size_t i = 0; i < sizeof( validGlobalParameters ) / sizeof( *validGlobalParameters ); i++)
//    {
//        setvalidparams->insert(validGlobalParameters[i]);
//    }
//}

//char * flags_completion(const char*text, int state)
//{
//    static vector<string> validparams;
//    if (state == 0)
//    {
//        validparams.clear();
//        char *saved_line = strdup(getCurrentThreadLine().c_str());
//        vector<string> words = getlistOfWords(saved_line);
//        free(saved_line);
//        if (words.size())
//        {
//            set<string> setvalidparams;
//            set<string> setvalidOptValues;
//            addGlobalFlags(&setvalidparams);

//            string thecommand = words[0];
//            insertValidParamsPerCommand(&setvalidparams, thecommand, &setvalidOptValues);
//            set<string>::iterator it;
//            for (it = setvalidparams.begin(); it != setvalidparams.end(); it++)
//            {
//                string param = *it;
//                string toinsert;

//                if (param.size() > 1)
//                {
//                    toinsert = "--" + param;
//                }
//                else
//                {
//                    toinsert = "-" + param;
//                }

//                validparams.push_back(toinsert);
//            }

//            for (it = setvalidOptValues.begin(); it != setvalidOptValues.end(); it++)
//            {
//                string param = *it;
//                string toinsert;

//                if (param.size() > 1)
//                {
//                    toinsert = "--" + param + '=';
//                }
//                else
//                {
//                    toinsert = "-" + param + '=';
//                }

//                validparams.push_back(toinsert);
//            }
//        }
//    }
//    char *toret = generic_completion(text, state, validparams);
//    return toret;
//}

//char * flags_value_completion(const char*text, int state)
//{
//    static vector<string> validValues;

//    if (state == 0)
//    {
//        validValues.clear();

//        char *saved_line = strdup(getCurrentThreadLine().c_str());
//        vector<string> words = getlistOfWords(saved_line);
//        free(saved_line);
//        if (words.size() > 1)
//        {
//            string thecommand = words[0];
//            string currentFlag = words[words.size() - 1];

//            map<string, string> cloptions;
//            map<string, int> clflags;

//            set<string> validParams;

//            insertValidParamsPerCommand(&validParams, thecommand);

//            if (setOptionsAndFlags(&cloptions, &clflags, &words, validParams, true))
//            {
//                // return invalid??
//            }

//            if (thecommand == "share")
//            {
//                if (currentFlag.find("--level=") == 0)
//                {
//                    string prefix = strncmp(text, "--level=", strlen("--level="))?"":"--level=";
//                    validValues.push_back(prefix+getShareLevelStr(MegaShare::ACCESS_UNKNOWN));
//                    validValues.push_back(prefix+getShareLevelStr(MegaShare::ACCESS_READ));
//                    validValues.push_back(prefix+getShareLevelStr(MegaShare::ACCESS_READWRITE));
//                    validValues.push_back(prefix+getShareLevelStr(MegaShare::ACCESS_FULL));
//                    validValues.push_back(prefix+getShareLevelStr(MegaShare::ACCESS_OWNER));
//                    validValues.push_back(prefix+getShareLevelStr(MegaShare::ACCESS_UNKNOWN));
//                }
//                if (currentFlag.find("--with=") == 0)
//                {
////                    validValues = cmdexecuter->getlistusers(); //TODO: this should be asked via socket
//                    string prefix = strncmp(text, "--with=", strlen("--with="))?"":"--with=";
//                    for (u_int i=0;i<validValues.size();i++)
//                    {
//                        validValues.at(i)=prefix+validValues.at(i);
//                    }
//                }
//            }
//            if (( thecommand == "userattr" ) && ( currentFlag.find("--user=") == 0 ))
//            {
////                validValues = cmdexecuter->getlistusers(); //TODO: this should be asked via socket
//                string prefix = strncmp(text, "--user=", strlen("--user="))?"":"--user=";
//                for (u_int i=0;i<validValues.size();i++)
//                {
//                    validValues.at(i)=prefix+validValues.at(i);
//                }
//            }
//        }
//    }

//    char *toret = generic_completion(text, state, validValues);
//    return toret;
//}

//void unescapeifRequired(string &what)
//{
//    if (!rl_completion_quote_character) {
//        return unescapeEspace(what);
//    }
//}

//char* remotepaths_completion(const char* text, int state)
//{
//    static vector<string> validpaths;
//    if (state == 0)
//    {
//        string wildtext(text);
//#ifdef USE_PCRE
//        wildtext += ".";
//#elif __cplusplus >= 201103L
//        wildtext += ".";
//#endif
//        wildtext += "*";

//        if (!rl_completion_quote_character) {
//            unescapeEspace(wildtext);
//        }
////        validpaths = cmdexecuter->listpaths(wildtext); //TODO: this should be asked via socket
//    }
//    return generic_completion(text, state, validpaths);
//}

//char* remotefolders_completion(const char* text, int state)
//{
//    static vector<string> validpaths;
//    if (state == 0)
//    {
//        string wildtext(text);
//#ifdef USE_PCRE
//        wildtext += ".";
//#elif __cplusplus >= 201103L
//        wildtext += ".";
//#endif
//        wildtext += "*";

////        validpaths = cmdexecuter->listpaths(wildtext, true); //TODO: this should be asked via socket
//    }
//    return generic_completion(text, state, validpaths);
//}

//char* loglevels_completion(const char* text, int state)
//{
//    static vector<string> validloglevels;
//    if (state == 0)
//    {
//        validloglevels.push_back(getLogLevelStr(MegaApi::LOG_LEVEL_FATAL));
//        validloglevels.push_back(getLogLevelStr(MegaApi::LOG_LEVEL_ERROR));
//        validloglevels.push_back(getLogLevelStr(MegaApi::LOG_LEVEL_WARNING));
//        validloglevels.push_back(getLogLevelStr(MegaApi::LOG_LEVEL_INFO));
//        validloglevels.push_back(getLogLevelStr(MegaApi::LOG_LEVEL_DEBUG));
//        validloglevels.push_back(getLogLevelStr(MegaApi::LOG_LEVEL_MAX));
//    }
//    return generic_completion(text, state, validloglevels);
//}

//char* contacts_completion(const char* text, int state)
//{
//    static vector<string> validcontacts;
//    if (state == 0)
//    {
////        validcontacts = cmdexecuter->getlistusers(); //TODO: this should be asked via socket
//    }
//    return generic_completion(text, state, validcontacts);
//}

//char* sessions_completion(const char* text, int state)
//{
//    static vector<string> validSessions;
//    if (state == 0)
//    {
////        validSessions = cmdexecuter->getsessions(); //TODO: this should be asked via socket
//    }

//    if (validSessions.size() == 0)
//    {
//        return empty_completion(text, state);
//    }

//    return generic_completion(text, state, validSessions);
//}

//char* nodeattrs_completion(const char* text, int state)
//{
//    static vector<string> validAttrs;
//    if (state == 0)
//    {
//        validAttrs.clear();
//        char *saved_line = strdup(getCurrentThreadLine().c_str());
//        vector<string> words = getlistOfWords(saved_line);
//        free(saved_line);
//        if (words.size() > 1)
//        {
////            validAttrs = cmdexecuter->getNodeAttrs(words[1]); //TODO: this should be asked via socket
//        }
//    }

//    if (validAttrs.size() == 0)
//    {
//        return empty_completion(text, state);
//    }

//    return generic_completion(text, state, validAttrs);
//}

//char* userattrs_completion(const char* text, int state)
//{
//    static vector<string> validAttrs;
//    if (state == 0)
//    {
//        validAttrs.clear();
////        validAttrs = cmdexecuter->getUserAttrs(); //TODO: this should be asked via socket
//    }

//    if (validAttrs.size() == 0)
//    {
//        return empty_completion(text, state);
//    }

//    return generic_completion(text, state, validAttrs);
//}

//void discardOptionsAndFlags(vector<string> *ws)
//{
//    for (std::vector<string>::iterator it = ws->begin(); it != ws->end(); )
//    {
//        /* std::cout << *it; ... */
//        string w = ( string ) * it;
//        if (w.length() && ( w.at(0) == '-' )) //begins with "-"
//        {
//            it = ws->erase(it);
//        }
//        else //not an option/flag
//        {
//            ++it;
//        }
//    }
//}

//rl_compentry_func_t *getCompletionFunction(vector<string> words)
//{
//    // Strip words without flags
//    string thecommand = words[0];

//    if (words.size() > 1)
//    {
//        string lastword = words[words.size() - 1];
//        if (lastword.find_first_of("-") == 0)
//        {
//            if (lastword.find_last_of("=") != string::npos)
//            {
//                return flags_value_completion;
//            }
//            else
//            {
//                return flags_completion;
//            }
//        }
//    }
//    discardOptionsAndFlags(&words);

//    int currentparameter = words.size() - 1;
//    if (stringcontained(thecommand.c_str(), localremotefolderpatterncommands))
//    {
//        if (currentparameter == 1)
//        {
//            return local_completion;
//        }
//        if (currentparameter == 2)
//        {
//            return remotefolders_completion;
//        }
//    }
//    else if (thecommand == "put")
//    {
//        if (currentparameter == 1)
//        {
//            return local_completion;
//        }
//        else
//        {
//            return remotepaths_completion;
//        }
//    }
//    else if (stringcontained(thecommand.c_str(), remotepatterncommands))
//    {
//        if (currentparameter == 1)
//        {
//            return remotepaths_completion;
//        }
//    }
//    else if (stringcontained(thecommand.c_str(), remotefolderspatterncommands))
//    {
//        if (currentparameter == 1)
//        {
//            return remotefolders_completion;
//        }
//    }
//    else if (stringcontained(thecommand.c_str(), multipleremotepatterncommands))
//    {
//        if (currentparameter >= 1)
//        {
//            return remotepaths_completion;
//        }
//    }
//    else if (stringcontained(thecommand.c_str(), localpatterncommands))
//    {
//        if (currentparameter == 1)
//        {
//            return local_completion;
//        }
//    }
//    else if (stringcontained(thecommand.c_str(), remoteremotepatterncommands))
//    {
//        if (( currentparameter == 1 ) || ( currentparameter == 2 ))
//        {
//            return remotepaths_completion;
//        }
//    }
//    else if (stringcontained(thecommand.c_str(), remotelocalpatterncommands))
//    {
//        if (currentparameter == 1)
//        {
//            return remotepaths_completion;
//        }
//        if (currentparameter == 2)
//        {
//            return local_completion;
//        }
//    }
//    else if (stringcontained(thecommand.c_str(), emailpatterncommands))
//    {
//        if (currentparameter == 1)
//        {
//            return contacts_completion;
//        }
//    }
//    else if (thecommand == "import")
//    {
//        if (currentparameter == 2)
//        {
//            return remotepaths_completion;
//        }
//    }
//    else if (thecommand == "killsession")
//    {
//        if (currentparameter == 1)
//        {
//            return sessions_completion;
//        }
//    }
//    else if (thecommand == "attr")
//    {
//        if (currentparameter == 1)
//        {
//            return remotepaths_completion;
//        }
//        if (currentparameter == 2)
//        {
//            return nodeattrs_completion;
//        }
//    }
//    else if (thecommand == "userattr")
//    {
//        if (currentparameter == 1)
//        {
//            return userattrs_completion;
//        }
//    }
//    else if (thecommand == "log")
//    {
//        if (currentparameter == 1)
//        {
//            return loglevels_completion;
//        }
//    }
//    return empty_completion;
//}


char* generic_completion(const char* text, int state, vector<string> validOptions)
{
    static size_t list_index, len;
    static bool foundone;
    string name;
    if (!validOptions.size()) // no matches
    {
        return empty_completion(text,state); //dont fall back to filenames
    }
    if (!state)
    {
        list_index = 0;
        foundone = false;
        len = strlen(text);
    }
    while (list_index < validOptions.size())
    {
        name = validOptions.at(list_index);
        if (!rl_completion_quote_character) {
            escapeEspace(name);
        }

        list_index++;

        if (!( strcmp(text, "")) || (( name.size() >= len ) && ( strlen(text) >= len ) && ( name.find(text) == 0 )))
        {
            if (name.size() && (( name.at(name.size() - 1) == '=' ) || ( name.at(name.size() - 1) == '/' )))
            {
                rl_completion_suppress_append = 1;
            }
            foundone = true;
            return dupstr((char*)name.c_str());
        }
    }

    if (!foundone)
    {
        return empty_completion(text,state); //dont fall back to filenames
    }

    return((char*)NULL );
}

inline bool ends_with(std::string const & value, std::string const & ending)
{
    if (ending.size() > value.size()) return false;
    return std::equal(ending.rbegin(), ending.rend(), value.rbegin());
}

char* remote_completion(const char* text, int state)
{

    char *saved_line = strdup(getCurrentThreadLine().c_str());

    static vector<string> validOptions;
    if (state == 0)
    {
        validOptions.clear();
        string completioncommand("completionshell ");
        completioncommand+=saved_line;
//        if (ends_with(completioncommand))

        string s;
        ostringstream oss(s);

        comms->executeCommand(completioncommand,oss);

        string outputcommand = oss.str();
//        cout << "OUTPUT completion values: " << outputcommand << endl; //TODO: delete


        char *ptr = (char *)outputcommand.c_str();
        char *beginopt = ptr;
        while (*ptr)
        {
            if (*ptr == 0x1F)
            {
                *ptr = '\0';
//                string topush;
//                if (strncmp(text,beginopt,strlen(text))) //not begins with text
//                {
//                    topush+=text;
//                }
//                topush+=beginopt;
//                validOptions.push_back(topush);
                validOptions.push_back(beginopt);

                beginopt=ptr+1;
            }
            ptr++;
        }
        if (*beginopt)
        {
//            string topush;
//            if (strncmp(text,beginopt,strlen(text))) //not begins with text
//            {
//                topush+=text;
//            }
//            topush+=beginopt;
//            validOptions.push_back(topush);
            validOptions.push_back(beginopt);

        }
    }

//    cout << "completion values: " << oss.str() << endl; //TODO: delete


    free(saved_line);

    return generic_completion(text, state, validOptions);
}

char* local_completion(const char* text, int state)
{
    return((char*)NULL );  //matches will be NULL: readline will use local completion
}

static char** getCompletionMatches(const char * text, int start, int end)
{
    rl_filename_quoting_desired = 1;

    char **matches;

    matches = (char**)NULL;

    if (start == 0)
    {
//        matches = rl_completion_matches((char*)text, &commands_completion); //TODO: this should be queried to the server
        if (matches == NULL)
        {
            matches = rl_completion_matches((char*)text, &empty_completion);
        }
    }
    else
    {
        matches = rl_completion_matches((char*)text, remote_completion); //TODO: this should be queried to the server

    }
    return( matches );
}

//string getListOfCompletionValues(vector<string> words)
//{
//    string completionValues;
//    rl_compentry_func_t * compfunction = getCompletionFunction(words);
//    if (compfunction == local_completion)
//    {
//        return "MEGACMD_USE_LOCAL_COMPLETION";
//    }
//    int state=0;
//    if (words.size()>1)
//    while (true)
//    {
//        char *newval;
//        string &lastword = words[words.size()-1];
//        if (lastword.size()>3 && lastword[0]== '-' && lastword[1]== '-' && lastword.find('=')!=string::npos)
//        {
//            newval = compfunction(lastword.substr(lastword.find_first_of('=')+1).c_str(), state);
//        }
//        else
//        {
//            newval = compfunction(lastword.c_str(), state);
//        }

//        if (!newval) break;
//        if (completionValues.size())
//        {
//            completionValues+=" ";
//        }

//        if (strstr(newval," "))
//        {
//            completionValues+="\"";
//            completionValues+=newval;
//            completionValues+="\"";
//        }
//        else
//        {
//            completionValues+=newval;
//        }
//        free(newval);

//        state++;
//    }
//    return completionValues;
//}



void printHistory()
{
    int length = history_length;
    int offset = 1;
    int rest = length;
    while (rest >= 10)
    {
        offset++;
        rest = rest / 10;
    }

//    mutexHistory.lock(); //TODO: use mutex
    for (int i = 0; i < length; i++)
    {
        history_set_pos(i);
        OUTSTREAM << setw(offset) << i << "  " << current_history()->line << endl;
    }

//    mutexHistory.unlock();
}

//MegaApi* getFreeApiFolder()
//{
//    semaphoreapiFolders.wait();
//    mutexapiFolders.lock();
//    MegaApi* toret = apiFolders.front();
//    apiFolders.pop();
//    occupiedapiFolders.push_back(toret);
//    mutexapiFolders.unlock();
//    return toret;
//}

//void freeApiFolder(MegaApi *apiFolder)
//{
//    mutexapiFolders.lock();
//    occupiedapiFolders.erase(std::remove(occupiedapiFolders.begin(), occupiedapiFolders.end(), apiFolder), occupiedapiFolders.end());
//    apiFolders.push(apiFolder);
//    semaphoreapiFolders.release();
//    mutexapiFolders.unlock();
//}

const char * getUsageStr(const char *command)
{
    if (!strcmp(command, "login"))
    {
        return "login [email [password]] | exportedfolderurl#key | session";
    }
    if (!strcmp(command, "begin"))
    {
        return "begin [ephemeralhandle#ephemeralpw]";
    }
    if (!strcmp(command, "signup"))
    {
        return "signup email [password] [--name=\"Your Name\"]";
    }
    if (!strcmp(command, "confirm"))
    {
        return "confirm link email [password]";
    }
    if (!strcmp(command, "session"))
    {
        return "session";
    }
    if (!strcmp(command, "mount"))
    {
        return "mount";
    }
    if (!strcmp(command, "unicode"))
    {
        return "unicode";
    }
    if (!strcmp(command, "ls"))
    {
        return "ls [-lRr] [remotepath]";
    }
    if (!strcmp(command, "cd"))
    {
        return "cd [remotepath]";
    }
    if (!strcmp(command, "log"))
    {
        return "log [-sc] level";
    }
    if (!strcmp(command, "du"))
    {
        return "du [remotepath remotepath2 remotepath3 ... ]";
    }
    if (!strcmp(command, "pwd"))
    {
        return "pwd";
    }
    if (!strcmp(command, "lcd"))
    {
        return "lcd [localpath]";
    }
    if (!strcmp(command, "lpwd"))
    {
        return "lpwd";
    }
    if (!strcmp(command, "import"))
    {
        return "import exportedfilelink#key [remotepath]";
    }
    if (!strcmp(command, "put"))
    {
        return "put  [-c] [-q] [--ignore-quota-warn] localfile [localfile2 localfile3 ...] [dstremotepath]";
    }
    if (!strcmp(command, "putq"))
    {
        return "putq [cancelslot]";
    }
    if (!strcmp(command, "get"))
    {
        return "get [-m] [-q] [--ignore-quota-warn] exportedlink#key|remotepath [localpath]";
    }
    if (!strcmp(command, "getq"))
    {
        return "getq [cancelslot]";
    }
    if (!strcmp(command, "pause"))
    {
        return "pause [get|put] [hard] [status]";
    }
    if (!strcmp(command, "attr"))
    {
        return "attr remotepath [-s attribute value|-d attribute]";
    }
    if (!strcmp(command, "userattr"))
    {
        return "userattr [-s attribute value|attribute] [--user=user@email]";
    }
    if (!strcmp(command, "mkdir"))
    {
        return "mkdir [-p] remotepath";
    }
    if (!strcmp(command, "rm"))
    {
        return "rm [-r] [-f] remotepath";
    }
    if (!strcmp(command, "mv"))
    {
        return "mv srcremotepath dstremotepath";
    }
    if (!strcmp(command, "cp"))
    {
        return "cp srcremotepath dstremotepath|dstemail:";
    }
    if (!strcmp(command, "sync"))
    {
        return "sync [localpath dstremotepath| [-ds] [ID|localpath]";
    }
    if (!strcmp(command, "https"))
    {
        return "https [on|off]";
    }
    if (!strcmp(command, "export"))
    {
        return "export [-d|-a [--expire=TIMEDELAY]] [remotepath]";
    }
    if (!strcmp(command, "share"))
    {
        return "share [-p] [-d|-a --with=user@email.com [--level=LEVEL]] [remotepath]";
    }
    if (!strcmp(command, "invite"))
    {
        return "invite [-d|-r] dstemail [--message=\"MESSAGE\"]";
    }
    if (!strcmp(command, "ipc"))
    {
        return "ipc email|handle -a|-d|-i";
    }
    if (!strcmp(command, "showpcr"))
    {
        return "showpcr";
    }
    if (!strcmp(command, "users"))
    {
        return "users [-s] [-h] [-n] [-d contact@email]";
    }
    if (!strcmp(command, "getua"))
    {
        return "getua attrname [email]";
    }
    if (!strcmp(command, "putua"))
    {
        return "putua attrname [del|set string|load file]";
    }
    if (!strcmp(command, "speedlimit"))
    {
        return "speedlimit [-u|-d] [-h] [NEWLIMIT]";
    }
    if (!strcmp(command, "killsession"))
    {
        return "killsession [-a|sessionid]";
    }
    if (!strcmp(command, "whoami"))
    {
        return "whoami [-l]";
    }
    if (!strcmp(command, "passwd"))
    {
        return "passwd [oldpassword newpassword]";
    }
    if (!strcmp(command, "retry"))
    {
        return "retry";
    }
    if (!strcmp(command, "recon"))
    {
        return "recon";
    }
    if (!strcmp(command, "reload"))
    {
        return "reload";
    }
    if (!strcmp(command, "logout"))
    {
        return "logout [--keep-session]";
    }
    if (!strcmp(command, "symlink"))
    {
        return "symlink";
    }
    if (!strcmp(command, "version"))
    {
        return "version [-l][-c]";
    }
    if (!strcmp(command, "debug"))
    {
        return "debug";
    }
    if (!strcmp(command, "chatf"))
    {
        return "chatf ";
    }
    if (!strcmp(command, "chatc"))
    {
        return "chatc group [email ro|rw|full|op]*";
    }
    if (!strcmp(command, "chati"))
    {
        return "chati chatid email ro|rw|full|op";
    }
    if (!strcmp(command, "chatr"))
    {
        return "chatr chatid [email]";
    }
    if (!strcmp(command, "chatu"))
    {
        return "chatu chatid";
    }
    if (!strcmp(command, "chatga"))
    {
        return "chatga chatid nodehandle uid";
    }
    if (!strcmp(command, "chatra"))
    {
        return "chatra chatid nodehandle uid";
    }
    if (!strcmp(command, "quit"))
    {
        return "quit";
    }
    if (!strcmp(command, "history"))
    {
        return "history";
    }
    if (!strcmp(command, "thumbnail"))
    {
        return "thumbnail [-s] remotepath localpath";
    }
    if (!strcmp(command, "preview"))
    {
        return "preview [-s] remotepath localpath";
    }
    if (!strcmp(command, "find"))
    {
        return "find [remotepath] [-l] [--pattern=PATTERN]";
    }
    if (!strcmp(command, "help"))
    {
        return "help [-f]";
    }
    if (!strcmp(command, "clear"))
    {
        return "clear";
    }
    if (!strcmp(command, "transfers"))
    {
        return "transfers [-c TAG|-a] | [-r TAG|-a]  | [-p TAG|-a] [--only-downloads | --only-uploads] [SHOWOPTIONS]";
    }
    return "command not found";
}

bool validCommand(string thecommand)
{
    return stringcontained((char*)thecommand.c_str(), validCommands);
}

string getsupportedregexps()
{
#ifdef USE_PCRE
        return "Perl Compatible Regular Expressions";
#elif __cplusplus >= 201103L
        return "c++11 Regular Expressions";
#else
        return "it accepts wildcards: ? and *. e.g.: ls f*00?.txt";
#endif
}

string getHelpStr(const char *command)
{
    ostringstream os;

    os << getUsageStr(command) << endl;
    if (!strcmp(command, "login"))
    {
        os << "Logs into a mega" << endl;
        os << " You can log in either with email and password, with session ID," << endl;
        os << " or into a folder (an exported/public folder)" << endl;
        os << " If loging into a folder indicate url#key" << endl;
    }
    else if (!strcmp(command, "signup"))
    {
        os << "Register as user with a given email" << endl;
        os << endl;
        os << "Options:" << endl;
        os << " --name=\"Your Name\"" << "\t" << "Name to register. e.g. \"John Smith\"" << endl;
        os << endl;
        os << " You will receive an email to confirm your account. " << endl;
        os << " Once you have received the email, please proceed to confirm the link " << endl;
        os << " included in that email with \"confirm\"." << endl;
    }
    else if (!strcmp(command, "clear"))
    {
        os << "Clear screen" << endl;
    }
    else if (!strcmp(command, "help"))
    {
        os << "Prints list of commands" << endl;
        os << endl;
        os << "Options:" << endl;
        os << " -f" << "\t" << "Indluce a brief description of the commands" << endl;
    }
    else if (!strcmp(command, "history"))
    {
        os << "Prints history of used commands" << endl;
        os << "  Only commands used in interactive mode are registered" << endl;
    }
    else if (!strcmp(command, "confirm"))
    {
        os << "Confirm an account using the link provided after the \"singup\" process." << endl;
        os << " It requires the email and the password used to obtain the link." << endl;
        os << endl;
    }
    else if (!strcmp(command, "session"))
    {
        os << "Prints (secret) session ID" << endl;
    }
    else if (!strcmp(command, "mount"))
    {
        os << "Lists all the main nodes" << endl;
    }
    else if (!strcmp(command, "unicode"))
    {
        os << "Toogle unicode input enabled/disabled in interactive shell" << endl;
        os << endl;
        os << " Unicode mode is experimental, you might experience" << endl;
        os << " some issues interacting with the console" << endl;
        os << " (e.g. history navigation fails)" << endl;
    }
    else if (!strcmp(command, "ls"))
    {
        os << "Lists files in a remote path" << endl;
        os << " remotepath can be a pattern (" << getsupportedregexps() << ") " << endl;
        os << " Also, constructions like /PATTERN1/PATTERN2/PATTERN3 are allowed" << endl;
        os << endl;
        os << "Options:" << endl;
        os << " -R|-r" << "\t" << "list folders recursively" << endl;
        os << " -l" << "\t" << "include extra information" << endl;
    }
    else if (!strcmp(command, "cd"))
    {
        os << "Changes the current remote folder" << endl;
        os << endl;
        os << "If no folder is provided, it will be changed to the root folder" << endl;
    }
    else if (!strcmp(command, "log"))
    {
        os << "Prints/Modifies the current logs level" << endl;
        os << endl;
        os << "Options:" << endl;
        os << " -c" << "\t" << "CMD log level (higher level messages). " << endl;
        os << "   " << "\t" << " Messages captured by the command line." << endl;
        os << " -s" << "\t" << "SDK log level (lower level messages)." << endl;
        os << "   " << "\t" << " Messages captured by the engine and libs" << endl;

        os << endl;
        os << "Verbosity in non-interactive mode: Regardless of the log level of the" << endl;
        os << " interactive shell, you can increase the amount of information given" <<  endl;
        os << "   by any command by passing \"-v\" (\"-vv\", \"-vvv\", ...)" << endl;


    }
    else if (!strcmp(command, "du"))
    {
        os << "Prints size used by files/folders" << endl;
        os << " remotepath can be a pattern (" << getsupportedregexps() << ") " << endl;
        os << endl;
        os << "Options:" << endl;
        os << " -h" << "\t" << "Human readable" << endl;
        os << endl;
    }
    else if (!strcmp(command, "pwd"))
    {
        os << "Prints the current remote folder" << endl;
    }
    else if (!strcmp(command, "lcd"))
    {
        os << "Changes the current local folder for the interactive console" << endl;
        os << endl;
        os << "It will be used for uploads and downloads" << endl;
        os << endl;
        os << "If not using interactive console, the current local folder will be " << endl;
        os << " that of the shell executing mega comands" << endl;
    }
    else if (!strcmp(command, "lpwd"))
    {
        os << "Prints the current local folder for the interactive console" << endl;
        os << endl;
        os << "It will be used for uploads and downloads" << endl;
        os << endl;
        os << "If not using interactive console, the current local folder will be " << endl;
        os << " that of the shell executing mega comands" << endl;
    }
    else if (!strcmp(command, "logout"))
    {
        os << "Logs out" << endl;
        os << endl;
        os << "Options:" << endl;
        os << " --keep-session" << "\t" << "Keeps the current session." << endl;
    }
    else if (!strcmp(command, "import"))
    {
        os << "Imports the contents of a remote link into user's cloud" << endl;
        os << endl;
        os << "If no remote path is provided, the current local folder will be used" << endl;
    }
    else if (!strcmp(command, "put"))
    {
        os << "Uploads files/folders to a remote folder" << endl;
        os << endl;
        os << "Options:" << endl;
        os << " -c" << "\t" << "Creates remote folder destination in case of not existing." << endl;
        os << " -q" << "\t" << "queue upload: execute in the background. Don't wait for it to end' " << endl;
        os << " --ignore-quota-warn" << "\t" << "ignore quota surpassing warning. " << endl;
        os << "                    " << "\t" << "  The upload will be attempted anyway." << endl;
    }
    else if (!strcmp(command, "get"))
    {
        os << "Downloads a remote file/folder or a public link " << endl;
        os << endl;
        os << "In case it is a file, the file will be downloaded at the specified folder " << endl;
        os << "                             (or at the current folder if none specified)." << endl;
        os << "  If the localpath(destiny) already exists and is the same (same contents)" << endl;
        os << "  nothing will be done. If differs, it will create a new file appending \" (NUM)\" " << endl;
        os << "  if the localpath(destiny) is a folder with a file with the same name on it, " << endl;
        os << "         it will preserve the, it will create a new file appending \" (NUM)\" " << endl;
        os << endl;
        os << "For folders, the entire contents (and the root folder itself) will be" << endl;
        os << "                    by default downloaded into the destination folder" << endl;
        os << "Options:" << endl;
        os << " -q" << "\t" << "queue download: execute in the background. Don't wait for it to end' " << endl;
        os << " -m" << "\t" << "if the folder already exists, the contents will be merged with the " << endl;
        os << "                     downloaded one (preserving the existing files)" << endl;
        os << " --ignore-quota-warn" << "\t" << "ignore quota surpassing warning. " << endl;
        os << "                    " << "\t" << "  The download will be attempted anyway." << endl;
    }
    if (!strcmp(command, "attr"))
    {
        os << "Lists/updates node attributes" << endl;
        os << endl;
        os << "Options:" << endl;
        os << " -s" << "\tattribute value \t" << "sets an attribute to a value" << endl;
        os << " -d" << "\tattribute       \t" << "removes the attribute" << endl;
    }
    if (!strcmp(command, "userattr"))
    {
        os << "Lists/updates user attributes" << endl;
        os << endl;
        os << "Options:" << endl;
        os << " -s" << "\tattribute value \t" << "sets an attribute to a value" << endl;
        os << " --user=user@email" << "\t" << "select the user to query/change" << endl;
    }
    else if (!strcmp(command, "mkdir"))
    {
        os << "Creates a directory or a directories hierarchy" << endl;
        os << endl;
        os << "Options:" << endl;
        os << " -p" << "\t" << "Allow recursive" << endl;
    }
    else if (!strcmp(command, "rm"))
    {
        os << "Deletes a remote file/folder" << endl;
        os << endl;
        os << "Options:" << endl;
        os << " -r" << "\t" << "Delete recursively (for folders)" << endl;
        os << " -f" << "\t" << "Force (no asking)" << endl;
    }
    else if (!strcmp(command, "mv"))
    {
        os << "Moves a file/folder into a new location (all remotes)" << endl;
        os << endl;
        os << "If the location exists and is a folder, the source will be moved there" << endl;
        os << "If the location doesn't exits, the source will be renamed to the defined destiny" << endl;
    }
    else if (!strcmp(command, "cp"))
    {
        os << "Moves a file/folder into a new location (all remotes)" << endl;
        os << endl;
        os << "If the location exists and is a folder, the source will be copied there" << endl;
        os << "If the location doesn't exits, the file/folder will be renamed to the defined destiny" << endl;
    }
    else if (!strcmp(command, "https"))
    {
        os << "Shows if HTTPS is used for transfers. Use \"https on\" to enable it." << endl;
        os << endl;
        os << "HTTPS is not necesary since all data is stored and transfered encrypted." << endl;
        os << "Enabling it will increase CPU usage and add network overhead." << endl;
    }
    else if (!strcmp(command, "sync"))
    {
        os << "Controls synchronizations" << endl;
        os << endl;
        os << "If no argument is provided, it lists current synchronization with their IDs " << endl;
        os << "                                                             and their state" << endl;
        os << endl;
        os << "If provided local and remote paths, it will start synchronizing a local folder " << endl;
        os << "                                                           into a remote folder" << endl;
        os << endl;
        os << "If an ID is provided, it will list such synchronization with its state, " << endl;
        os << "                                          unless an option is specified:" << endl;
        os << "-d" << " " << "ID|localpath" << "\t" << "deletes a synchronization" << endl;
        os << "-s" << " " << "ID|localpath" << "\t" << "stops(pauses) or resumes a synchronization" << endl;
    }
    else if (!strcmp(command, "export"))
    {
        os << "Prints/Modifies the status of current exports" << endl;
        os << endl;
        os << "Options:" << endl;
        os << " -a" << "\t" << "Adds an export (or modifies it if existing)" << endl;
        os << " --expire=TIMEDELAY" << "\t" << "Determines the expiration time of a node." << endl;
        os << "                   " << "\t" << "   It indicates the delay in hours(h), days(d), " << endl;
        os << "                   " << "\t"  << "   minutes(M), seconds(s), months(m) or years(y)" << endl;
        os << "                   " << "\t" << "   e.g. \"1m12d3h\" stablish an expiration time 1 month, " << endl;
        os << "                   " << "\t"  << "   12 days and 3 hours after the current moment" << endl;
        os << " -d" << "\t" << "Deletes an export" << endl;
        os << endl;
        os << "If a remote path is given it'll be used to add/delete or in case of no option selected," << endl;
        os << " it will display all the exports existing in the tree of that path" << endl;
    }
    else if (!strcmp(command, "share"))
    {
        os << "Prints/Modifies the status of current shares" << endl;
        os << endl;
        os << "Options:" << endl;
        os << " -p" << "\t" << "Show pending shares" << endl;
        os << " --with=email" << "\t" << "Determines the email of the user to [no longer] share with" << endl;
        os << " -d" << "\t" << "Stop sharing with the selected user" << endl;
        os << " -a" << "\t" << "Adds a share (or modifies it if existing)" << endl;
        os << " --level=LEVEL" << "\t" << "Level of acces given to the user" << endl;
        os << "              " << "\t" << "0: " << "Read access" << endl;
        os << "              " << "\t" << "1: " << "Read and write" << endl;
        os << "              " << "\t" << "2: " << "Full access" << endl;
        os << "              " << "\t" << "3: " << "Owner access" << endl;
        os << endl;
        os << "If a remote path is given it'll be used to add/delete or in case " << endl;
        os << " of no option selected, it will display all the shares existing " << endl;
        os << " in the tree of that path" << endl;
        os << endl;
        os << "When sharing a folder with a user that is not a contact (see \"users\" help)" << endl;
        os << "  the share will be in a pending state. You can list pending shares with" << endl;
        os << " \"share -p\". He would need to accept your invitation (see \"ipc\")" << endl;
        os << endl;
        os << "If someone has shared somethin with you, it will be listed as a root folder" << endl;
        os << " Use \"mount\" to list folders shared with you" << endl;
    }
    else if (!strcmp(command, "invite"))
    {
        os << "Invites a contact / deletes an invitation" << endl;
        os << endl;
        os << "Options:" << endl;
        os << " -d" << "\t" << "Deletes invitation" << endl;
        os << " -r" << "\t" << "Sends the invitation again" << endl;
        os << " --message=\"MESSAGE\"" << "\t" << "Sends inviting message" << endl;
        os << endl;
        os << "Use \"showpcr\" to browse invitations" << endl;
        os << "Use \"ipc\" to manage invitations received" << endl;
        os << "Use \"users\" to see contacts" << endl;
    }
    if (!strcmp(command, "ipc"))
    {
        os << "Manages contact incomming invitations." << endl;
        os << endl;
        os << "Options:" << endl;
        os << " -a" << "\t" << "Accepts invitation" << endl;
        os << " -d" << "\t" << "Rejects invitation" << endl;
        os << " -i" << "\t" << "Ignores invitation [WARNING: do not use unless you know what you are doing]" << endl;
        os << endl;
        os << "Use \"invite\" to send/remove invitations to other users" << endl;
        os << "Use \"showpcr\" to browse incoming/outgoing invitations" << endl;
        os << "Use \"users\" to see contacts" << endl;
    }
    if (!strcmp(command, "showpcr"))
    {
        os << "Shows incoming and outgoing contact requests." << endl;
        os << endl;
        os << "Use \"ipc\" to manage invitations received" << endl;
        os << "Use \"users\" to see contacts" << endl;
    }
    else if (!strcmp(command, "users"))
    {
        os << "List contacts" << endl;
        os << endl;
        os << "Options:" << endl;
        os << " -s" << "\t" << "Show shared folders with listed contacts" << endl;
        os << " -h" << "\t" << "Show all contacts (hidden, blocked, ...)" << endl;
        os << " -n" << "\t" << "Show users names" << endl;
        os << " -d" << "\tcontact@email " << "Deletes the specified contact" << endl;
        os << endl;
        os << "Use \"invite\" to send/remove invitations to other users" << endl;
        os << "Use \"showpcr\" to browse incoming/outgoing invitations" << endl;
        os << "Use \"ipc\" to manage invitations received" << endl;
        os << "Use \"users\" to see contacts" << endl;
    }
    else if (!strcmp(command, "speedlimit"))
    {
        os << "Displays/modifies upload/download rate limits" << endl;
        os << " NEWLIMIT stablish the new limit in B/s (0 = no limit)" << endl;
        os << endl;
        os << "Options:" << endl;
        os << " -d" << "\t" << "Download speed limit" << endl;
        os << " -u" << "\t" << "Upload speed limit" << endl;
        os << " -h" << "\t" << "Human readable" << endl;
        os << endl;
    }
    else if (!strcmp(command, "killsession"))
    {
        os << "Kills a session of current user." << endl;
        os << endl;
        os << "Options:" << endl;
        os << " -a" << "\t" << "kills all sessions except the current one" << endl;
        os << endl;
        os << "To see all session use \"whoami -l\"" << endl;
    }
    else if (!strcmp(command, "whoami"))
    {
        os << "Print info of the user" << endl;
        os << endl;
        os << "Options:" << endl;
        os << " -l" << "\t" << "Show extended info: total storage used, storage per main folder " << endl;
        os << "   " << "\t" << "(see mount), pro level, account balance, and also the active sessions" << endl;
    }
    if (!strcmp(command, "passwd"))
    {
        os << "Modifies user password" << endl;
    }
    else if (!strcmp(command, "reload"))
    {
        os << "Forces a reload of the remote files of the user" << endl;
    }
    else if (!strcmp(command, "version"))
    {
        os << "Prints MEGA versioning info" << endl;
        os << endl;
        os << "Options:" << endl;
        os << " -c" << "\t" << "Shows changelog for the current version" << endl;
        os << " -l" << "\t" << "Show extended info: MEGA SDK version and features enabled" << endl;
    }
    else if (!strcmp(command, "thumbnail"))
    {
        os << "To download/upload the thumbnail of a file." << endl;
        os << " If no -s is inidicated, it will download the thumbnail." << endl;
        os << endl;
        os << "Options:" << endl;
        os << " -s" << "\t" << "Sets the thumbnail to the specified file" << endl;
    }
    else if (!strcmp(command, "preview"))
    {
        os << "To download/upload the preview of a file." << endl;
        os << " If no -s is inidicated, it will download the preview." << endl;
        os << endl;
        os << "Options:" << endl;
        os << " -s" << "\t" << "Sets the preview to the specified file" << endl;
    }
    else if (!strcmp(command, "find"))
    {
        os << "Find nodes matching a pattern" << endl;
        os << endl;
        os << "Options:" << endl;
        os << " --pattern=PATTERN" << "\t" << "Pattern to match";
        os << " (" << getsupportedregexps() << ") " << endl;
        os << " -l" << "\t" << "Prints file info" << endl;

    }
    if(!strcmp(command,"debug") )
    {
        os << "Enters debugging mode (HIGHLY VERBOSE)" << endl;
    }
    else if (!strcmp(command, "quit"))
    {
        os << "Quits" << endl;
        os << endl;
        os << "Notice that the session will still be active, and local caches available" << endl;
        os << "The session will be resumed when the service is restarted" << endl;
    }
    else if (!strcmp(command, "transfers"))
    {
        os << "Lists or operate with transfers" << endl;
        os << "If executed without option it will list the first 10 tranfers" << endl;
        os << "Options:" << endl;
        os << " -c (TAG|-a)" << "\t" << "Cancels transfer with TAG (or all with -a)" << endl;
        os << " -p (TAG|-a)" << "\t" << "Pause transfer with TAG (or all with -a)" << endl;
        os << " -r (TAG|-a)" << "\t" << "Resume transfer with TAG (or all with -a)" << endl;
        os << " -only-uploads" << "\t" << "Show/Operate only upload transfers" << endl;
        os << " -only-downloads" << "\t" << "Show/Operate only download transfers" << endl;
        os << endl;
        os << "Show options:" << endl;
        os << " -show-syncs" << "\t" << "Show synchronization transfers" << endl;
        os << " -show-completed" << "\t" << "Show completed transfers" << endl;
        os << " -only-completed" << "\t" << "Show only completed download" << endl;
        os << " --limit=N" << "\t" << "Show only first N transfers" << endl;
        os << " --path-display-size=N" << "\t" << "Use a fixed size of N characters for paths" << endl;
    }
    return os.str();
}

#define SSTR( x ) static_cast< std::ostringstream & >( \
        ( std::ostringstream() << std::dec << x ) ).str()
void printAvailableCommands(int extensive = 0)
{
    vector<string> validCommandsOrdered = validCommands;
    sort(validCommandsOrdered.begin(), validCommandsOrdered.end());
    if (!extensive)
    {
        size_t i = 0;
        size_t j = (validCommandsOrdered.size()/3)+((validCommandsOrdered.size()%3>0)?1:0);
        size_t k = 2*(validCommandsOrdered.size()/3)+validCommandsOrdered.size()%3;
        for (i = 0; i < validCommandsOrdered.size() && j < validCommandsOrdered.size()  && k < validCommandsOrdered.size(); i++, j++, k++)
        {
            OUTSTREAM << "      " << setw(20) << left << validCommandsOrdered.at(i) <<  setw(20) << validCommandsOrdered.at(j)  <<  "      " << validCommandsOrdered.at(k) << endl;
        }
        if (validCommandsOrdered.size()%3)
        {
            OUTSTREAM << "      " << setw(20) <<  validCommandsOrdered.at(i);
            if (validCommandsOrdered.size()%3 > 1 )
            {
                OUTSTREAM << setw(20) <<  validCommandsOrdered.at(j);
            }
            OUTSTREAM << endl;
        }
    }
    else
    {
        for (size_t i = 0; i < validCommandsOrdered.size(); i++)
        {
            if (validCommandsOrdered.at(i)!="completion")
            {
                if (extensive > 1)
                {
                    u_int width = 90;
                    int rows = 1, cols = width;

                    #if defined( RL_ISSTATE ) && defined( RL_STATE_INITIALIZED )
                        if (RL_ISSTATE(RL_STATE_INITIALIZED))
                        {
                            rl_resize_terminal();
                            rl_get_screen_size(&rows, &cols);
                        }
                    #endif

                    if (cols)
                    {
                        width = min((int)width,cols-2);
                    }

                    OUTSTREAM <<  "<" << validCommandsOrdered.at(i) << ">" << endl;
                    OUTSTREAM <<  "Usage: " << getHelpStr(validCommandsOrdered.at(i).c_str());
                    for (u_int j = 0; j< width; j++) OUTSTREAM << "-";
                    OUTSTREAM << endl;


                }
                else
                {
                    OUTSTREAM << "      " << getUsageStr(validCommandsOrdered.at(i).c_str());
                    string helpstr = getHelpStr(validCommandsOrdered.at(i).c_str());
                    helpstr=string(helpstr,helpstr.find_first_of("\n")+1);
                    OUTSTREAM << ": " << string(helpstr,0,helpstr.find_first_of("\n"));

                    OUTSTREAM << endl;
                }
            }
        }
    }
}


#ifdef _WIN32
#include <fcntl.h>
#include <io.h>

/**
 * @brief getcharacterreadlineUTF16support
 * while this works, somehow arrows and other readline stuff is disabled using this one.
 * @param stream
 * @return
 */
int getcharacterreadlineUTF16support (FILE *stream)
{
    int result;
    char b[10];
    memset(b,0,10);

    while (1)
    {
        int oldmode = _setmode(fileno(stream), _O_U16TEXT);

        result = read (fileno (stream), &b, 10);
        _setmode(fileno(stream), oldmode);

        if (result == 0)
        {
            return (EOF);
        }

        // convert the UTF16 string to widechar
        size_t wbuffer_size;
        mbstowcs_s(&wbuffer_size, NULL, 0, b, _TRUNCATE);
        wchar_t *wbuffer = new wchar_t[wbuffer_size];
        mbstowcs_s(&wbuffer_size, wbuffer, wbuffer_size, b, _TRUNCATE);

        // convert the UTF16 widechar to UTF8 string
        string receivedutf8;
        MegaApi::utf16ToUtf8(wbuffer, wbuffer_size,&receivedutf8);

        if (strlen(receivedutf8.c_str()) > 1) //multi byte utf8 sequence: place the UTF8 characters into rl buffer one by one
        {
            for (u_int i=0;i< strlen(receivedutf8.c_str());i++)
            {
                rl_line_buffer[rl_end++] = receivedutf8.c_str()[i];
                rl_point=rl_end;
            }
            rl_line_buffer[rl_end] = '\0';

            return 0;
        }

        if (result =! 0)
        {
            return (b[0]);
        }

        /* If zero characters are returned, then the file that we are
     reading from is empty!  Return EOF in that case. */
        if (result == 0)
        {
            return (EOF);
        }
    }
}
#endif

//void executecommand(char* ptr)
//{
//    vector<string> words = getlistOfWords(ptr);
//    if (!words.size())
//    {
//        return;
//    }

//    string thecommand = words[0];

//    if (( thecommand == "?" ) || ( thecommand == "h" ))
//    {
//        printAvailableCommands();
//        return;
//    }

//    if (words[0] == "completion")
//    {
//        if (words.size() < 3) words.push_back("");
//        vector<string> wordstocomplete(words.begin()+1,words.end());
//        setCurrentThreadLine(wordstocomplete);
//        OUTSTREAM << getListOfCompletionValues(wordstocomplete);
//        return;
//    }

//    map<string, string> cloptions;
//    map<string, int> clflags;

//    set<string> validParams;
//    addGlobalFlags(&validParams);

//    if (setOptionsAndFlags(&cloptions, &clflags, &words, validParams, true))
//    {
//        setCurrentOutCode(MCMD_EARGS);
//        LOG_err << "      " << getUsageStr(thecommand.c_str());
//        return;
//    }

//    insertValidParamsPerCommand(&validParams, thecommand);

//    if (!validCommand(thecommand))   //unknown command
//    {
//        setCurrentOutCode(MCMD_EARGS);
//        LOG_err << "      " << getUsageStr("unknwon");
//        return;
//    }

//    if (setOptionsAndFlags(&cloptions, &clflags, &words, validParams))
//    {
//        setCurrentOutCode(MCMD_EARGS);
//        LOG_err << "      " << getUsageStr(thecommand.c_str());
//        return;
//    }
//    setCurrentThreadLogLevel(MegaApi::LOG_LEVEL_ERROR + (getFlag(&clflags, "v")?(1+getFlag(&clflags, "v")):0));

//    if (getFlag(&clflags, "help"))
//    {
//        string h = getHelpStr(thecommand.c_str());
//        OUTSTREAM << h << endl;
//        return;
//    }


//    if ( thecommand == "help" )
//    {
//        if (getFlag(&clflags,"upgrade"))
//        {

//             const char *userAgent = api->getUserAgent();
//             char* url = new char[strlen(userAgent)+10];

//             sprintf(url, "pro/uao=%s",userAgent);

//             string theurl;

//             if (api->isLoggedIn())
//             {

//                 MegaCmdListener *megaCmdListener = new MegaCmdListener(api, NULL);
//                 api->getSessionTransferURL(url, megaCmdListener);
//                 megaCmdListener->wait();
//                 if (megaCmdListener->getError() && megaCmdListener->getError()->getErrorCode() == MegaError::API_OK)
//                 {
//                     theurl = megaCmdListener->getRequest()->getLink();
//                 }
//                 else
//                 {
//                     setCurrentOutCode(MCMD_EUNEXPECTED);
//                     LOG_warn << "Unable to get session transfer url: " << megaCmdListener->getError()->getErrorString();
//                 }
//                 delete megaCmdListener;
//             }

//             if (!theurl.size())
//             {
//                 theurl = url;
//             }

//             OUTSTREAM << "MEGA offers different PRO plans to increase your allowed transfer quota and user storage." << endl;
//             OUTSTREAM << "Open the following link in your browser to obtain a PRO account: " << endl;
//             OUTSTREAM << "  " << theurl << endl;

//             delete [] url;
//        }
//        else if (getFlag(&clflags,"non-interactive"))
//        {
//            OUTSTREAM << "MEGAcmd features two modes of interaction:" << endl;
//            OUTSTREAM << " - interactive: entering commands in this shell. Enter \"help\" to list available commands" << endl;
//            OUTSTREAM << " - non-interactive: MEGAcmd is also listening to outside petitions" << endl;
//            OUTSTREAM << "For the non-interactive mode, there are client commands you can use. " << endl;
//#ifdef _WIN32

//            OUTSTREAM << "Along with the interactive shell, there should be several mega-*.bat scripts" << endl;
//            OUTSTREAM << "installed with MEGAcmd. You can use them writting their absolute paths, " << endl;
//            OUTSTREAM << "or including their location into your environment PATH and execute simply with mega-*" << endl;
//            OUTSTREAM << "If you use PowerShell, you can add the the location of the scripts to the PATH with:" << endl;
//            OUTSTREAM << "  $env:PATH += \";$env:LOCALAPPDATA\\MEGAcmd\"" << endl;
//            OUTSTREAM << "Client commands completion requires bash, hence, it is not available for Windows. " << endl;
//            OUTSTREAM << endl;

//           OUTSTREAM << "  Security caveats:" << endl;
//           OUTSTREAM << "For the current Windows version of MEGAcmd, the server will be using network sockets " << endl;
//           OUTSTREAM << "for attending client commands. This socket is open for petitions on localhost, hence, " << endl;
//           OUTSTREAM << "you should not use it in a multiuser environment." << endl;
//#elif __MACH__
//            OUTSTREAM << "After installing the dmg, along with the interactive shell, client commands" << endl;
//            OUTSTREAM << "should be located at /Applications/MEGAcmd.app/Contents/MacOS" << endl;
//            OUTSTREAM << "If you wish to use the client commands from MacOS Terminal, open the Terminal and " << endl;
//            OUTSTREAM << "include the installation folder in the PATH. Typically:" << endl;
//            OUTSTREAM << endl;
//            OUTSTREAM << " export PATH=/Applications/MEGAcmd.app/Contents/MacOS:$PATH" << endl;
//            OUTSTREAM << endl;
//            OUTSTREAM << "And for bash completion, source megacmd_completion.sh:" << endl;
//            OUTSTREAM << " source /Applications/MEGAcmd.app/Contents/MacOS/megacmd_completion.sh" << endl;
//#else
//            OUTSTREAM << "If you have installed MEGAcmd using one of the available packages" << endl;
//            OUTSTREAM << "both the interactive shell (mega-cmd) and the different client commands (mega-*) " << endl;
//            OUTSTREAM << "will be in your PATH (you might need to open your shell again). " << endl;
//            OUTSTREAM << "If you are using bash, you should also have autocompletion for client commands working. " << endl;

//#endif
//        }
//        else
//        {
//            OUTSTREAM << "Here is the list of available commands and their usage" << endl;
//            OUTSTREAM << "Use \"help -f\" to get a brief description of the commands" << endl;
//            OUTSTREAM << "You can get further help on a specific command with \"command\" --help " << endl;
//            OUTSTREAM << "Alternatively, you can use \"help\" -ff to get a complete description of all commands" << endl;
//            OUTSTREAM << "Use \"help --non-interactive\" to learn how to use MEGAcmd with scripts" << endl;
//            OUTSTREAM << "Use \"help --upgrade\" to learn about the limitations and obtaining PRO accounts" << endl;

//            OUTSTREAM << endl << "Commands:" << endl;

//            printAvailableCommands(getFlag(&clflags,"f"));
//            OUTSTREAM << endl << "Verbosity in non-interactive mode: you can increase the amount of information given by any command by passing \"-v\" (\"-vv\", \"-vvv\", ...)" << endl;
//        }
//        return;
//    }

//    if ( thecommand == "clear" )
//    {
//#ifdef _WIN32
//        HANDLE hStdOut;
//        CONSOLE_SCREEN_BUFFER_INFO csbi;
//        DWORD count;

//        hStdOut = GetStdHandle( STD_OUTPUT_HANDLE );
//        if (hStdOut == INVALID_HANDLE_VALUE) return;

//        /* Get the number of cells in the current buffer */
//        if (!GetConsoleScreenBufferInfo( hStdOut, &csbi )) return;
//        /* Fill the entire buffer with spaces */
//        if (!FillConsoleOutputCharacter( hStdOut, (TCHAR) ' ', csbi.dwSize.X *csbi.dwSize.Y, { 0, 0 }, &count ))
//        {
//            return;
//        }
//        /* Fill the entire buffer with the current colors and attributes */
//        if (!FillConsoleOutputAttribute(hStdOut, csbi.wAttributes, csbi.dwSize.X *csbi.dwSize.Y, { 0, 0 }, &count))
//        {
//            return;
//        }
//        /* Move the cursor home */
//        SetConsoleCursorPosition( hStdOut, { 0, 0 } );
//#else
//        rl_clear_screen(0,0);
//#endif
//        return;
//    }
//#ifdef _WIN32
//    if (words[0] == "unicode")
//    {
//        rl_getc_function=(rl_getc_function==&getcharacterreadlineUTF16support)?rl_getc:&getcharacterreadlineUTF16support;
//        OUTSTREAM << "Unicode input " << ((rl_getc_function==&getcharacterreadlineUTF16support)?"ENABLED":"DISABLED") << endl;
//        return;
//    }
//#endif
////    cmdexecuter->executecommand(words, &clflags, &cloptions); //TODO: this should be asked via socket
//}


//static bool process_line(char* l)
//{
//    switch (prompt)
//    {
//        case AREYOUSURETODELETE:
//            if (!strcmp(l,"yes") || !strcmp(l,"YES") || !strcmp(l,"y") || !strcmp(l,"Y"))
//            {
//                cmdexecuter->confirmDelete();
//            }
//            else if (!strcmp(l,"no") || !strcmp(l,"NO") || !strcmp(l,"n") || !strcmp(l,"N"))
//            {
//                cmdexecuter->discardDelete();
//            }
//            else
//            {
//                //Do nth, ask again
//                OUTSTREAM << "Please enter [y]es/[n]o: " << flush;
//            }
//        break;
//        case LOGINPASSWORD:
//        {
//            if (!strlen(l))
//            {
//                break;
//            }
//            if (!cmdexecuter->confirming)
//            {
//                cmdexecuter->loginWithPassword(l);
//            }
//            else
//            {
//                cmdexecuter->confirmWithPassword(l);
//                cmdexecuter->confirming = false;
//            }
//            setprompt(COMMAND);
//            break;
//        }

//        case OLDPASSWORD:
//        {
//            if (!strlen(l))
//            {
//                break;
//            }
//            oldpasswd = l;
//            OUTSTREAM << endl;
//            setprompt(NEWPASSWORD);
//            break;
//        }

//        case NEWPASSWORD:
//        {
//            if (!strlen(l))
//            {
//                break;
//            }
//            newpasswd = l;
//            OUTSTREAM << endl;
//            setprompt(PASSWORDCONFIRM);
//        }
//        break;

//        case PASSWORDCONFIRM:
//        {
//            if (!strlen(l))
//            {
//                break;
//            }
//            if (l != newpasswd)
//            {
//                OUTSTREAM << endl << "New passwords differ, please try again" << endl;
//            }
//            else
//            {
//                OUTSTREAM << endl;
//                if (!cmdexecuter->signingup)
//                {
//                    cmdexecuter->changePassword(oldpasswd.c_str(), newpasswd.c_str());
//                }
//                else
//                {
//                    cmdexecuter->signupWithPassword(l);
//                    cmdexecuter->signingup = false;
//                }
//            }

//            setprompt(COMMAND);
//            break;
//        }

//        case COMMAND:
//        {
//            if (!l || !strcmp(l, "q") || !strcmp(l, "quit") || !strcmp(l, "exit"))
//            {
//                //                store_line(NULL);
//                return true; // exit
//            }
//            executecommand(l);
//            break;
//        }
//    }
//    return false; //Do not exit
//}

//void * doProcessLine(void *pointer)
//{
//    CmdPetition *inf = (CmdPetition*)pointer;

//    OUTSTRINGSTREAM s;
//    setCurrentThreadOutStream(&s);
//    setCurrentThreadLogLevel(MegaApi::LOG_LEVEL_ERROR);
//    setCurrentOutCode(MCMD_OK);

//    LOG_verbose << " Processing " << *inf << " in thread: " << MegaThread::currentThreadId() << " " << cm->get_petition_details(inf);

//    doExit = process_line(inf->getLine());

//    LOG_verbose << " Procesed " << *inf << " in thread: " << MegaThread::currentThreadId() << " " << cm->get_petition_details(inf);

//    MegaThread * petitionThread = inf->getPetitionThread();
//    cm->returnAndClosePetition(inf, &s, getCurrentOutCode());

//    semaphoreClients.release();
//    if (doExit)
//    {
//        exit(0);
//    }

//    mutexEndedPetitionThreads.lock();
//    endedPetitionThreads.push_back(petitionThread);
//    mutexEndedPetitionThreads.unlock();

//    return NULL;
//}


//void delete_finished_threads()
//{
//    mutexEndedPetitionThreads.lock();
//    for (std::vector<MegaThread *>::iterator it = endedPetitionThreads.begin(); it != endedPetitionThreads.end(); )
//    {
//        MegaThread *mt = (MegaThread*)*it;
//        for (std::vector<MegaThread *>::iterator it2 = petitionThreads.begin(); it2 != petitionThreads.end(); )
//        {
//            if (mt == (MegaThread*)*it2)
//            {
//                it2 = petitionThreads.erase(it2);
//            }
//            else
//            {
//                ++it2;
//            }
//        }

//        mt->join();
//        delete mt;
//        it = endedPetitionThreads.erase(it);
//    }
//    mutexEndedPetitionThreads.unlock();
//}



//void finalize()
//{
//    static bool alreadyfinalized = false;
//    if (alreadyfinalized)
//        return;
//    alreadyfinalized = true;
//    LOG_info << "closing application ...";
//    delete_finished_threads();
//    delete cm;
//    if (!consoleFailed)
//    {
//        delete console;
//    }
//    delete megaCmdMegaListener;
//    delete api;
//    while (!apiFolders.empty())
//    {
//        delete apiFolders.front();
//        apiFolders.pop();
//    }

//    for (std::vector< MegaApi * >::iterator it = occupiedapiFolders.begin(); it != occupiedapiFolders.end(); ++it)
//    {
//        delete ( *it );
//    }

//    occupiedapiFolders.clear();

//    delete megaCmdGlobalListener;
//    delete cmdexecuter;

//    LOG_debug << "resources have been cleaned ...";
//    delete loggerCMD;
//    ConfigurationManager::unloadConfiguration();

//}


void wait_for_input(int readline_fd)
{
    //TODO: test in windows
    fd_set fds;

    FD_ZERO(&fds);
    FD_SET(readline_fd, &fds);

    int rc = select(FD_SETSIZE, &fds, NULL, NULL, NULL);
    if (rc < 0)
    {
        if (errno != EINTR)  //syscall
        {
#ifdef _WIN32
            if (errno != ENOENT) // unexpectedly enters here, although works fine TODO: review this
#endif
                LOG_fatal << "Error at select: " << errno; //TODO: implement logging?
            return;
        }
    }
}

// main loop
void megacmd()
{
    char *saved_line = NULL;
    int saved_point = 0;

    rl_save_prompt();

    int readline_fd = -1;
    if (!consoleFailed)
    {
        readline_fd = fileno(rl_instream);
    }

    static bool firstloop = true;

    comms->registerForStateChanges();
    usleep(1000); //give it a while to communicate the state

    for (;; )
    {
        if (prompt == COMMAND)
        {
            if (!handlerinstalled || !firstloop)
            {
                if (!consoleFailed)
                {
                    rl_callback_handler_install(*dynamicprompt ? dynamicprompt : prompts[COMMAND], store_line);
                }
                handlerinstalled = false;

                // display prompt
                if (saved_line)
                {
                    rl_replace_line(saved_line, 0);
                    free(saved_line);
                }

                rl_point = saved_point;
                rl_redisplay();
            }
        }

        firstloop = false;


        // command editing loop - exits when a line is submitted
        for (;; )
        {

            if (prompt == COMMAND || prompt == AREYOUSURETODELETE)
            {
                wait_for_input(readline_fd);

                //api->retryPendingConnections(); //TODO: this should go to the server!

                rl_callback_read_char();
                if (doExit)
                {
                    return;
                }
            }
            else
            {
                //console->readpwchar(pw_buf, sizeof pw_buf, &pw_buf_pos, &line);//TODO: use console
            }

            if (line)
            {
                break;
            }
        }

        // save line
        saved_point = rl_point;
        saved_line = rl_copy_text(0, rl_end);

        // remove prompt
        rl_save_prompt();
        rl_replace_line("", 0);
        rl_redisplay();

        if (line)
        {
            if (strlen(line))
            {
                // execute user command
                comms->executeCommand(line);
                if (!strcmp(line,"exit") || !strcmp(line,"quit"))
                {
                    doExit = true;
                }
            }
            free(line);
            line = NULL;
        }
        if (doExit)
        {
            if (saved_line != NULL)
                free(saved_line);
            return;
        }
    }
}

class NullBuffer : public std::streambuf
{
public:
    int overflow(int c)
    {
        return c;
    }
};

void printCenteredLine(string msj, u_int width, bool encapsulated = true)
{
    if (msj.size()>width)
    {
        width = msj.size();
    }
    if (encapsulated)
        COUT << "|";
    for (u_int i = 0; i < (width-msj.size())/2; i++)
        COUT << " ";
    COUT << msj;
    for (u_int i = 0; i < (width-msj.size())/2 + (width-msj.size())%2 ; i++)
        COUT << " ";
    if (encapsulated)
        COUT << "|";
    COUT << endl;
}

void printWelcomeMsg()
{
    u_int width = 75;
    int rows = 1, cols = width;
#if defined( RL_ISSTATE ) && defined( RL_STATE_INITIALIZED )

    if (RL_ISSTATE(RL_STATE_INITIALIZED))
    {
        rl_resize_terminal();
        rl_get_screen_size(&rows, &cols);
    }
#endif

    if (cols)
    {
        width = cols-2;
#ifdef _WIN32
        width--;
#endif
    }

    COUT << endl;
    COUT << ".";
    for (u_int i = 0; i < width; i++)
        COUT << "=" ;
    COUT << ".";
    COUT << endl;
    printCenteredLine(" __  __                   ____ __  __ ____  ",width);
    printCenteredLine("|  \\/  | ___  __ _  __ _ / ___|  \\/  |  _ \\ ",width);
    printCenteredLine("| |\\/| |/ _ \\/ _` |/ _` | |   | |\\/| | | | |",width);
    printCenteredLine("| |  | |  __/ (_| | (_| | |___| |  | | |_| |",width);
    printCenteredLine("|_|  |_|\\___|\\__, |\\__,_|\\____|_|  |_|____/ ",width);
    printCenteredLine("             |___/                          ",width);
    COUT << "|";
    for (u_int i = 0; i < width; i++)
        COUT << " " ;
    COUT << "|";
    COUT << endl;
    printCenteredLine("Welcome to MegaCMD! A Command Line Interactive and Scriptable",width);
    printCenteredLine("Application to interact with your MEGA account",width);
    printCenteredLine("This is a BETA version, it might not be bug-free.",width);
    printCenteredLine("Also, the signature/output of the commands may change in a future.",width);
    printCenteredLine("Please write to support@mega.nz if you find any issue or",width);
    printCenteredLine("have any suggestion concerning its functionalities.",width);
    printCenteredLine("Enter \"help --non-interactive\" to learn how to use MEGAcmd with scripts.",width);
    printCenteredLine("Enter \"help\" for basic info and a list of available commands.",width);

    COUT << "`";
    for (u_int i = 0; i < width; i++)
        COUT << "=" ;
    COUT << "´";
    COUT << endl;

}

int quote_detector(char *line, int index)
{
    return (
        index > 0 &&
        line[index - 1] == '\\' &&
        !quote_detector(line, index - 1)
    );
}


#ifdef __MACH__


bool enableSetuidBit()
{
    char *response = runWithRootPrivileges("do shell script \"chown root /Applications/MEGAcmd.app/Contents/MacOS/MEGAcmdLoader && chmod 4755 /Applications/MEGAcmd.app/Contents/MacOS/MEGAcmdLoader && echo true\"");
    if (!response)
    {
        return NULL;
    }
    bool result = strlen(response) >= 4 && !strncmp(response, "true", 4);
    delete response;
    return result;
}


void initializeMacOSStuff(int argc, char* argv[])
{
#ifdef QT_DEBUG
        return;
#endif
asd
    int fd = -1;
    if (argc)
    {
        long int value = strtol(argv[argc-1], NULL, 10);
        if (value > 0 && value < INT_MAX)
        {
            fd = value;
        }
    }

    if (fd < 0)
    {
        if (!enableSetuidBit())
        {
            ::exit(0);
        }

        //Reboot
        if (fork() )
        {
            execv("/Applications/MEGAcmd.app/Contents/MacOS/MEGAcmdLoader",argv);
        }
        sleep(10); // TODO: remove
        ::exit(0);
    }
}

#endif

bool runningInBackground()
{
#ifndef _WIN32
    pid_t fg = tcgetpgrp(STDIN_FILENO);
    if(fg == -1) {
        // Piped:
        return false;
    }  else if (fg == getpgrp()) {
        // foreground
        return false;
    } else {
        // background
        return true;
    }
#endif
    return false;
}

#ifdef _WIN32
void mycompletefunct(char **c, int num_matches, int max_length)
{
    int rows = 1, cols = 80;

#if defined( RL_ISSTATE ) && defined( RL_STATE_INITIALIZED )

            if (RL_ISSTATE(RL_STATE_INITIALIZED))
            {
                rl_resize_terminal();
                rl_get_screen_size(&rows, &cols);
            }
#endif

    OUTSTREAM << endl;
    int nelements_per_col = (cols-1)/(max_length+1);
    for (int i=1; i < num_matches; i++)
    {
        OUTSTREAM << setw(max_length+1) << left << c[i];
        if ( (i%nelements_per_col == 0) && (i != num_matches-1))
        {
            OUTSTREAM << endl;
        }
    }
    OUTSTREAM << endl;
}
#endif

int main(int argc, char* argv[])
{
#ifdef _WIN32
    // Set Environment's default locale
    setlocale(LC_ALL, "");
    rl_completion_display_matches_hook = mycompletefunct;
#endif

#ifdef __MACH__
    initializeMacOSStuff(argc,argv);
#endif

//    NullBuffer null_buffer;
//    std::ostream null_stream(&null_buffer);
//    SimpleLogger::setAllOutputs(&null_stream);
//    SimpleLogger::setLogLevel(logMax); // do not filter anything here, log level checking is done by loggerCMD

//    loggerCMD = new MegaCMDLogger(&COUT);

//    loggerCMD->setApiLoggerLevel(MegaApi::LOG_LEVEL_ERROR);
//    loggerCMD->setCmdLoggerLevel(MegaApi::LOG_LEVEL_INFO);

//    if (( argc > 1 ) && !( strcmp(argv[1], "--debug")))
//    {
//        loggerCMD->setApiLoggerLevel(MegaApi::LOG_LEVEL_DEBUG);
//        loggerCMD->setCmdLoggerLevel(MegaApi::LOG_LEVEL_DEBUG);
//    }

//    mutexHistory.init(false);

//    mutexEndedPetitionThreads.init(false);

//    ConfigurationManager::loadConfiguration(( argc > 1 ) && !( strcmp(argv[1], "--debug")));

//    char userAgent[30];
//    sprintf(userAgent, "MEGAcmd/%d.%d.%d.0", MEGACMD_MAJOR_VERSION,MEGACMD_MINOR_VERSION,MEGACMD_MICRO_VERSION);

//#if defined(__MACH__) && defined(ENABLE_SYNC)
//    int fd = -1;
//    if (argc)
//    {
//        long int value = strtol(argv[argc-1], NULL, 10);
//        if (value > 0 && value < INT_MAX)
//        {
//            fd = value;
//        }
//    }

//    if (fd >= 0)
//    {
//        api = new MegaApi("BdARkQSQ", ConfigurationManager::getConfigFolder().c_str(), userAgent, fd);
//    }
//    else
//    {
//        api = new MegaApi("BdARkQSQ", ConfigurationManager::getConfigFolder().c_str(), userAgent);
//    }
//#else
//    api = new MegaApi("BdARkQSQ", ConfigurationManager::getConfigFolder().c_str(), userAgent);
//#endif

//    for (int i = 0; i < 5; i++)
//    {
//        MegaApi *apiFolder = new MegaApi("BdARkQSQ", (const char*)NULL, userAgent);
//        apiFolders.push(apiFolder);
//        apiFolder->setLoggerObject(loggerCMD);
//        apiFolder->setLogLevel(MegaApi::LOG_LEVEL_MAX);
//        semaphoreapiFolders.release();
//    }

//    for (int i = 0; i < 100; i++)
//    {
//        semaphoreClients.release();
//    }

//    mutexapiFolders.init(false);

//    api->setLoggerObject(loggerCMD);
//    api->setLogLevel(MegaApi::LOG_LEVEL_MAX);

//    sandboxCMD = new MegaCmdSandbox();
//    cmdexecuter = new MegaCmdExecuter(api, loggerCMD, sandboxCMD);

//    megaCmdGlobalListener = new MegaCmdGlobalListener(loggerCMD, sandboxCMD);
//    megaCmdMegaListener = new MegaCmdMegaListener(api, NULL);
//    api->addGlobalListener(megaCmdGlobalListener);
//    api->addListener(megaCmdMegaListener);

    // intialize the comms object
    comms = new MegaCmdShellCommunications();

    // set up the console
#ifdef _WIN32
    console = new CONSOLE_CLASS;
#else
    struct termios term;
    if ( ( tcgetattr(STDIN_FILENO, &term) < 0 ) || runningInBackground() ) //try console
    {
        consoleFailed = true;
//        console = NULL;//TODO: use console
    }
    else
    {
//        console = new CONSOLE_CLASS; //TODO: use console
    }
#endif
//    cm = new COMUNICATIONMANAGER();

#if _WIN32
    if( SetConsoleCtrlHandler( (PHANDLER_ROUTINE) CtrlHandler, TRUE ) )
     {
        LOG_debug << "Control handler set";
     }
     else
     {
        LOG_warn << "Control handler set";
     }
#else
    // prevent CTRL+C exit
    if (!consoleFailed){
        signal(SIGINT, sigint_handler);
    }
#endif

//    atexit(finalize);//TODO: reset?

    //TODO: local completion is failing
    rl_attempted_completion_function = getCompletionMatches;
    rl_completer_quote_characters = "\"'";
    rl_filename_quote_characters  = " ";
    rl_completer_word_break_characters = (char *)" ";

    rl_char_is_quoted_p = &quote_detector;


    if (!runningInBackground())
    {
        rl_initialize(); // initializes readline,
        // so that we can use rl_message or rl_resize_terminal safely before ever
        // prompting anything.
    }

    printWelcomeMsg();
    if (consoleFailed)
    {
        LOG_warn << "Couldn't initialize interactive CONSOLE. Running as non-interactive ONLY";
    }

//    if (!ConfigurationManager::session.empty())
//    {
//        loginInAtStartup = true;
//        stringstream logLine;
//        logLine << "login " << ConfigurationManager::session;
//        LOG_debug << "Executing ... " << logLine.str();
//        process_line((char*)logLine.str().c_str());
//        loginInAtStartup = false;
//    }

    megacmd();
//    finalize(); //TODO: reset?
}