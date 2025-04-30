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

// Executers
void execute(cmdLine *pCmdLine);
int forkAndExec(char *path, char *const argv[], int in_fd, int out_fd);

// Helpers 
void runPipeline(cmdLine *left);
bool shouldDebug(int agrc, char **argv);
Command getCommand(const char *cmd);
void handleRedirect(cmdLine *pCmdLine);
void DebugMessage(char *message, bool sysError);
bool validateNoRedirectConflict(cmdLine *left, cmdLine *right);
void DebugChild(int pid, char *cmd);


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

        if (pCmdLine->next) {
            runPipeline(pCmdLine);
            freeCmdLines(pCmdLine->next);
        }
        else {
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
            // Wait for child a bit
            nanosleep(&(struct timespec){0, 500000000}, NULL);
        }
        freeCmdLines(pCmdLine);
    }
}

// Handle exactly two commands joined by a pipe
void runPipeline(cmdLine *left) {
    cmdLine *right = left->next;

    if (!validateNoRedirectConflict(left, right))
        return;

    int pipefd[2];
    if (pipe(pipefd) == -1) {
        DebugMessage("pipe", true);
        return;
    }

    // left: stdout -> pipefd[1]
    int p1 = forkAndExec(
        left->arguments[0],
        left->arguments,
        left->inputRedirect  ? open(left->inputRedirect,  O_RDONLY) : -1,
        pipefd[1]
    );
    DebugChild(p1, left->arguments[0]);

    close(pipefd[1]);
    // right: stdin <- pipefd[0]
    int p2 = forkAndExec(
        right->arguments[0],
        right->arguments,
        pipefd[0],
        right->outputRedirect ? open(right->outputRedirect, O_CREAT|O_WRONLY | O_TRUNC, 0666) : -1
    );
    DebugChild(p2, right->arguments[0]);

    // parent closes both ends
    close(pipefd[0]);

    // wait for both
    // TODO: do i need to check blocking here (&)?
    waitpid(p1, NULL, 0);
    waitpid(p2, NULL, 0);
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
    DebugChild(pid, pCmdLine->arguments[0]);

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
            DebugMessage("cd: missing operand", false);
        }
        return;
    }
    if (chdir(path) != 0) {
        if (debug) {
            DebugMessage("chdir failed", true);
        }
    }
    else {
        getcwd(cwd, PATH_MAX);
    }
}

//sigCommand - Sends the specified signal to the process whose PID is provided by pidStr.
void sigCommand(const char *pidStr, int sig) {
    if(pidStr == NULL) {
        DebugMessage("PID not provided", false);
        return;
    }
    int pid = atoi(pidStr);
    if (kill(pid, sig) == -1) {
        DebugMessage("Halt signal failed", true);
    }
}


// ——— Helpers —————————————————————————————————————————————

void handleRedirect(cmdLine *pCmdLine) {
    if (pCmdLine->inputRedirect != NULL) {
        close(0);
        if (open(pCmdLine->inputRedirect, O_RDONLY) == -1) {
            if (debug) {
                DebugMessage("Input redirection open failed", true);
            }
        }
    }
    if (pCmdLine->outputRedirect!= NULL) {
        close(1);
        if (open(pCmdLine->outputRedirect, O_WRONLY | O_CREAT | O_TRUNC, 0644)  == -1) {
            if (debug) {
                DebugMessage("output redirection open failed", true);
            }
        }
    }
}

// Print an error and return false
bool validateNoRedirectConflict(cmdLine *left, cmdLine *right) {
    if (left->outputRedirect || right->inputRedirect) {
        DebugMessage("can't mix pipe and I/O redirect", false);
        return false;
    }
    return true;
}

// Fork a child that redirects its stdin/out, then execs.
// If in_fd or out_fd is -1, that side isn’t redirected.
int forkAndExec(char *path, char *const argv[],
                         int in_fd, int out_fd)
{
    int pid = fork();
    if (pid == 0) {
        // child
        if (in_fd  != -1) { close(STDIN_FILENO);  dup(in_fd);  }
        if (out_fd != -1) { close(STDOUT_FILENO); dup(out_fd); }
        // close any pipe FDs inherited
        // (we assume parent will close its copies)
        execvp(path, argv);
        DebugMessage("exec failed", true);
        exit(1);
    }
    return pid;
}

void DebugMessage(char *message, bool sysError){
    if(debug){
        if (sysError)
            perror(message);
        else
            fprintf(stderr, "%s\n", message);
    }
}

void DebugChild(int pid, char *cmd) {
    char msg[100];
    snprintf(msg, sizeof(msg), "%s%d", "PID: ", pid);
    DebugMessage(msg, false);
    snprintf(msg, sizeof(msg), "%s%s", "Command: ", cmd);
    DebugMessage(msg, false);
}