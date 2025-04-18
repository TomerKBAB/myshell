#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdbool.h>

#include "LineParser.h"
int main(int argc, char **argv) {
    if (argc == 1) {
        fprintf(stderr, "message needs to be provided as argument");
        exit(1);
    }

    char *msg = argv[1];

    int fd[2];
    if (pipe(fd) == -1) {
        perror("pipe failed");
        exit(1);
    }

    int pid = fork();
    //Error in fork
    if (pid < 0) {
        exit(1);
    }

    // Child process execute
    if (pid == 0) {
        close(fd[1]);

        close(0);
        dup2(fd[0], STDIN_FILENO);
        close(fd[0]);
        char recv[1024];
        fgets(recv, 1024, stdin);
        printf("msg from father: %s\n", recv);
    }
    else {
        close(fd[0]);
        close(1);
        dup2(fd[1], STDOUT_FILENO);
        close(fd[1]);
        sleep(1);
        // New line for fgets at child
        fprintf(stdout, "%s\n",msg);
    }
}