#ifndef FILEORAM_UTILS_NAMEGEN_H_
#define FILEORAM_UTILS_NAMEGEN_H_

#include <initializer_list>
#include <map>
#include <regex>
#include <string>

namespace file_oram::utils {
static inline std::string GenName(const std::initializer_list<std::string> kvs) {
  if (kvs.size() == 0 || (kvs.size() % 2) == 1) {
    return "";
  }

  std::string res;
  bool key = true;
  for (const std::string &kv : kvs) {
    res += key ? kv : (kv + "-");
    key = !key; // keyval-keyval-
  }
  res = std::regex_replace(res, std::regex("-+"), "-");
  res.resize(res.size() - 1);
  return res;
}
} // namespace file_oram::utils

#endif //FILEORAM_UTILS_NAMEGEN_H_
