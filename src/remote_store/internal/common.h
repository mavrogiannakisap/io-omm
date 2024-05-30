#ifndef FILEORAM_REMOTE_STORE_INTERNAL_COMMON_H_
#define FILEORAM_REMOTE_STORE_INTERNAL_COMMON_H_

#include <fcntl.h>
#include <unistd.h>

#include <csignal>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "utils/trace.h"

namespace file_oram::storage::internal {

static bool EndsWith(std::string const &value, std::string const &ending) {
  if (ending.size() > value.size()) return false;
  return std::equal(ending.rbegin(), ending.rend(), value.rbegin());
}

namespace internal {
const int kFileFlags = O_CREAT | O_RDWR;
};

const int kFilePerms = 0600;

class FileCheckRes {
 public:
  int file_open_flags_ = internal::kFileFlags;
  std::filesystem::path weakly_canonical_path_;
  bool always_flush_ = true;
  bool success = false;
  bool had_same_size_ = false;

  FileCheckRes(int f, std::filesystem::path p, bool s)
      : file_open_flags_(f), weakly_canonical_path_(p), success(s) {}
  FileCheckRes() = default;
};

static inline FileCheckRes CheckFile(
    const std::filesystem::path &p, size_t total_size, bool truncate) {

  FileCheckRes res;
  std::error_code ec;
  auto wcp = std::filesystem::weakly_canonical(p, ec);
  if (ec) {
    std::clog << "Failed to make path [" << p << "] "
              << "weakly canonical."
              << " Error code: " << ec.value() << " - " << ec.message()
              << std::endl;
    return res;
  }
  res.weakly_canonical_path_ = wcp;

  bool exists = std::filesystem::exists(wcp, ec);
  if (ec) {
    std::clog << "Failed to check whether path [" << wcp << "] "
              << "exists."
              << " Error code: " << ec.value() << " - " << ec.message()
              << std::endl;
    return res;
  }
  if (exists) {
    bool is_reg_file = std::filesystem::is_regular_file(wcp, ec);
    if (ec) {
      std::clog << "Failed to check whether path [" << wcp << "] "
                << "is a regular file."
                << " Error code: " << ec.value() << " - " << ec.message()
                << std::endl;
      return res;
    }
    if (!is_reg_file) {
      std::clog << "Requested path exists and isn't a regular file."
                << std::endl;
      return res;
    }
  }
  std::filesystem::create_directories(wcp.parent_path(), ec);
  if (ec) {
    std::clog << "Failed to create parent path [" << wcp.parent_path()
              << "]."
              << " Error code: " << ec.value() << " - " << ec.message()
              << std::endl;
    return res;
  }

  if (exists) {
    size_t curr_size = std::filesystem::file_size(wcp, ec);
    if (ec) {
      std::clog << "Failed to check the size of [" << wcp << "]."
                << " Error code: " << ec.value() << " - " << ec.message()
                << std::endl;
      return res;
    }
    if (truncate || curr_size != total_size) {
      res.file_open_flags_ |= O_TRUNC;
    }
    res.had_same_size_ = curr_size == total_size;
  }

  // if (wcp.string().find("nocache") != std::string::npos) {
  res.always_flush_ = true;
  // }

  res.success = true;

  return res;
}

static void FlushCaches() {
#if defined(__linux__)
 if (::getuid()) {
   ::printf("Not root, flush failed.\n");
   ::exit(1);
 }
 int fd = ::open("/proc/sys/vm/drop_caches", O_WRONLY);
 ::write(fd, "3", 1);
 ::close(fd);
#elif defined(__APPLE__)
 if (::getuid()) {
   ::printf("Not root, flush failed.\n");
   ::exit(1);
 }
 ::system("purge");
#else
  ::printf("Could not flush; not linux or mac!\n");
  ::exit(1);
#endif
}

} // namespace file_oram::storage::internal

#endif //FILEORAM_REMOTE_STORE_INTERNAL_COMMON_H_
