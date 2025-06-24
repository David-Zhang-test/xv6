#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

void
find(char *path, char *name){
  char buf[512], *p;
  int fd;
  struct dirent de;
  struct stat st;

  if ((fd = open(path, 0)) < 0) {
    fprintf(2, "find: cannot open %s\n", path);
    return;
  }


  if (fstat(fd, &st) < 0) {
    fprintf(2, "find: cannot stat %s\n", path);
    close(fd);
    return;
  }

  switch(st.type){
  case T_FILE:
      p = path + strlen(path); // 找到路径字符串的末尾
      while(p > path && *p != '/')
        p--;
      p++; // p 现在指向文件名部分

      if (strcmp(p, name) == 0) { // 比较文件名
        printf("%s\n", path); // 匹配成功，打印路径
      }
      break;

  case T_DIR:
    if (strlen(path) + 1 + DIRSIZ + 1 > sizeof(buf)) {
        printf("find: path too long\n");
        break;
      }
    strcpy(buf, path);
    p = buf + strlen(buf);
    *p++ = '/'; // 在路径后添加斜杠

    // 读取目录项
    while (read(fd, &de, sizeof(de)) == sizeof(de)) {
      if (de.inum == 0) // 跳过空的目录项
        continue;

      // 跳过 "." 和 ".." 目录，防止无限递归
      if (strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0)
        continue;

      // 拼接完整的子目录/文件路径
      memmove(p, de.name, DIRSIZ);
      p[DIRSIZ] = 0; // 确保字符串以空字符结尾

      // 递归调用 find
      find(buf, name);
    }
    break;
  }
  close(fd);
}

int
main(int argc, char *argv[])
{
  if (argc != 3) {
    fprintf(2, "Usage: find <directory> <name>\n");
    exit(1);
  }

  // 调用 find 函数开始搜索
  find(argv[1], argv[2]);

  exit(0);
}