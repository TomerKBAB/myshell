#define _XOPEN_SOURCE 600 // used to include WCONTINUED used for waitpid
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
#include <sys/types.h>
#include <errno.h>
#include <ctype.h>

#include "LineParser.h"

#define TERMINATED  -1
#define RUNNING 1
#define SUSPENDED 0
#define HISTLEN 20

typedef enum {
    CMD_QUIT,
    CMD_CD,
    CMD_HALT,
    CMD_WAKEUP,
    CMD_ICE,
    CMD_PROCS,
    CMD_EXECUTE
} Command;

typedef struct process{
        cmdLine* cmd;
        pid_t pid;
        int status; 
        struct process *next;
} process;

typedef struct history_entry {
    char *cmd;
    struct history_entry *next;
} history_entry;

typedef struct {
    history_entry *head;
    history_entry *tail;
    int count;
} history_list;

bool debug;
process *process_list = NULL;

// USer Commands
void sigCommand(const char *pidStr, int sig);
void cdCommand(const char *path, char *cwd);

// Executers
void dispatchCommand(cmdLine *pCmdLine, char cwd[]);
void execute(cmdLine *pCmdLine);
int forkAndExec(char *path, char *const argv[], int in_fd, int out_fd);

// Process
void addProcess(process** process_list, cmdLine* cmd, pid_t pid);
void printProcessList(process** process_list);
void freeProcessList(process **plist);
void updateProcessList(process **plist);
void updateProcessStatus(process *process_list, int pid, int status);
void removeTerminatedProcesses(process **plist);

// History
void initHistory(history_list *h);
void freeHistory(history_list *h);
void addHistory(history_list *h, const char *cmd);
void printHistory(const history_list *h);
const char *getHistory(const history_list *h, int n);
const char *getLastHistory(const history_list *h);
bool tryHistoryCommands(history_list *history, char *input);

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
    history_list history;
    initHistory(&history);
    while (!quit) {

        printf("%s: ", cwd);
         if(fgets(input, sizeof(input), stdin) == NULL) {
            break;  //EOF signal
        }

        // Strip new line
        input[strcspn(input, "\n")] = '\0';
        if (input[0] == '\0')
            continue;

         // history handling 
        bool skipExecution = tryHistoryCommands(&history, input);
        if (skipExecution)
            continue;     // either "hist" or an error in !! / !n

        // record in history
        addHistory(&history, input);

        cmdLine *pCmdLine = parseCmdLines(input);
        if (!pCmdLine) {
            continue;  // Skip to next iteration if parsing failed or empty
        }

        // handle quit 
        if (getCommand(pCmdLine->arguments[0]) == CMD_QUIT) {
            freeCmdLines(pCmdLine);
            quit = true;
            break;
        }

        dispatchCommand(pCmdLine, cwd);
    }

    // Cleanup
    freeProcessList(&process_list);
    freeHistory(&history);
    return 0;
}

// Handle exactly two commands joined by a pipe
void runPipeline(cmdLine *pCmdLine) {
    cmdLine *left  = pCmdLine;
    cmdLine *right = pCmdLine->next;

    // Detach into two one‐node lists
    left->next  = NULL;
    right->next = NULL;

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
    addProcess(&process_list, left, p1);
    DebugChild(p1, left->arguments[0]);

    close(pipefd[1]);
    // right: stdin <- pipefd[0]
    int p2 = forkAndExec(
        right->arguments[0],
        right->arguments,
        pipefd[0],
        right->outputRedirect ? open(right->outputRedirect, O_CREAT|O_WRONLY | O_TRUNC, 0666) : -1
    );
    addProcess(&process_list, right, p2);
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

/// Dispatch a parsed command line 
void dispatchCommand(cmdLine *pCmdLine, char cwd[]) {
    bool shouldFree = true;

    if (pCmdLine->next) {
        runPipeline(pCmdLine);
        shouldFree = false;
    }
    else {
        Command cmd = getCommand(pCmdLine->arguments[0]);
        switch (cmd) {
            case CMD_QUIT: //we'll quit in main
                break;
            case CMD_CD:
                cdCommand(
                    pCmdLine->argCount>1 ? pCmdLine->arguments[1] : NULL,
                    cwd
                );
                break;
            case CMD_HALT:
                sigCommand(
                    pCmdLine->argCount>1 ? pCmdLine->arguments[1] : NULL,
                    SIGSTOP
                );
                break;
            case CMD_ICE:
                sigCommand(
                    pCmdLine->argCount>1 ? pCmdLine->arguments[1] : NULL,
                    SIGINT
                );
                break;
            case CMD_WAKEUP:
                sigCommand(
                    pCmdLine->argCount>1 ? pCmdLine->arguments[1] : NULL,
                    SIGCONT
                );
                break;
            case CMD_PROCS:
                printProcessList(&process_list);
                break;
            case CMD_EXECUTE:
                execute(pCmdLine);
                shouldFree = false;
                break;
            default:
                break;
        }
    }

    nanosleep(&(struct timespec){0, 500000000}, NULL);
    if (shouldFree)
        freeCmdLines(pCmdLine);
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
    // In parent
    else {
        addProcess(&process_list, pCmdLine, pid);
        DebugChild(pid, pCmdLine->arguments[0]);
        if(pCmdLine->blocking) {
            waitpid(pid, NULL, 0);
        }
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
    else if (strcmp(cmd, "procs") == 0)
        return CMD_PROCS;
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
        DebugMessage("signal failed", true);
    }
    else {
        if (sig == SIGSTOP) {
            updateProcessStatus(process_list, pid, SUSPENDED);
            DebugMessage("signaled STOP", false);
        }
        else if (sig == SIGCONT) {
            updateProcessStatus(process_list, pid, RUNNING);
            DebugMessage("signaled SIGCONT", false);
        }
        else if (sig == SIGINT) {
            updateProcessStatus(process_list, pid, TERMINATED);
            DebugMessage("signaled SIGINT", false);
        }
    }
}


// ——— Process —————————————————————————————————————————————
void addProcess(process** plist, cmdLine* cmd, pid_t pid) {
    process *p = malloc(sizeof(process));
    if (!p) { perror("malloc"); return; }
    p->cmd = cmd;
    p->pid = pid;
    p->status = RUNNING;
    p->next = *plist;
    *plist = p;
}

// Update statuses by non-blocking waitpid() 
void updateProcessList(process **plist) {
    for (process *p = *plist; p; p = p->next) {
        int st;
        // Add WCONTINUED so SIGCONT will show up as a state change
        pid_t r = waitpid(p->pid, &st, WNOHANG | WUNTRACED | WCONTINUED);
        if (r > 0) {
            if (WIFEXITED(st) || WIFSIGNALED(st))
                p->status = TERMINATED;
            else if (WIFSTOPPED(st))
                p->status = SUSPENDED;
            else
                p->status = RUNNING;
        }
        else if (r == -1 && errno == ECHILD) {
            p->status = TERMINATED;
        }
    }
}

/**
 * Find the process with the given pid in process_list
 * and set its status field to the new value.
 */
void updateProcessStatus(process *process_list, int pid, int status) {
    for (process *p = process_list; p; p = p->next) {
        if (p->pid == pid) {
            p->status = status;
            return;
        }
    }
}

// Prints process List PID Command STATUS
void printProcessList(process** plist) {
    updateProcessList(plist);

    // Header 
    printf("PID          Command      STATUS\n");
    // Entries
    for (process *p = *plist; p; p = p->next) {
        const char *statusStr = 
            (p->status == RUNNING)   ? "Running"    :
            (p->status == SUSPENDED) ? "Suspended"  :
                                       "Terminated";

        // Print PID left-aligned in width 12, command in width 12, then status
        printf("%-12d%-12s%s\n",
               p->pid,
               p->cmd->arguments[0],
               statusStr);
    }

    // Clean up the terminated processes
    removeTerminatedProcesses(plist);
}

void removeTerminatedProcesses(process **plist) {
    process *prev = NULL;
    process *cur  = *plist;
    while (cur) {
        if (cur->status == TERMINATED) {
            process *toDel = cur;
            // unlink it
            if (prev)
                prev->next = cur->next;
            else
                *plist = cur->next;
            cur = cur->next;           // advance before freeing
            freeCmdLines(toDel->cmd);
            free(toDel);
        }
        else {
            prev = cur;
            cur  = cur->next;
        }
    }
}

// Free the list on shell exit
void freeProcessList(process **plist) {
    process *p = *plist;
    while (p) {
        process *next = p->next;
        freeCmdLines(p->cmd);   // if you own that memory
        free(p);
        p = next;
    }
    *plist = NULL;
}

// ——— History —————————————————————————————————————————————

// initialize to empty
void initHistory(history_list *h) {
    h->head = NULL;
    h->tail = NULL;
    h->count = 0;
}

// free all nodes & strings
void freeHistory(history_list *h) {
    history_entry *curr = h->head;
    while (curr) {
        history_entry *next = curr->next;
        free(curr->cmd);
        free(curr);
        curr = next;
    }
    initHistory(h);
}

// add a new command, removing the oldest if full
void addHistory(history_list *h, const char *cmd) {
    char *copy = strdup(cmd);
    history_entry *entry = malloc(sizeof(history_entry));
    entry->cmd  = copy;
    entry->next = NULL;

    if (h->count == 0) {
        h->head = entry;
        h->tail = entry;
        h->count = 1;
    }
    else {
        h->tail->next = entry;
        h->tail = entry;
        if (h->count < HISTLEN) {
            h->count++;
        } else {
            // remove oldest
            history_entry *old = h->head;
            h->head = old->next;
            free(old->cmd);
            free(old);
        }
    }
}

// print numbered history
void printHistory(const history_list *h) {
    history_entry *e = h->head;
    // Stops at null entry
    for (int i = 1; e; e = e->next, i++) {
        printf("%2d  %s\n", i, e->cmd);
    }
}

// get the nth (1-based) entry, or NULL
const char *getHistory(const history_list *h, int n) {
    if (n < 1 || n > h->count)
        return NULL;
    history_entry *e = h->head;
    for (int i = 1; i < n; i++)
        e = e->next;
    return e ? e->cmd : NULL;
}

// get the last (most recent) entry, or NULL
const char *getLastHistory(const history_list *h) {
    return h->tail ? h->tail->cmd : NULL;
}

/// Handle "hist", "!!", and "!n".  If we did handle one, returns true
/// and leaves `input` unchanged (for "hist") or overwritten with
/// the expanded command (for !! or  !n).
bool tryHistoryCommands(history_list *history, char *input) {
    if (strcmp(input, "hist") == 0) {
        printHistory(history);
        return true;
    }
    if (strcmp(input, "!!") == 0) {
        const char *last = getLastHistory(history);
        if (!last) { fprintf(stderr, "No commands in history\n"); return true; }
        char *suffix = input + 2;
        snprintf(input, 2048, "%s%s", last, suffix);
        printf("%s\n", input);
        return false;  // we will execute the expanded line
    }
    if (input[0]=='!' && isdigit((unsigned char)input[1])) {
        char *p = input + 1;
        while (isdigit((unsigned char)*p)) p++;
        int idx = atoi(input+1);
        const char *cmd = getHistory(history, idx);
        if (!cmd) { fprintf(stderr, "No such command: %d\n", idx); return true; }
        snprintf(input, 2048, "%s%s", cmd, p);
        printf("%s\n", input);
        return false;
    }
    return false;
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