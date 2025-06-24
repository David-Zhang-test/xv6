#include "kernel/types.h"
#include "kernel/param.h"
#include "user/user.h"

#define MAX_LINE_LEN 1024

int
main(int argc, char *argv[]){
  char buf[MAX_LINE_LEN];
  char *new_argv[MAXARG]; // for xargs

  int i, j;
  int n;
  int line_start = 0;

  if (argc < 2) {
    fprintf(2, "Usage: xargs <command> [initial-args...]\n");
    exit();
  }

  for (i = 1; i < argc; i++ ){
    new_argv[i - 1] = argv[i]; 
  }
  j = i-1;
  while ((n = read(0, buf + line_start, 1)) > 0){
    if (buf[line_start] == '\n') { // 遇到换行符，表示一行结束
      buf[line_start] = 0; // 将换行符替换为空字符，作为字符串结束符

      // 将读取到的行作为最后一个参数添加到 new_argv
      new_argv[j] = buf;
      new_argv[j + 1] = 0; // 参数列表以 NULL 结尾

      // 创建子进程执行命令
      if (fork() == 0) {
        // 子进程：执行命令
        exec(new_argv[0], new_argv);
        // 如果 exec 失败，打印错误并退出
        fprintf(2, "xargs: exec failed for %s\n", new_argv[0]);
        exit();
      } else {
        // 父进程：等待子进程完成
        wait();
        // 重置 line_start 为 0，为下一行做准备
        line_start = 0;
      }
    } else{
      line_start++; // 继续读取下一个字符
      // 检查行缓冲区是否溢出
      if (line_start >= MAX_LINE_LEN) {
        fprintf(2, "xargs: input line too long\n");
        exit();
      }
    }
  }
  if (n<0){
    fprintf(2, "xargs: read error\n");
    exit();
  }
  exit();
}