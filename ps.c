#ifdef CS333_P2
#include "types.h"
#include "user.h"

#define MAX 64

int
main(void)
{
  struct uproc* p = (struct uproc*)malloc(sizeof(struct uproc) * MAX); //"allocate memory in user program from heap"
  int num = getprocs(MAX, p);
  
#ifdef CS333_P4
  printf(1,"PID\tName\tUID\tGID\tPPID\tPrio\tElapsed_Time\tTotal_CPU_Time\tState\tSize\n");
#else
  printf(1,"PID\tName\tUID\tGID\tPPID\tElapsed_Time\tTotal_CPU_Time\tState\tSize\n");
#endif
  for(int i = 0; i < num; ++i){
    
  uint elapsed_dec = p[i].elapsed_ticks%1000;
  char * e_fill = "";
  if (elapsed_dec < 10)
    e_fill = "00";
  if (elapsed_dec >= 10 && elapsed_dec < 100)
    e_fill = "0";

  uint cpu_dec = p[i].CPU_total_ticks%1000;
  char * cpu_fill = "";
  if(cpu_dec < 10)
    cpu_fill = "00";
  if(cpu_dec >= 10 && cpu_dec < 100)
    cpu_fill = "0";
#ifdef CS333_P4
  printf(1,"%d\t%s\t%d\t%d\t%d\t%d\t%d.%s%d\t\t%d.%s%d\t\t%s\t%d\n ",
  p[i].pid, p[i].name, p[i].uid, p[i].gid, p[i].ppid, p[i].priority,
  elapsed_dec/1000, e_fill,
  elapsed_dec,
  cpu_dec/1000, cpu_fill, 
  cpu_dec,
  p[i].state, p[i].size);

#elif CS333_P2
  printf(1,"%d\t%s\t%d\t%d\t%d\t%d.%s%d\t\t%d.%s%d\t\t%s\t%d\n ",
  p[i].pid, p[i].name, p[i].uid, p[i].gid, p[i].ppid,
  elapsed_dec/1000, e_fill,
  elapsed_dec,
  cpu_dec/1000, cpu_fill, 
  cpu_dec,
  p[i].state, p[i].size);
#endif
  }


  free(p);
  exit();
}
#endif //CS333_P2
