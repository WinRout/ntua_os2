#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/wait.h>
#include <stdint.h>

int main (int argc, char **argv) {
  char *file = argv[1];
  char *letter = argv[2];
  int fd = open(file, O_RDWR);
  printf("FDq: %d\n", fd);
  lseek(fd, 0x6f ,SEEK_SET);
  if(write(fd, letter, 1) == 1) {
    printf("SUCCESS\n");
  }
  else {
    printf("FAIL\n");
  }
}
