#pragma once

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>

#include <Arduino.h>

class File {
 public:
  File() : data_(std::make_shared<std::string>()) {}
  explicit File(std::string data) : data_(std::make_shared<std::string>(std::move(data))) {}

  explicit operator bool() const { return open_; }
  bool isDirectory() const { return false; }
  size_t size() const { return data_->size(); }
  void close() { open_ = false; }

  bool seek(size_t position) {
    if (position > data_->size()) return false;
    position_ = position;
    return true;
  }

  size_t read(uint8_t *out, size_t size) {
    ++readCount_;
    const size_t count = std::min(size, data_->size() - position_);
    std::memcpy(out, data_->data() + position_, count);
    position_ += count;
    return count;
  }

  void print(char value) { data_->push_back(value); }
  void print(const char *value) {
    if (value != nullptr) *data_ += value;
  }
  void print(const String &value) { *data_ += value.c_str(); }

  void println() { data_->push_back('\n'); }
  void println(const char *value) {
    print(value);
    println();
  }
  void println(const String &value) {
    print(value);
    println();
  }

  const std::string &contents() const { return *data_; }
  size_t readCount() const { return readCount_; }

 private:
  std::shared_ptr<std::string> data_;
  size_t position_ = 0;
  size_t readCount_ = 0;
  bool open_ = true;
};

namespace fs {
    class FS {};
}
