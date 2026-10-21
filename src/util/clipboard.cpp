#include "util/clipboard.hpp"

#include <cstdio>
#include <cstring>

#include "util/str.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <cstdlib>
#endif

namespace mct::clipboard {

#ifdef _WIN32

std::string text() {
  if (!OpenClipboard(nullptr)) return std::string();
  std::string out;
  HANDLE handle = GetClipboardData(CF_UNICODETEXT);
  if (handle) {
    const wchar_t* data = static_cast<const wchar_t*>(GlobalLock(handle));
    if (data) out = str::narrow(std::wstring(data));
    GlobalUnlock(handle);
  }
  CloseClipboard();
  return out;
}

bool set_text(const std::string& value) {
  if (!OpenClipboard(nullptr)) return false;
  EmptyClipboard();
  std::wstring wide = str::widen(value);
  const size_t bytes = (wide.size() + 1) * sizeof(wchar_t);
  HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE, bytes);
  if (!handle) {
    CloseClipboard();
    return false;
  }
  void* target = GlobalLock(handle);
  if (!target) {
    GlobalFree(handle);
    CloseClipboard();
    return false;
  }
  std::memcpy(target, wide.c_str(), bytes);
  GlobalUnlock(handle);
  if (!SetClipboardData(CF_UNICODETEXT, handle)) {
    GlobalFree(handle);
    CloseClipboard();
    return false;
  }
  CloseClipboard();
  return true;
}

#else

std::string text() {
  FILE* pipe = popen("xclip -selection clipboard -o 2>/dev/null || pbpaste 2>/dev/null", "r");
  if (!pipe) return std::string();
  std::string out;
  char buffer[4096];
  while (std::fgets(buffer, sizeof(buffer), pipe)) out += buffer;
  pclose(pipe);
  return out;
}

bool set_text(const std::string& value) {
  FILE* pipe = popen("xclip -selection clipboard 2>/dev/null || pbcopy 2>/dev/null", "w");
  if (!pipe) return false;
  bool ok = std::fwrite(value.data(), 1, value.size(), pipe) == value.size();
  pclose(pipe);
  return ok;
}

#endif

}  // namespace mct::clipboard
