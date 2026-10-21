#include "util/fs.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "util/str.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace mct::fs {

namespace {

#ifdef _WIN32
std::wstring w(const std::string& s) { return str::widen(s); }

int64_t filetime_to_unix(const FILETIME& ft) {
  uint64_t t = (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
  if (t < 116444736000000000ULL) return 0;
  return static_cast<int64_t>((t - 116444736000000000ULL) / 10000000ULL);
}
#endif

// 流式复制单个文件，避免把 GB 级文件整体读进内存
bool stream_copy_file(const std::string& from, const std::string& to) {
#ifdef _WIN32
  HANDLE in = CreateFileW(w(from).c_str(), GENERIC_READ,
                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                          OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (in == INVALID_HANDLE_VALUE) return false;
  HANDLE out = CreateFileW(w(to).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
  if (out == INVALID_HANDLE_VALUE) {
    CloseHandle(in);
    return false;
  }
  std::vector<char> buffer(1 << 20);
  bool ok = true;
  while (true) {
    DWORD got = 0;
    if (!ReadFile(in, buffer.data(), static_cast<DWORD>(buffer.size()), &got, nullptr)) {
      ok = false;
      break;
    }
    if (got == 0) break;
    DWORD written = 0;
    if (!WriteFile(out, buffer.data(), got, &written, nullptr) || written != got) {
      ok = false;
      break;
    }
  }
  CloseHandle(in);
  CloseHandle(out);
  if (!ok) DeleteFileW(w(to).c_str());
  return ok;
#else
  FILE* in = std::fopen(from.c_str(), "rb");
  if (!in) return false;
  FILE* out = std::fopen(to.c_str(), "wb");
  if (!out) {
    std::fclose(in);
    return false;
  }
  std::vector<char> buffer(1 << 20);
  bool ok = true;
  while (true) {
    size_t got = std::fread(buffer.data(), 1, buffer.size(), in);
    if (got > 0 && std::fwrite(buffer.data(), 1, got, out) != got) {
      ok = false;
      break;
    }
    if (got < buffer.size()) {
      if (std::ferror(in)) ok = false;
      break;
    }
  }
  std::fclose(in);
  std::fclose(out);
  if (!ok) std::remove(to.c_str());
  return ok;
#endif
}

struct CopyItem {
  std::string rel;
  uint64_t size = 0;
};

void collect_files(const std::string& root, const std::string& rel,
                   std::vector<CopyItem>& items) {
  std::string dir = rel.empty() ? root : join(root, rel);
  for (const auto& entry : list_dir(dir)) {
    std::string child = rel.empty() ? entry.name : rel + "/" + entry.name;
    if (entry.is_dir) {
      collect_files(root, child, items);
    } else {
      items.push_back({child, entry.size});
    }
  }
}

}  // namespace

std::string join(const std::string& a, const std::string& b) {
  if (a.empty()) return b;
  if (b.empty()) return a;
  if (a.back() == '/' || a.back() == '\\') return a + b;
  return a + sep() + b;
}

std::string join(const std::string& a, const std::string& b, const std::string& c) {
  return join(join(a, b), c);
}

std::string join(const std::string& a, const std::string& b, const std::string& c,
                 const std::string& d) {
  return join(join(a, b, c), d);
}

std::string normalize(const std::string& path) {
  std::string s = path;
  for (char& c : s)
    if (c == '\\') c = '/';
  std::string out;
  bool last_sep = false;
  for (char c : s) {
    if (c == '/') {
      if (last_sep) continue;
      last_sep = true;
    } else {
      last_sep = false;
    }
    out += c;
  }
  while (out.size() > 1 && out.back() == '/') {
    if (out.size() == 3 && out[1] == ':') break;
    out.pop_back();
  }
  return out;
}

std::string parent(const std::string& path) {
  std::string p = normalize(path);
  size_t pos = p.find_last_of('/');
  if (pos == std::string::npos) return std::string();
  if (pos == 0) return "/";
  if (pos == 2 && p[1] == ':') return p.substr(0, 3);
  return p.substr(0, pos);
}

std::string filename(const std::string& path) {
  std::string p = normalize(path);
  size_t pos = p.find_last_of('/');
  return pos == std::string::npos ? p : p.substr(pos + 1);
}

std::string stem(const std::string& path) {
  std::string f = filename(path);
  size_t pos = f.find_last_of('.');
  return pos == std::string::npos ? f : f.substr(0, pos);
}

std::string extension(const std::string& path) {
  std::string f = filename(path);
  size_t pos = f.find_last_of('.');
  return pos == std::string::npos ? std::string() : f.substr(pos);
}

#ifdef _WIN32

bool exists(const std::string& path) {
  DWORD attr = GetFileAttributesW(w(path).c_str());
  return attr != INVALID_FILE_ATTRIBUTES;
}

bool is_dir(const std::string& path) {
  DWORD attr = GetFileAttributesW(w(path).c_str());
  return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool is_file(const std::string& path) {
  DWORD attr = GetFileAttributesW(w(path).c_str());
  return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool ensure_dir(const std::string& path) {
  if (path.empty()) return false;
  std::wstring p = w(path);
  for (wchar_t& c : p)
    if (c == L'/') c = L'\\';
  size_t pos = 0;
  if (p.size() >= 2 && p[1] == L':') pos = 2;
  if (p.size() >= 2 && p[0] == L'\\' && p[1] == L'\\') pos = 2;  // UNC
  while (pos <= p.size()) {
    size_t next = p.find(L'\\', pos);
    if (next == std::wstring::npos) next = p.size();
    std::wstring part = p.substr(0, next);
    if (!part.empty() && !(part.size() == 2 && part[1] == L':')) {
      DWORD attr = GetFileAttributesW(part.c_str());
      if (attr == INVALID_FILE_ATTRIBUTES) {
        if (!CreateDirectoryW(part.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
          return false;
      } else if ((attr & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        return false;
      }
    }
    if (next >= p.size()) break;
    pos = next + 1;
  }
  return true;
}

bool read_file(const std::string& path, std::string& out) {
  HANDLE h = CreateFileW(w(path).c_str(), GENERIC_READ,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER size;
  if (!GetFileSizeEx(h, &size)) {
    CloseHandle(h);
    return false;
  }
  out.clear();
  if (size.QuadPart > 0) {
    out.resize(static_cast<size_t>(size.QuadPart));
    size_t done = 0;
    while (done < out.size()) {
      DWORD got = 0;
      DWORD want = static_cast<DWORD>(std::min<size_t>(out.size() - done, 1 << 20));
      if (!ReadFile(h, &out[done], want, &got, nullptr) || got == 0) {
        CloseHandle(h);
        return false;
      }
      done += got;
    }
  }
  CloseHandle(h);
  return true;
}

bool write_file(const std::string& path, const std::string& data) {
  std::string dir = parent(path);
  if (!dir.empty() && !ensure_dir(dir)) return false;
  HANDLE h = CreateFileW(w(path).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  size_t done = 0;
  while (done < data.size()) {
    DWORD wrote = 0;
    DWORD want = static_cast<DWORD>(std::min<size_t>(data.size() - done, 1 << 20));
    if (!WriteFile(h, data.data() + done, want, &wrote, nullptr) || wrote == 0) {
      CloseHandle(h);
      return false;
    }
    done += wrote;
  }
  CloseHandle(h);
  return true;
}

bool write_file_atomic(const std::string& path, const std::string& data) {
  std::string tmp = path + ".tmp";
  if (!write_file(tmp, data)) return false;
  return rename_path(tmp, path);
}

bool remove_file(const std::string& path) {
  return DeleteFileW(w(path).c_str()) != 0;
}

bool remove_dir_recursive(const std::string& path) {
  if (!exists(path)) return true;
  if (!is_dir(path)) return remove_file(path);
  std::vector<FileInfo> entries = list_dir(path);
  for (const auto& e : entries) {
    std::string child = join(path, e.name);
    if (e.is_dir)
      remove_dir_recursive(child);
    else
      remove_file(child);
  }
  return RemoveDirectoryW(w(path).c_str()) != 0;
}

bool rename_path(const std::string& from, const std::string& to) {
  return MoveFileExW(w(from).c_str(), w(to).c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}

bool copy_file(const std::string& from, const std::string& to) {
  return CopyFileW(w(from).c_str(), w(to).c_str(), FALSE) != 0;
}

uint64_t file_size(const std::string& path) {
  WIN32_FILE_ATTRIBUTE_DATA d;
  if (!GetFileAttributesExW(w(path).c_str(), GetFileExInfoStandard, &d)) return 0;
  return (static_cast<uint64_t>(d.nFileSizeHigh) << 32) | d.nFileSizeLow;
}

int64_t mtime(const std::string& path) {
  WIN32_FILE_ATTRIBUTE_DATA d;
  if (!GetFileAttributesExW(w(path).c_str(), GetFileExInfoStandard, &d)) return 0;
  return filetime_to_unix(d.ftLastWriteTime);
}

std::vector<FileInfo> list_dir(const std::string& path) {
  std::vector<FileInfo> out;
  std::wstring pattern = w(path);
  if (!pattern.empty() && pattern.back() != L'\\' && pattern.back() != L'/') pattern += L'\\';
  pattern += L'*';
  WIN32_FIND_DATAW fd;
  HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE) return out;
  do {
    std::wstring name = fd.cFileName;
    if (name == L"." || name == L"..") continue;
    FileInfo info;
    info.name = str::narrow(name);
    info.is_dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    info.size = (static_cast<uint64_t>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
    info.mtime = filetime_to_unix(fd.ftLastWriteTime);
    out.push_back(std::move(info));
  } while (FindNextFileW(h, &fd));
  FindClose(h);
  return out;
}

std::string absolute(const std::string& path) {
  std::wstring in = w(path);
  DWORD need = GetFullPathNameW(in.c_str(), 0, nullptr, nullptr);
  if (need == 0) return path;
  std::wstring out(need, L'\0');
  DWORD got = GetFullPathNameW(in.c_str(), need, &out[0], nullptr);
  if (got == 0) return path;
  out.resize(got);
  return str::narrow(out);
}

std::string current_dir() {
  DWORD need = GetCurrentDirectoryW(0, nullptr);
  if (need == 0) return ".";
  std::wstring buf(need, L'\0');
  DWORD got = GetCurrentDirectoryW(need, &buf[0]);
  buf.resize(got);
  return str::narrow(buf);
}

std::string home_dir() {
  std::string v = get_env("USERPROFILE");
  if (!v.empty()) return v;
  std::string d = get_env("HOMEDRIVE") + get_env("HOMEPATH");
  if (!d.empty() && d != "\\") return d;
  return appdata_dir();
}

std::string appdata_dir() {
  std::string v = get_env("APPDATA");
  if (!v.empty()) return v;
  return current_dir();
}

std::string temp_dir() {
  wchar_t buf[MAX_PATH + 1];
  DWORD n = GetTempPathW(MAX_PATH + 1, buf);
  if (n == 0) return ".";
  return str::narrow(std::wstring(buf, n));
}

std::string exe_dir() {
  wchar_t buf[MAX_PATH * 4];
  DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(MAX_PATH * 4));
  if (n == 0) return current_dir();
  std::string path = str::narrow(std::wstring(buf, n));
  std::string dir = parent(path);
  return dir.empty() ? current_dir() : dir;
}

std::string get_env(const std::string& name) {
  std::wstring key = str::widen(name);
  DWORD need = GetEnvironmentVariableW(key.c_str(), nullptr, 0);
  if (need == 0) return std::string();
  std::wstring buf(need, L'\0');
  DWORD got = GetEnvironmentVariableW(key.c_str(), &buf[0], need);
  if (got == 0) return std::string();
  buf.resize(got);
  return str::narrow(buf);
}

std::vector<std::string> path_dirs() {
  std::vector<std::string> out;
  for (const auto& p : str::split(get_env("PATH"), ';')) {
    if (!p.empty()) out.push_back(p);
  }
  return out;
}

bool equal(const std::string& a, const std::string& b) {
  return str::iequals(normalize(a), normalize(b));
}

#else  // POSIX

bool exists(const std::string& path) {
  struct stat st;
  return ::stat(path.c_str(), &st) == 0;
}

bool is_dir(const std::string& path) {
  struct stat st;
  return ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool is_file(const std::string& path) {
  struct stat st;
  return ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool ensure_dir(const std::string& path) {
  if (path.empty()) return false;
  std::string cur;
  for (const auto& part : str::split(path, '/')) {
    if (part.empty()) {
      cur = "/";
      continue;
    }
    cur = cur.empty() ? part : (cur == "/" ? "/" + part : cur + "/" + part);
    if (::mkdir(cur.c_str(), 0755) != 0 && errno != EEXIST) return false;
  }
  return true;
}

bool read_file(const std::string& path, std::string& out) {
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return false;
  out.clear();
  char buf[1 << 16];
  while (true) {
    size_t n = std::fread(buf, 1, sizeof(buf), f);
    if (n == 0) break;
    out.append(buf, n);
  }
  bool ok = std::ferror(f) == 0;
  std::fclose(f);
  return ok;
}

bool write_file(const std::string& path, const std::string& data) {
  std::string dir = parent(path);
  if (!dir.empty() && !ensure_dir(dir)) return false;
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return false;
  bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
  std::fclose(f);
  return ok;
}

bool write_file_atomic(const std::string& path, const std::string& data) {
  std::string tmp = path + ".tmp";
  if (!write_file(tmp, data)) return false;
  return rename_path(tmp, path);
}

bool remove_file(const std::string& path) { return ::unlink(path.c_str()) == 0; }

bool remove_dir_recursive(const std::string& path) {
  if (!is_dir(path)) return remove_file(path);
  for (const auto& e : list_dir(path)) {
    std::string child = join(path, e.name);
    if (e.is_dir)
      remove_dir_recursive(child);
    else
      remove_file(child);
  }
  return ::rmdir(path.c_str()) == 0;
}

bool rename_path(const std::string& from, const std::string& to) {
  return ::rename(from.c_str(), to.c_str()) == 0;
}

bool copy_file(const std::string& from, const std::string& to) {
  std::string data;
  if (!read_file(from, data)) return false;
  return write_file(to, data);
}

uint64_t file_size(const std::string& path) {
  struct stat st;
  if (::stat(path.c_str(), &st) != 0) return 0;
  return static_cast<uint64_t>(st.st_size);
}

int64_t mtime(const std::string& path) {
  struct stat st;
  if (::stat(path.c_str(), &st) != 0) return 0;
  return static_cast<int64_t>(st.st_mtime);
}

std::vector<FileInfo> list_dir(const std::string& path) {
  std::vector<FileInfo> out;
  DIR* d = ::opendir(path.c_str());
  if (!d) return out;
  while (struct dirent* ent = ::readdir(d)) {
    std::string name = ent->d_name;
    if (name == "." || name == "..") continue;
    struct stat st;
    std::string full = join(path, name);
    if (::stat(full.c_str(), &st) != 0) continue;
    FileInfo info;
    info.name = name;
    info.is_dir = S_ISDIR(st.st_mode);
    info.size = static_cast<uint64_t>(st.st_size);
    info.mtime = static_cast<int64_t>(st.st_mtime);
    out.push_back(std::move(info));
  }
  ::closedir(d);
  return out;
}

std::string absolute(const std::string& path) {
  char buf[4096];
  if (!::realpath(path.c_str(), buf)) return path;
  return buf;
}

std::string current_dir() {
  char buf[4096];
  if (!::getcwd(buf, sizeof(buf))) return ".";
  return buf;
}

std::string home_dir() {
  std::string v = get_env("HOME");
  if (!v.empty()) return v;
  struct passwd* pw = ::getpwuid(::getuid());
  return pw ? pw->pw_dir : ".";
}

std::string appdata_dir() {
  std::string v = get_env("XDG_DATA_HOME");
  if (!v.empty()) return v;
  return join(home_dir(), ".local/share");
}

std::string temp_dir() {
  std::string v = get_env("TMPDIR");
  return v.empty() ? "/tmp" : v;
}

std::string exe_dir() {
  char buf[4096];
  ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (n <= 0) return current_dir();
  buf[n] = '\0';
  return parent(buf);
}

std::string get_env(const std::string& name) {
  const char* v = std::getenv(name.c_str());
  return v ? v : "";
}

std::vector<std::string> path_dirs() {
  std::vector<std::string> out;
  for (const auto& p : str::split(get_env("PATH"), ':')) {
    if (!p.empty()) out.push_back(p);
  }
  return out;
}

bool equal(const std::string& a, const std::string& b) { return normalize(a) == normalize(b); }

#endif

bool copy_dir(const std::string& src, const std::string& dst,
              const std::function<void(const CopyProgress&)>& on_progress,
              const std::function<bool()>& cancelled) {
  const std::string source = normalize(absolute(src));
  const std::string target = normalize(absolute(dst));
  if (source.empty() || target.empty()) return false;
  if (source == target) return true;
  // 目标目录位于源目录内部时会导致无限递归
  if (str::starts_with(target + "/", source + "/")) return false;
  if (!ensure_dir(target)) return false;

  std::vector<CopyItem> items;
  collect_files(source, "", items);
  CopyProgress progress;
  progress.files_total = items.size();
  for (const auto& item : items) progress.bytes_total += item.size;

  for (const auto& item : items) {
    if (cancelled && cancelled()) return false;
    const std::string from = join(source, item.rel);
    const std::string to = join(target, item.rel);
    progress.current = item.rel;
    if (is_file(to) && file_size(to) == item.size) {
      progress.bytes_done += item.size;
      ++progress.files_done;
      if (on_progress) on_progress(progress);
      continue;
    }
    const std::string dir = parent(to);
    if (!dir.empty() && !ensure_dir(dir)) return false;
    if (!stream_copy_file(from, to)) return false;
    progress.bytes_done += item.size;
    ++progress.files_done;
    if (on_progress) on_progress(progress);
  }
  return true;
}

}  // namespace mct::fs
