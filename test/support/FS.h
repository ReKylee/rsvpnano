#pragma once

#include <memory>
#include <string>

#include <Arduino.h>

class File {
 public:
  File() : data_(std::make_shared<std::string>()) {}

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

 private:
  std::shared_ptr<std::string> data_;
};
