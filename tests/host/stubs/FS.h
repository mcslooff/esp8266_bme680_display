#pragma once
#include <Arduino.h>
#include <map>
#include <memory>
#include <limits>

enum SeekMode { SeekSet };

class File {
 public:
  std::shared_ptr<std::string> bytes;
  size_t position = 0;
  size_t* readCount = nullptr;
  size_t* writeLimit = nullptr;
  explicit operator bool() const { return bool(bytes); }
  size_t size() const { return bytes ? bytes->size() : 0; }
  bool seek(size_t offset, SeekMode) {
    if (!bytes || offset > bytes->size()) return false;
    position = offset;
    return true;
  }
  int read() {
    if (!bytes || position >= bytes->size()) return -1;
    ++*readCount;
    return static_cast<uint8_t>((*bytes)[position++]);
  }
  size_t println(const String& body) {
    const std::string text = std::string(body.c_str()) + "\r\n";
    const size_t count = std::min(text.size(), *writeLimit);
    bytes->append(text.data(), count);
    return count;
  }
  bool truncate(size_t length) {
    bytes->resize(length);
    return true;
  }
  void close() { bytes.reset(); }
};

class FS {
 public:
  std::map<std::string, std::shared_ptr<std::string>> files;
  size_t readCount = 0, writeLimit = std::numeric_limits<size_t>::max();
  bool failOpen = false, failRemove = false;
  bool exists(const char* name) const { return files.count(name); }
  File open(const char* name, const char* mode) {
    File file;
    if (failOpen || (*mode == 'r' && !exists(name))) return file;
    if (!exists(name)) files[name] = std::make_shared<std::string>();
    file.bytes = files[name];
    file.position = *mode == 'a' ? file.size() : 0;
    file.readCount = &readCount;
    file.writeLimit = &writeLimit;
    return file;
  }
  bool remove(const char* name) {
    if (failRemove) return false;
    return files.erase(name) != 0;
  }
  void seed(const std::string& text) { files["buffer.json"] = std::make_shared<std::string>(text); }
  std::string buffered() const {
    const auto it = files.find("buffer.json");
    return it == files.end() ? "" : *it->second;
  }
};
