#include <iostream>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <memory>
#include <cstring>
#include <vector>
#include <string>
#include <time.h>
#include <fcntl.h>

#include "Tokenizer.h"

// all the basic colours for a shell prompt
#define RED     "\033[1;31m"
#define GREEN	"\033[1;32m"
#define YELLOW  "\033[1;33m"
#define BLUE	"\033[1;34m"
#define WHITE	"\033[1;37m"
#define NC      "\033[0m"

using namespace std;

int main () {
    
    // grab original parent file descriptors 
    int old_stdin = dup(0);
    int old_stdout = dup(1); 

    // create a vector of pids? for zombie process prevention?
    vector<pid_t> backgrounded;

    // grab cwd and first prev dir before executing shell
    char buf[1024]; // chose 1024 arbitrarily, or 1KB
    string currDir = getcwd(buf, sizeof(buf));
    string prevDir = getcwd(buf, sizeof(buf));
    
    for (;;) {

        // check if any of the backgrounded pids are done (if so, pop)
        int stat = 0;
        int b = 0;
        for (pid_t backed : backgrounded) {
            int w = waitpid(backed, &stat, WNOHANG);
            // check status to see if backgrounded proc has returned
            // remove it from the vector if it has
            // stat > 1 means backgrounded process is done
            // NVM, don't check stat
            if (w > 0) {
                backgrounded.erase(backgrounded.begin() + b); // at position b
                // cout << "Deleted Backgrounded Process. Pid: " << backed << endl; // maybe include [1], referring to what No. the background process is

                // format, with 20 spaces (or 5 tabs of 4 whitespaces each?) between "Done" and "ls"
                // [1]+  Done                    ls --color=auto -la
                // only once a button is pressed after it finishes... hm
            }

            b++;
        }

        // need date/time, username, and absolute path to current dir
        // ex: Oct 16 1:24:56 /home/logan_talton/CSCE313/PAs/PA2/pa2-aggie-shell-313-logtempacct$
        time_t currTime = time(NULL);
        char* currTimeCh = ctime(&currTime); // now we need to remove the first 4 and last 4 indices...
        string currTimeStr(currTimeCh);
        currTimeStr.erase(0, 4); 
        currTimeStr.erase(currTimeStr.size()-5, 5);
        char* usr = getenv("USER");
        cout << currTimeStr << GREEN << usr << NC << ":" << BLUE << currDir << YELLOW << "$" << NC << " ";

        // get user inputted command
        string input;
        getline(cin, input);
        // ERROR: if "exit " is entered, glitches out
        if (input=="Exit" || input=="exit" || cin.eof()) {  // print exit message and break out of infinite loop
            cout << RED << "Now exiting shell..." << endl << "Goodbye" << NC << endl;
            break;
        }
        if (input == "") { // somehow implement feature to once "" is entered, it displays done backgrounded processes
            continue;
        }

        // get tokenized commands from user input
        Tokenizer tknr(input);
        if (tknr.hasError()) {  // continue to next prompt if input had an error
            continue;
        }

        // Optional Debugging Section
        // // print out every command token-by-token on individual lines
        // // prints to cerr to avoid influencing autograder
        // for (auto cmd : tknr.commands) {
        //     for (auto str : cmd->args) {
        //         cerr << "|" << str << "| ";
        //     }
        //     if (cmd->hasInput()) {
        //         cerr << "in< " << cmd->in_file << " ";
        //     }
        //     if (cmd->hasOutput()) {
        //         cerr << "out> " << cmd->out_file << " ";
        //     }
        //     cerr << endl;
        // }

        // pasting from LE2
        int oldfds[2];
        int newfds[2];
        for (long unsigned int i = 0; i < tknr.commands.size(); i++) {

            auto fullCommand = tknr.commands[i]; // single command of potentially several args: arg1 | arg2 | ...

            // check for cd
            // ASSUMPTION: we are not testing for if other stuff is included with cd that shouldn't OR that cd is used in a piped fashion 
            if (fullCommand->args[0] == "cd") {

                if (fullCommand->args[1] == "-") {
                    // prev dir
                    chdir(prevDir.c_str());

                    string tempDir = prevDir; // flip dirs for -
                    prevDir = currDir;
                    currDir = tempDir;

                } else {
                    // this line below will detect .. and all other valid cds
                    if (chdir(fullCommand->args[1].c_str()) == -1) {
                        perror("invalid argument passed for cd. aborting.");
                    }

                    prevDir = currDir; // iterate dirs
                    currDir = fullCommand->args[1];
                    currDir = getcwd(buf, sizeof(buf));
                }

                continue;
            }

            if (i != tknr.commands.size()-1) {
                if (pipe(newfds) == -1) {
                    perror("new pipe creation failed. aborting.");
                }
            }

            pid_t pid = fork();
            if (pid == -1) {
                perror("fork failed. aborting.");
            }

            if (pid == 0) {
                // child

                ////////////////////////////////
                // TEMP SPINLOCK: for debugging child
                // int j = 0;
                // while(true) {
                //     j++;
                //     if (j == 1) {
                //         j--;
                //     }
                //     if (j == 2) {
                //         break;
                //     }
                // } 
                ////////////////////////////////

                // new, grabbing straight from object vectors
                vector<char*> argsList;
                for (auto a : fullCommand->args) {
                    argsList.push_back(strdup(a.c_str()));
                }
                argsList.push_back(nullptr);
                char** args = argsList.data(); // data automatically allocates to **

                // making a full string from command (optional)
                // string fullUsrInput = tknr.input; ????
                string fullCommStr = "";
                for (auto ch : argsList) {
                    if (ch == nullptr) {
                        free(ch); // just in case
                        break;
                    }
                    string temp(ch);
                    fullCommStr += temp;
                    fullCommStr += " ";
                }
                fullCommStr.erase(fullCommStr.size()-1, 1); // remove last whitespace



                // IO Redirection > or <
                // Ex1: echo "hi there" > file1.txt (running cat file1.txt will output "hi there") (output redirection)
                // filet1.txt is the output to echo
                // Ex2: cat < file1.txt (this will display file1.txt, kinda same as just doing cat file1.txt, so kinda bad ex) (input redirection)
                // file1.txt is the input to cat
                if (fullCommand->hasOutput()) {
                    // if ">" exists

                    // cout << "creating output file..." << endl;
                    string outputFile = fullCommand->out_file;
                    int fl = creat(outputFile.c_str(), O_CREAT|O_TRUNC|S_IRWXU); // flags ok?
                    if (fl == -1) {
                        perror("output file redirection failed. aborting.");
                    }
                    dup2(fl, 1); // should this work with my architecture?
                    close(fl);
                }
                
                if (fullCommand->hasInput()) {
                    // if "<" exists

                    string inputFile = fullCommand->in_file;
                    int fl = open(inputFile.c_str(), O_RDONLY, S_IRWXU); // flags ok?
                    if (fl == -1) {
                        perror("input file redirection failed. aborting.");
                    }
                    dup2(fl, 0); // should this work with my architecture?
                    close(fl);
                }


                
                // if a previous command exists
                if (i != 0) {
                    dup2(oldfds[0], 0);
                    close(oldfds[0]);
                    close(oldfds[1]);
                }
                
                // if a next command exists
                if (i != tknr.commands.size()-1) {
                    // middle commands
                    close(newfds[0]);
                    dup2(newfds[1], 1);
                    close(newfds[1]); 
                }

                // in child, execute command, command&args
                if (execvp(args[0], args) == -1) {
                    // on unknown program, still prints extra ": No such file or directory", I think it's ok
                    string fullErrMessage = fullCommStr + ": command not found";
                    perror(fullErrMessage.c_str());

                    // deallocate argsList and exit(0) seems to have incorrect commands work 
                    for (long unsigned int l = 0; l < argsList.size(); l++) {
                        free(argsList.at(l));
                    }

                    exit(0);
                }
                    
                
            } else {
                // parent

                // if a previous command exists
                if (i != 0) {
                    close(oldfds[0]); // close previous pipe entirely
                    close(oldfds[1]);
                }

                // if a next command exists
                if (i != tknr.commands.size()-1) {
                    oldfds[0] = newfds[0]; // iterate file descriptors
                    oldfds[1] = newfds[1];
                }

                // int stat = 0; // we init stat at beginning of program
                if ((!fullCommand->isBackground()) && (i == tknr.commands.size()-1)) {
                    // wait for command
                    waitpid(pid, &stat, 0); 
                } else if ((fullCommand->isBackground()) && (i == tknr.commands.size()-1)) {
                    // backgrounded command, push to background vector
                    // TA: we are a backgrounded process, wait without hanging later
                    // waitpid(pid, &stat, WNOHANG); //...?
                    backgrounded.push_back(pid);
                    cout << "[" << backgrounded.size() << "] " << pid << endl; // [backgrounded.size()], where itll be inserted / no. of backgrounded proc
                }

                //         if (!tknr.commands.at(0)->isBackground()) { // you can check if it is backgrounded by
                //             waitpid(pid, &status, 0); // only run if its not a backgrounded process (what does that mean?) 
                //             // backgrounded process definition: ...? we can't do waitpid on a backgrounded process, hence keeping a vector for memory
                //         } else {
                //             // we are a backgrounded process, wait without hanging later
                //             // waitpid(pid, &status, WNOHANG);
                //             backgrounded.push_back(pid);
                //         } 

                if (1 < stat) {
                    exit(stat); 
                }
            }

        }

        // replacing parent file descriptors if it was changed
        if (tknr.commands.size() > 1) {
            // only need to replace originals if more than one command was given
            dup2(old_stdin, 0);
            dup2(old_stdout, 1);
        }








        // LE2 part, given by TA for PA2
        // if cmd is 'cd', we don't want to create a fork()
        // we want to execute this in parent, not child, could create some sort of fork bomb
        // we need to change directory without forking, then looping back up
        // chdir();
        // continue, return to prompt...


        // // fork to create child
        // pid_t pid = fork();
        // if (pid < 0) {  // error check
        //     perror("fork");
        //     exit(2);
        // }

        // if (pid == 0) {  // if child, exec to run command
        //     // run single commands with no arguments
        //     char* args[] = {(char*) tknr.commands.at(0)->args.at(0).c_str(), nullptr};

        //     // pipe redirection, | cmd1 | cmd2

        //     // check to see if we have an input or output file
        //     if (tknr.commands.at(0)->hasInput()) {
        //         // open input file
        //         // redirect stdin to the opened file
        //     } else if (/* same goes for output fule */) {
        //         // same for hasOutput
        //     }


        //     if (execvp(args[0], args) < 0) {  // error check
        //         perror("execvp");
        //         exit(2);
        //     }

        // } else {  // if parent, wait for child to finish
        //     int status = 0;

        //     if (/* last command */) {

        //         if (!tknr.commands.at(0)->isBackground()) { // you can check if it is backgrounded by
        //             waitpid(pid, &status, 0); // only run if its not a backgrounded process (what does that mean?) 
        //             // backgrounded process definition: ...? we can't do waitpid on a backgrounded process, hence keeping a vector for memory
        //         } else {
        //             // we are a backgrounded process, wait without hanging later
        //             // waitpid(pid, &status, WNOHANG);
        //             backgrounded.push_back(pid);
        //         } 

        //     }

        //     if (status > 1) {  // exit if child didn't exec properly
        //         exit(status);
        //     }
        // }

    }

    // outside for, now replace parent file descriptors is
    

}
