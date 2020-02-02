#ifdef CS333_P2
#include "types.h"
#include "user.h"

int
main(int argc, char * argv[])
{
  int tick = uptime();
  int time = fork();
  if (time < 0){ //fork failed
    exit();
  }
  else if (time == 0){ //child
    exec(argv[1], argv+1);
    exit();
  }
  else{ //parent
    time = wait();
  }

  int t_time = uptime() - tick;

  printf(1, "%s ran in %d.%d seconds\n", argv[1], t_time/1000, t_time%1000);

  exit();
}
#endif //CS333_P2
