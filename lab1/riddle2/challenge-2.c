#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/wait.h>
#include <stdint.h>

int main(int argc, char **argv) {
  pid_t pid;
  pid = fork();
  if (pid == 0) {
    execve("./riddle", argv, argv);
  }
  else {
    sleep(1);
    kill(pid, SIGCONT);
    wait(NULL);
  }
}
