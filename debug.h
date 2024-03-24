#pragma once

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>

class FormatSize {
public:
  explicit FormatSize(std::uintmax_t s) : m_value(s) {}
  friend std::ostream &operator<<(std::ostream &os, FormatSize f);

private:
  std::uintmax_t m_value;
};

std::ostream &operator<<(std::ostream &os, FormatSize f) {
  int unit = 0;
  float head = (float)f.m_value;
  while (head >= 1024) {
    ++unit;
    head /= 1024;
  }
  head = std::ceil(head * 10.0f) / 10.0f;
  os << head;

  if (unit) { os << "KMGTPE"[unit - 1] << 'i'; }

  return os << 'B';
}

void PrintFSType(const std::filesystem::directory_entry &_f) {
  namespace fs = std::filesystem;
  std::cout << _f.path().filename();
  switch (_f.symlink_status().type()) {
  case fs::file_type::none: std::cout << " has `not-evaluated-yet` type"; break;
  case fs::file_type::not_found: std::cout << " does not exist"; break;
  case fs::file_type::regular: std::cout << " is a regular file"; break;
  case fs::file_type::directory: std::cout << " is a directory"; break;
  case fs::file_type::symlink: std::cout << " is a symlink"; break;
  case fs::file_type::block: std::cout << " is a block device"; break;
  case fs::file_type::character: std::cout << " is a character device"; break;
  case fs::file_type::fifo: std::cout << " is a named IPC pipe"; break;
  case fs::file_type::socket: std::cout << " is a named IPC socket"; break;
  case fs::file_type::unknown: std::cout << " has `unknown` type"; break;
  default: std::cout << " has `implementation-defined` type"; break;
  }
  std::cout << '\n';
}
