#pragma once
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <iomanip>
#include <sstream>
#include <string>

using std::isfinite;
template <typename T>
T constrain(T value, T low, T high) {
  return std::min(high, std::max(low, value));
}

class String {
 public:
  String() = default;
  String(const char* value) : value_(value) {}
  String(const std::string& value) : value_(value) {}
  String(int value) : value_(std::to_string(value)) {}
  String(unsigned long value) : value_(std::to_string(value)) {}
  String(float value, int decimals) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(decimals) << value;
    value_ = out.str();
  }
  size_t length() const { return value_.size(); }
  const char* c_str() const { return value_.c_str(); }
  char operator[](size_t index) const { return value_[index]; }
  void reserve(size_t size) { value_.reserve(size); }
  String& operator+=(const String& other) {
    value_ += other.value_;
    return *this;
  }
  String& operator+=(char other) {
    value_ += other;
    return *this;
  }
  friend String operator+(String left, const String& right) { return left += right; }
  void replace(const char* from, const char* to) {
    size_t index = 0;
    while ((index = value_.find(from, index)) != std::string::npos) {
      value_.replace(index, std::string(from).size(), to);
      index += std::string(to).size();
    }
  }
  void toUpperCase() {
    for (char& c : value_) c = static_cast<char>(std::toupper(c));
  }

 private:
  std::string value_;
};

struct SerialStub {
  std::string output;
  void print(const char* text) { output += text; }
};
extern SerialStub Serial;
