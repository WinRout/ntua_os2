#include<stdio.h>
#include <unistd.h>
#include <sys/types.h>

int main (int argc, char **argv, char **envp) {
  int fd1[2];
  int fd2[2];
  pipe(fd1);
  pipe(fd2);
  dup2 (fd1[0], 33);
  dup2 (fd1[1], 34);
  dup2 (fd2[0], 53);
  dup2 (fd2[1], 54);
  execve("./riddle", argv, envp);
}
