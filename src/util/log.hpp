// 简单日志：写入 <home>/logs/mct.log
#pragma once

#include <string>

namespace mct::log {

void init(const std::string& path);
void shutdown();
void write(const char* level, const std::string& message);
void debug(const std::string& message);
void info(const std::string& message);
void warn(const std::string& message);
void error(const std::string& message);
std::string path();

}  // namespace mct::log
