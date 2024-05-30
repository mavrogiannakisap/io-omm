#ifndef FILEORAM_REMOTE_STORE_INTERNAL_POSIX_SINGLE_FILE_STORE_H_
#define FILEORAM_REMOTE_STORE_INTERNAL_POSIX_SINGLE_FILE_STORE_H_

#include "store.h"

#include <fcntl.h>
#include <unistd.h>

#include <csignal>
#include <cstddef>
#include <iostream>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "remote_store/internal/common.h"
#include "utils/trace.h"

namespace file_oram::storage::internal {

constexpr size_t kReadBuffSize = 1UL << 30;
constexpr size_t kWriteBuffSize = 1UL << 30;

class PosixSingleFileStore : public Store {
 public:

  void SetFlushCache(bool flush_) {
    always_flush_ = flush_;
  }

  static std::optional<PosixSingleFileStore *> Construct(
      size_t n, size_t entry_size,
      const std::filesystem::path &p, bool truncate = false) {
    auto res = new PosixSingleFileStore(n, entry_size, p, truncate);
    if (!res->setup_successful_) return std::nullopt;
    return res;
  }

  std::string Read(size_t i) override {
    std::string res(entry_size_, '\0');
    size_t read_bytes = 0;
    while (read_bytes < entry_size_) {
      size_t to_read = kReadBuffSize;
      if (read_bytes + to_read > entry_size_) {
        to_read = entry_size_ - read_bytes;
      }
      size_t file_offset = (i * entry_size_) + read_bytes;
      auto read_res = ::pread(
          file_, res.data() + read_bytes, to_read, file_offset);
      if (read_res == -1) {
        std::clog << "Couldn't read file; fd=" << file_ << ", path=" << path_
                  << ", prev offset=" << read_bytes
                  << ", tried to read " << to_read << " more"
                  << "; errno=" << errno << std::endl;
        return {};
      }
      read_bytes += to_read;
    }
    if (always_flush_)
      Uncache();
    return res;
  }

  bool Write(size_t i, const std::string &data) override {
    size_t written_bytes = 0;
    while (written_bytes < entry_size_) {
      size_t to_write = kWriteBuffSize;
      if (written_bytes + to_write > entry_size_) {
        to_write = entry_size_ - written_bytes;
      }
      size_t file_offset = (i * entry_size_) + written_bytes;
      auto write_res = ::pwrite(
          file_, data.data() + written_bytes, to_write, file_offset);
      if (write_res == -1) {
        std::clog << "Couldn't write to file; fd=" << file_
                  << ", path=" << path_
                  << ", prev offset=" << written_bytes
                  << ", tried to write " << to_write << " more"
                  << "; errno=" << errno << std::endl;
        return false;
      }
      written_bytes += to_write;
    }
    if (always_flush_)
      Uncache();
    return true;
  }

  ~PosixSingleFileStore() override {
    ::close(file_);
  }

 private:
  PosixSingleFileStore(size_t n, size_t entry_size,
                       const std::filesystem::path &p, bool truncate = false)
      : n_(n), entry_size_(entry_size) {

    auto fcs = CheckFile(p, n * entry_size, truncate);
    if (!fcs.success) {
      return;
    }

    path_ = fcs.weakly_canonical_path_;
    always_flush_ = fcs.always_flush_;

    file_ = ::open(path_.c_str(), fcs.file_open_flags_, kFilePerms);
    if (file_ == -1) {
      std::clog << "Couldn't open file; errno=" << errno << std::endl;
      return;
    }
    setup_successful_ = true;
  }

  static void Uncache() {
    sync();
    FlushCaches();
  }

  size_t n_;
  size_t entry_size_;
  std::filesystem::path path_;
  int file_;
  bool setup_successful_ = false;
  bool always_flush_ = true;
  size_t TotalSize() { return n_ * entry_size_; }
};

} // namespace file_oram::storage::internal

#endif //FILEORAM_REMOTE_STORE_INTERNAL_POSIX_SINGLE_FILE_STORE_H_
