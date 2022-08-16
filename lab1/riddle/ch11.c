#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/wait.h>

int main (int argc, char **argv) {
  int fd = open("secret_number", O_RDWR|O_CREAT);
  pid_t pid = fork();
  if (pid==0) {
    execve("./riddle", NULL, NULL);
  }
  else {
    sleep(1);
    char buff[8];
    while (buff[0] != ':') { //read until ':'
        read(fd, buff, 1);
    }
    read(fd, buff, 1); //read space
    read(fd, buff, 8); //read number (actual answer)
    printf("%s\n", buff);
    kill(pid, SIGCONT);
    //wait(NULL);
  }
}
