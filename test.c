#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char *argv[])
{
  printf(1, "My pid is %d\nMy ppid is %d\n", getpid(), getppid());
  exit();
}
