#pragma once
// Linux implementation of the deliberately small Arduino-shaped platform
// contract in src/platform/platform.h.  Keep machine-specific code here: the
// shared BBS core must build unchanged on the ESP32 and on the server.

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cerrno>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <sstream>
#include <system_error>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include <mbedtls/sha256.h>
#if defined(__linux__)
#include <sys/random.h>
#endif

using std::max;
using std::min;

class String {
 public:
  String() = default;
  String(const char *value) : value_(value ? value : "") {}
  String(const char *value, size_t length) : value_(value ? std::string(value, length) : "") {}
  String(const std::string &value) : value_(value) {}
  String(std::string &&value) : value_(std::move(value)) {}
  String(char value) : value_(1, value) {}
  String(bool value) : value_(value ? "1" : "0") {}
  String(signed char value) : value_(std::to_string(value)) {}
  String(unsigned char value) : value_(std::to_string(value)) {}
  String(short value) : value_(std::to_string(value)) {}
  String(unsigned short value) : value_(std::to_string(value)) {}
  String(int value) : value_(std::to_string(value)) {}
  String(unsigned int value) : value_(std::to_string(value)) {}
  String(long value) : value_(std::to_string(value)) {}
  String(unsigned long value) : value_(std::to_string(value)) {}
  String(long long value) : value_(std::to_string(value)) {}
  String(unsigned long long value) : value_(std::to_string(value)) {}
  String(float value) : value_(formatFloat(value)) {}
  String(double value) : value_(formatFloat(value)) {}

  String &operator=(const char *value) { value_ = value ? value : ""; return *this; }
  String &operator=(const std::string &value) { value_ = value; return *this; }
  String &operator=(std::string &&value) { value_ = std::move(value); return *this; }

  const char *c_str() const { return value_.c_str(); }
  size_t length() const { return value_.size(); }
  bool isEmpty() const { return value_.empty(); }
  char charAt(size_t index) const { return index < value_.size() ? value_[index] : 0; }
  char operator[](size_t index) const { return charAt(index); }
  char &operator[](size_t index) { return value_[index]; }
  void setCharAt(size_t index, char value) { if (index < value_.size()) value_[index] = value; }
  void reserve(size_t size) { value_.reserve(size); }
  void clear() { value_.clear(); }

  String substring(size_t from) const { return from < value_.size() ? String(value_.substr(from)) : String(); }
  String substring(size_t from, size_t to) const {
    if (from > to) std::swap(from, to);
    if (from >= value_.size() || to <= from) return String();
    return String(value_.substr(from, std::min(to, value_.size()) - from));
  }
  int indexOf(char needle, size_t from = 0) const { return indexOf(String(needle), from); }
  int indexOf(const char *needle, size_t from = 0) const { return indexOf(String(needle), from); }
  int indexOf(const String &needle, size_t from = 0) const {
    const size_t result = value_.find(needle.value_, from);
    return result == std::string::npos ? -1 : static_cast<int>(result);
  }
  int lastIndexOf(char needle) const { return lastIndexOf(String(needle)); }
  int lastIndexOf(const char *needle) const { return lastIndexOf(String(needle)); }
  int lastIndexOf(const String &needle) const {
    const size_t result = value_.rfind(needle.value_);
    return result == std::string::npos ? -1 : static_cast<int>(result);
  }
  bool startsWith(const char *prefix) const { return startsWith(String(prefix)); }
  bool startsWith(const String &prefix) const { return value_.size() >= prefix.value_.size() && value_.compare(0, prefix.value_.size(), prefix.value_) == 0; }
  bool endsWith(const char *suffix) const { return endsWith(String(suffix)); }
  bool endsWith(const String &suffix) const {
    return value_.size() >= suffix.value_.size() && value_.compare(value_.size() - suffix.value_.size(), suffix.value_.size(), suffix.value_) == 0;
  }
  void replace(const String &from, const String &to) {
    if (from.value_.empty()) return;
    size_t at = 0;
    while ((at = value_.find(from.value_, at)) != std::string::npos) {
      value_.replace(at, from.value_.size(), to.value_);
      at += to.value_.size();
    }
  }
  void remove(size_t index, size_t count = std::string::npos) {
    if (index < value_.size()) value_.erase(index, count);
  }
  void trim() {
    const auto first = std::find_if_not(value_.begin(), value_.end(), [](unsigned char c) { return std::isspace(c); });
    const auto last = std::find_if_not(value_.rbegin(), value_.rend(), [](unsigned char c) { return std::isspace(c); }).base();
    if (first >= last) value_.clear(); else value_ = std::string(first, last);
  }
  void toUpperCase() { for (char &c : value_) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); }
  void toLowerCase() { for (char &c : value_) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
  long toInt() const {
    char *end = nullptr;
    errno = 0;
    const long parsed = std::strtol(value_.c_str(), &end, 10);
    if (end == value_.c_str()) return 0;
    // ESP32 Arduino's atol() has a 32-bit long. Keep that saturation on a
    // 64-bit Linux host rather than accepting a larger value there.
    if (errno == ERANGE) return parsed < 0 ? INT32_MIN : INT32_MAX;
    if (parsed > INT32_MAX) return INT32_MAX;
    if (parsed < INT32_MIN) return INT32_MIN;
    return parsed;
  }
  bool equalsIgnoreCase(const String &other) const {
    if (value_.size() != other.value_.size()) return false;
    for (size_t i = 0; i < value_.size(); ++i)
      if (std::tolower(static_cast<unsigned char>(value_[i])) != std::tolower(static_cast<unsigned char>(other.value_[i]))) return false;
    return true;
  }
  size_t concat(const char *bytes, size_t length) {
    if (!bytes) return 0;
    value_.append(bytes, length);
    return length;
  }
  size_t concat(const char *text) { return text ? concat(text, std::strlen(text)) : 0; }

  String &operator+=(const String &other) { value_ += other.value_; return *this; }
  String &operator+=(const char *other) { if (other) value_ += other; return *this; }
  String &operator+=(char other) { value_ += other; return *this; }
  template <typename T, typename = std::enable_if_t<std::is_arithmetic_v<T>>>
  String &operator+=(T other) { value_ += String(other).value_; return *this; }
  template <typename T, typename = std::enable_if_t<std::is_arithmetic_v<T>>>
  String operator+(T other) const { String result(*this); result += other; return result; }

  explicit operator bool() const { return !value_.empty(); }
  operator std::string() const { return value_; }
  friend bool operator==(const String &left, const String &right) { return left.value_ == right.value_; }
  friend bool operator!=(const String &left, const String &right) { return !(left == right); }
  friend bool operator<(const String &left, const String &right) { return left.value_ < right.value_; }
  friend String operator+(String left, const String &right) { left += right; return left; }
  friend String operator+(const char *left, const String &right) { return String(left) + right; }
  friend String operator+(char left, const String &right) { return String(left) + right; }

 private:
  static std::string formatFloat(double value) {
    std::ostringstream text;
    text << std::fixed << std::setprecision(2) << value;
    return text.str();
  }
  std::string value_;
};

template <typename T>
constexpr T constrain(T value, T low, T high) { return value < low ? low : (value > high ? high : value); }

inline uint32_t millis() {
  static const auto started = std::chrono::steady_clock::now();
  return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count());
}
inline void delay(uint32_t milliseconds) { std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds)); }

class ServerSerial {
 public:
  void begin(unsigned long) {}
  size_t print(const String &text) { std::cout << text.c_str(); return text.length(); }
  size_t print(const char *text) { std::cout << (text ? text : ""); return text ? std::strlen(text) : 0; }
  size_t println() { std::cout << '\n'; return 1; }
  size_t println(const String &text) { print(text); return println(); }
  int printf(const char *format, ...) {
    va_list args;
    va_start(args, format);
    const int wrote = std::vprintf(format, args);
    va_end(args);
    return wrote;
  }
};
inline ServerSerial Serial;

inline const char *platformName() { return "server"; }
inline constexpr uint8_t platformMaxNodes() { return 32; }
inline bool platformSystemClock(uint32_t &seconds) {
  const time_t now = std::time(nullptr);
  if (now < 0) return false;
#if defined(__linux__)
  // clockText() deliberately has no time-zone machinery. Encode the Linux
  // host's current local wall clock into its civil-time value instead of
  // showing UTC on a host whose configured zone is, for example, Helsinki.
  tm local{};
  if (!localtime_r(&now, &local)) return false;
  const int64_t wallClock = static_cast<int64_t>(now) + local.tm_gmtoff;
  if (wallClock < 0 || static_cast<uint64_t>(wallClock) > UINT32_MAX) return false;
  seconds = static_cast<uint32_t>(wallClock);
#else
  if (static_cast<uintmax_t>(now) > UINT32_MAX) return false;
  seconds = static_cast<uint32_t>(now);
#endif
  return true;
}
inline uint32_t bbsRandom() {
  uint32_t value = 0;
#if defined(__linux__)
  if (getrandom(&value, sizeof(value), 0) == static_cast<ssize_t>(sizeof(value))) return value;
#endif
  static std::random_device device;
  static std::mt19937 generator(device());
  return generator();
}
inline void *bigAlloc(size_t size) { return std::malloc(size); }
inline void *bigRealloc(void *memory, size_t size) { return std::realloc(memory, size); }
inline bool bigMemory() { return true; }

struct PlatformMutex {
  std::recursive_mutex mutex;
  void init() {}
  void lock() { mutex.lock(); }
  void unlock() { mutex.unlock(); }
};

// Phase 4 replaces this inert socket with the POSIX non-blocking client.
class NetClient {
 public:
  explicit operator bool() const { return false; }
  bool connected() const { return false; }
  int available() const { return 0; }
  int read() { return -1; }
  size_t write(const uint8_t *, size_t size) { return size; }
  size_t print(const String &text) { return text.length(); }
  void stop() {}
};

constexpr uint8_t FILE_READ = 0;
constexpr uint8_t FILE_WRITE = 1;
constexpr uint8_t FILE_APPEND = 2;

namespace tinybbs_server {
inline std::filesystem::path &rootPath() {
  static std::filesystem::path root;
  return root;
}
inline bool isSafeCardPath(const String &logical) {
  const std::string path = logical;
  if (path.rfind("/BBS", 0) != 0 || (path.size() > 4 && path[4] != '/')) return false;
  return std::filesystem::path(path).lexically_normal().generic_string().rfind("/BBS", 0) == 0 && path.find("..") == std::string::npos;
}
inline std::filesystem::path mapPath(const String &logical) {
  if (!isSafeCardPath(logical)) return {};
  const std::string path = logical;
  const std::string relative = path.size() == 4 ? "BBS" : "BBS" + path.substr(4);
  return rootPath() / std::filesystem::path(relative);
}
inline time_t fileTime(const std::filesystem::file_time_type &time) {
  const auto systemTime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
      time - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
  return std::chrono::system_clock::to_time_t(systemTime);
}
}  // namespace tinybbs_server

inline bool tinybbsSetRoot(const std::string &root) {
  std::error_code error;
  const auto absolute = std::filesystem::absolute(root, error);
  if (error || !std::filesystem::is_directory(absolute / "BBS", error)) return false;
  tinybbs_server::rootPath() = absolute;
  return true;
}

class File {
 public:
  File() = default;
  explicit operator bool() const { return state_ && state_->open; }
  bool operator!() const { return !static_cast<bool>(*this); }
  bool isDirectory() const { return state_ && state_->directory; }
  size_t size() const { return state_ && !state_->directory ? state_->size : 0; }
  uint32_t position() const {
    if (!state_ || state_->directory || !state_->stream.is_open()) return 0;
    const auto position = state_->writable ? state_->stream.tellp() : state_->stream.tellg();
    return position >= 0 ? static_cast<uint32_t>(position) : 0;
  }
  bool seek(uint32_t position) {
    if (!state_ || state_->directory || !state_->stream.is_open()) return false;
    state_->stream.clear();
    if (state_->writable) state_->stream.seekp(position);
    else state_->stream.seekg(position);
    return !state_->stream.fail();
  }
  int available() {
    if (!state_ || state_->directory || !state_->stream.is_open()) return 0;
    const int next = state_->stream.peek();
    return next == EOF ? 0 : 1;
  }
  int peek() {
    if (!state_ || state_->directory || !state_->stream.is_open()) return -1;
    const int next = state_->stream.peek();
    return next == EOF ? -1 : next;
  }
  int read() {
    if (!state_ || state_->directory || !state_->stream.is_open()) return -1;
    const int value = state_->stream.get();
    return value == EOF ? -1 : value;
  }
  size_t read(uint8_t *buffer, size_t length) {
    if (!state_ || state_->directory || !state_->stream.is_open() || !buffer) return 0;
    state_->stream.read(reinterpret_cast<char *>(buffer), static_cast<std::streamsize>(length));
    return static_cast<size_t>(state_->stream.gcount());
  }
  size_t write(const uint8_t *buffer, size_t length) {
    if (!state_ || state_->directory || !state_->stream.is_open() || !buffer) return 0;
    state_->stream.write(reinterpret_cast<const char *>(buffer), static_cast<std::streamsize>(length));
    if (state_->stream.fail()) return 0;
    state_->size = std::max(state_->size, static_cast<size_t>(state_->stream.tellp()));
    return length;
  }
  size_t write(const char *buffer, size_t length) { return write(reinterpret_cast<const uint8_t *>(buffer), length); }
  size_t print(const String &text) { return write(text.c_str(), text.length()); }
  size_t print(const char *text) { return text ? write(text, std::strlen(text)) : 0; }
  template <typename T, typename = std::enable_if_t<std::is_arithmetic_v<T>>>
  size_t print(T value) { return print(String(value)); }
  size_t println() { return print("\r\n"); }
  size_t println(const String &text) { return print(text) + println(); }
  size_t println(const char *text) { return print(text) + println(); }
  String readStringUntil(char delimiter) {
    String result;
    for (int value; (value = read()) >= 0;) {
      if (value == delimiter) break;
      result += static_cast<char>(value);
    }
    return result;
  }
  File openNextFile() {
    if (!state_ || !state_->directory || state_->directoryAt >= state_->entries.size()) return File();
    const auto child = state_->entries[state_->directoryAt++];
    const String logical = state_->logical + "/" + child.filename().generic_string().c_str();
    return openHost(child, FILE_READ, logical);
  }
  String name() const { return state_ ? String(state_->host.filename().generic_string()) : String(); }
  String path() const { return state_ ? state_->logical : String(); }
  time_t getLastWrite() const {
    if (!state_) return 0;
    std::error_code error;
    const auto changed = std::filesystem::last_write_time(state_->host, error);
    return error ? 0 : tinybbs_server::fileTime(changed);
  }
  void close() { if (state_ && state_->stream.is_open()) state_->stream.close(); if (state_) state_->open = false; }

 private:
  struct State {
    std::filesystem::path host;
    String logical;
    mutable std::fstream stream;
    std::vector<std::filesystem::path> entries;
    size_t directoryAt = 0, size = 0;
    bool open = false, directory = false, writable = false;
  };
  std::shared_ptr<State> state_;

  static File openHost(const std::filesystem::path &host, uint8_t mode, const String &logical = String()) {
    File file;
    std::error_code error;
    const bool exists = std::filesystem::exists(host, error);
    if (error || (mode == FILE_READ && !exists)) return file;
    file.state_ = std::make_shared<State>();
    file.state_->host = host;
    file.state_->logical = logical.length() ? logical : String("/BBS/") + host.filename().generic_string().c_str();
    file.state_->directory = exists && std::filesystem::is_directory(host, error);
    if (error) { file.state_.reset(); return file; }
    file.state_->open = true;
    if (file.state_->directory) {
      for (const auto &entry : std::filesystem::directory_iterator(host, error)) if (!error) file.state_->entries.push_back(entry.path());
      std::sort(file.state_->entries.begin(), file.state_->entries.end());
      return file;
    }
    std::ios::openmode openMode = std::ios::binary;
    if (mode == FILE_READ) openMode |= std::ios::in;
    else if (mode == FILE_WRITE) openMode |= std::ios::out | std::ios::trunc;
    else openMode |= std::ios::out | std::ios::app;
    file.state_->writable = mode != FILE_READ;
    file.state_->stream.open(host, openMode);
    if (!file.state_->stream.is_open()) { file.state_.reset(); return file; }
    file.state_->size = std::filesystem::file_size(host, error);
    if (error) file.state_->size = 0;
    return file;
  }
  friend class ServerSDMMC;
};

class ServerSDMMC {
 public:
  bool begin(const char * = "/sdcard", bool = true) { return !tinybbs_server::rootPath().empty() && std::filesystem::is_directory(tinybbs_server::rootPath() / "BBS"); }
  void setPins(int, int, int) {}
  File open(const String &logical, uint8_t mode = FILE_READ) {
    const auto host = tinybbs_server::mapPath(logical);
    if (host.empty()) return File();
    return File::openHost(host, mode, logical);
  }
  bool exists(const String &logical) const { const auto host = tinybbs_server::mapPath(logical); std::error_code error; return !host.empty() && std::filesystem::exists(host, error); }
  bool mkdir(const String &logical) const { const auto host = tinybbs_server::mapPath(logical); std::error_code error; return !host.empty() && std::filesystem::create_directories(host, error); }
  bool remove(const String &logical) const { const auto host = tinybbs_server::mapPath(logical); std::error_code error; return !host.empty() && std::filesystem::remove(host, error); }
  // FAT's rmdir is the same operation as remove for an empty directory.
  bool rmdir(const String &logical) const { return remove(logical); }
  bool rename(const String &from, const String &to) const {
    const auto source = tinybbs_server::mapPath(from), target = tinybbs_server::mapPath(to);
    if (source.empty() || target.empty()) return false;
    std::error_code error;
    if (std::filesystem::exists(target, error) || error) return false;
    std::filesystem::rename(source, target, error);
    return !error;
  }
  uint64_t totalBytes() const { std::error_code error; const auto space = std::filesystem::space(tinybbs_server::rootPath(), error); return error ? 0 : space.capacity; }
  uint64_t usedBytes() const { std::error_code error; const auto space = std::filesystem::space(tinybbs_server::rootPath(), error); return error ? 0 : space.capacity - space.available; }
};
inline ServerSDMMC SD_MMC;
