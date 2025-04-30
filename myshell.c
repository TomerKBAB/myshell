#include <stdio.h>
#include <unistd.h>
#include <linux/limits.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/wait.h>
#include <signal.h>
#include <time.h>
#include <fcntl.h>

#include "LineParser.h"

typedef enum {
    CMD_QUIT,
    CMD_CD,
    CMD_HALT,
    CMD_WAKEUP,
    CMD_ICE,
    CMD_EXECUTE
} Command;

bool debug;


// USer Commands
void sigCommand(const char *pidStr, int sig);
void cdCommand(const char *path, char *cwd);
void execute(cmdLine *pCmdLine);

// Utilities
bool shouldDebug(int agrc, char **argv);
Command getCommand(const char *cmd);
void handleRedirect(cmdLine *pCmdLine);
void DebugMessage(char *message, bool sysError);


int main(int argc, char **argv) {
    debug = shouldDebug(argc, argv);
    char cwd[PATH_MAX];
    char input[2048];
    bool quit = false;
    getcwd(cwd, PATH_MAX);
    while (!quit) {
        printf("%s: ", cwd);
         if(fgets(input, 2048, stdin) == NULL) {
            break;  //EOF signal
        }
        cmdLine *pCmdLine = parseCmdLines(input);
        if (pCmdLine == NULL) {
            continue;  // Skip to next iteration if parsing failed
        }
        Command cmd = getCommand(pCmdLine->arguments[0]);
        switch (cmd) {
            case CMD_QUIT:
                quit = true;
                break;
            case CMD_CD:
                cdCommand((pCmdLine->argCount > 1 ? pCmdLine->arguments[1] : NULL), cwd);
                break;
            case CMD_HALT:
                sigCommand((pCmdLine->argCount > 1 ? pCmdLine->arguments[1] : NULL), SIGSTOP);
                break;
            case CMD_ICE: 
                sigCommand((pCmdLine->argCount > 1 ? pCmdLine->arguments[1] : NULL), SIGINT);
                break;
            case CMD_WAKEUP: 
                sigCommand((pCmdLine->argCount > 1 ? pCmdLine->arguments[1] : NULL), SIGCONT);
                break;
            case CMD_EXECUTE:
                execute(pCmdLine);
                break;
        } 
        nanosleep(&(struct timespec){0, 500000000}, NULL);
        freeCmdLines(pCmdLine);
    }
}

// Iterates through user arguments, returns true if the prgoram should
// Print debugging information.
bool shouldDebug(int argc, char **argv) {
    for (int i = 0; i < argc; i++) {
        if(strcmp(argv[i], "-d") == 0)
            return true;
    }
    return false;
}

// executes using the path variables the command with arguemnts given.
void execute(cmdLine *pCmdLine) {
    int pid = fork();
    //Error in fork
    if (pid < 0) {
        DebugMessage("fork failed", true);
        // TODO: is it needed to free here?
        freeCmdLines(pCmdLine);
        return;
    }
    // Child process execute
    if (pid == 0) {
        handleRedirect(pCmdLine);
        if(execvp(pCmdLine->arguments[0], pCmdLine->arguments) == -1) {
            DebugMessage("execvp failed", true);
            exit(EXIT_FAILURE);
        }
    }
    // In parent, debug
    if (debug) {
        char msg[100];
        snprintf(msg, sizeof(msg), "%s%d", "PID: ", pid);
        DebugMessage(msg, false);
        snprintf(msg, sizeof(msg), "%s%s", "Command: ", pCmdLine->arguments[0]);
        DebugMessage(msg, false);
    }

    // Set blocking
    if(pCmdLine->blocking) {
        waitpid(pid, NULL, 0);
    }
}

// gets a cmd command, and returns the corresponsing enum value
Command getCommand(const char *cmd) {
    if (strcmp(cmd, "quit") == 0)
        return CMD_QUIT;
    else if (strcmp(cmd, "cd") == 0)
        return CMD_CD;
    else if (strcmp(cmd, "halt") == 0)
        return CMD_HALT;
    else if (strcmp(cmd, "wakeup") == 0)
        return CMD_WAKEUP;
    else if (strcmp(cmd, "ice") == 0)
        return CMD_ICE;
    else
        return CMD_EXECUTE;
}
// Changes directory to given path, updates cwd variable.
void cdCommand(const char* path, char *cwd) {
    if (path == NULL) {
        if(debug) {
            DebugMessage(stderr, "cd: missing operand\n");
        }
        return;
    }
    if (chdir(path) != 0) {
        if (debug) {
            perror("chdir failed");
        }
    }
    else {
        getcwd(cwd, PATH_MAX);
    }
}

//sigCommand - Sends the specified signal to the process whose PID is provided by pidStr.
void sigCommand(const char *pidStr, int sig) {
    if(pidStr == NULL) {
        if(debug) {
            DebugMessage(stderr, "PID not provided\n");
        }
        return;
    }
    int pid = atoi(pidStr);
    if (kill(pid, sig) == -1) {
        if (debug) {
            perror("Halt signal failed");
        }
    }
}

void handleRedirect(cmdLine *pCmdLine) {
    if (pCmdLine->inputRedirect != NULL) {
        close(0);
        if (open(pCmdLine->inputRedirect, O_RDONLY) == -1) {
            if (debug) {
                perror("Input redirection open failed");
            }
        }
    }
    if (pCmdLine->outputRedirect!= NULL) {
        close(1);
        if (open(pCmdLine->outputRedirect, O_WRONLY | O_CREAT | O_TRUNC, 0644)  == -1) {
            if (debug) {
                perror("output redirection open failed");
            }
        }
    }
}

void DebugMessage(char *message, bool sysError){
    if(debug){
        if (sysError)
            perror(message);
        else
            fprintf(stderr, "%s\n", message);
    }
}
