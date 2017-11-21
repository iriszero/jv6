#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char *argv[])
{
  int pid = fork();
  if (pid < 0)
  {
    printf(1, "Fork Error\n");
    return 1;
  }
  else if (pid == 0) {
    while(1)
    {
      printf(1, "Child\n");
      yield();
    }
  }
  else {
    while(1)
    {
      printf(1, "Parent\n");
      yield();
    }
  }
  exit();
}
