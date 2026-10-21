// 测试用的假 Java：只输出可被解析的版本号，便于验证启动参数构建
#include <cstdio>

int main() {
  std::fprintf(stdout, "openjdk version \"17.0.2\" 2022-01-18\n");
  std::fprintf(stdout, "OpenJDK Runtime Environment (build 17.0.2+8)\n");
  return 0;
}
