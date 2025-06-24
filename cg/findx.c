#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"


int
match(char *name, char *pattern) {
  while (*pattern && *name) {
    if (*pattern == '*') {
      pattern++;
      if (!*pattern) 
        return 1; // 如果模式以 '*' 结尾，匹配成功
      while (*name && *name != *pattern) 
        name++; // 跳过不匹配的字符
    } else if (*pattern != *name) {
      return 0; // 不匹配
    }
    pattern++;
    name++;
  }
  return *pattern == '\0' && *name == '\0'; // 完全匹配
}


void
findx(char *path, char *name){
  char buf[512], *p;
  int fd;
  struct dirent de;
  struct stat st;

  if ((fd = open(path, 0)) < 0) {
    fprintf(2, "findx: cannot open %s\n", path);
    return;
  }


  if (fstat(fd, &st) < 0) {
    fprintf(2, "findx: cannot stat %s\n", path);
    close(fd);
    return;
  }

  switch(st.type){
  case T_FILE:
      p = path + strlen(path); // 找到路径字符串的末尾
      while(p > path && *p != '/')
        p--;
      p++; // p 现在指向文件名部分

      if (match(p, name) == 1) { // 比较文件名
        printf("%s\n", path); // 匹配成功，打印路径
      }
      break;

  case T_DIR:
    if (strlen(path) + 1 + DIRSIZ + 1 > sizeof(buf)) {
        printf("findx: path too long\n");
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
      findx(buf, name);
    }
    break;
  }
  close(fd);
}

int
main(int argc, char *argv[])
{
  if (argc != 3) {
    fprintf(2, "Usage: findx <directory> <name>\n");
    exit();
  }

  // 调用 find 函数开始搜索
  findx(argv[1], argv[2]);

  exit();
}