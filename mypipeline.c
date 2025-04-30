#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/wait.h>

int main(int argc, char **argv) {
    //Create Pipe
    int fd[2];
    if (pipe(fd) == -1) {
        perror("pipe failed");
        exit(1);
    }

    // Create Child
    fprintf(stderr, "(parent_process>forking…)\n");
    int pid = fork();
    fprintf(stderr, "(parent_process>created process with id: %d)\n", pid);
    // In first child
    if (pid == 0) {
        fprintf(stderr, "(child1>redirecting stdout to the write end of the pipe…)\n");
        close(STDOUT_FILENO);
        dup(fd[1]);
        close(fd[0]);
        close(fd[1]);
        fprintf(stderr, "(child1>going to execute cmd: …)\n");
        execlp("ls", "ls", "-ls", NULL);
        perror("execlp failed");
        exit(1);
    }
    else {
        // Close parent stdout
        fprintf(stderr, "(parent_process>closing the write end of the pipe…)\n");
        // ANS1: if we dont close fd[1], the parent will still be considered a writer for this file descriptor,
        // Therefore, even after child1 finishes writing, child 2 wont get EOF signal, and will be left hanging.
        close(fd[1]);

        int pid2 = fork();
        // In second child
        if (pid2 == 0) {
            fprintf(stderr, "(child2>redirecting stdin to the read end of the pipe…)\n");
            close(STDIN_FILENO);
            dup(fd[0]);
            close(fd[0]);
            close(fd[1]);
            fprintf(stderr, "(child2>going to execute cmd: …)\n");
            execlp("wc", "wc", NULL);
            perror("execlp failed");
            exit(1);
        }
        // In parent
        else {
            fprintf(stderr, "(parent_process>closing the read end of the pipe…)\n");
            // ANS2: if we dont close fd[0], it will leak this file descriptor, but still work
            close(fd[0]);
            fprintf(stderr, "(parent_process>waiting for child processes to terminate…)\n");
            // ANS3: if we dont wait, the child becomes zombie, resource leak, no exit status.
            waitpid(pid, NULL, 0);
            waitpid(pid2, NULL, 0);
            fprintf(stderr, "(parent_process>exiting…)\n");
        }
    }
}