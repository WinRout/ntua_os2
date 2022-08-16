#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

int main(int argc, char *argv) {
  char name[10];
  char no[2];
  int fd;
  for (int i=0; i < 10; i++) {
    sprintf(no, "%d", i); //int to string
    strcpy(name, "bf0");  //copy string
    strcat(name, no);     //string concatenation
    fd = openat(AT_FDCWD, name, O_RDWR|O_CREAT);
    lseek(fd, 1073741824, SEEK_SET);
    write(fd, "A", 1);
  }
}
