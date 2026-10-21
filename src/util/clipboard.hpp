// 系统剪贴板读写（用于粘贴 OAuth 回调链接）
#pragma once

#include <string>

namespace mct::clipboard {

std::string text();
bool set_text(const std::string& text);

}  // namespace mct::clipboard
