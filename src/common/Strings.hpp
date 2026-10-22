#pragma once
#include <string>

namespace todo {

  inline std::string trim(const std::string& s) {
    const char* ws = " \t\r\n\f\v";
    const auto b = s.find_first_not_of(ws);
    if (b == std::string::npos) return "";
    return s.substr(b, s.find_last_not_of(ws) - b + 1);
  }

} // namespace todo
