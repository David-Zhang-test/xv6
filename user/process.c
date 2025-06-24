#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  int status_type;
  int count;

  if (argc != 2) {
    fprintf(2, "Usage: process <status_type>\n");
    fprintf(2, "  status_type: 1=RUNNING, 2=RUNNABLE, 3=SLEEPING\n");
    exit(1);
  }

  status_type = atoi(argv[1]); // 将字符串参数转换为整数

  if (status_type < 1 || status_type > 3) {
    fprintf(2, "Invalid status_type. Must be 1, 2, or 3.\n");
    exit(1);
  }

  // 调用新的系统调用
  count = process(status_type);

  if (count == -1) {
    fprintf(2, "Error: process system call failed or invalid parameter.\n");
    exit(1);
  } else {
    printf("Number of processes in state %d: %d\n", status_type, count);
  }

  exit(0);
}