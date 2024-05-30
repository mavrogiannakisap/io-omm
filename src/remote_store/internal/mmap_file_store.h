#ifndef FILEORAM_REMOTE_STORE_INTERNAL_MMAP_FILE_STORE_H_
#define FILEORAM_REMOTE_STORE_INTERNAL_MMAP_FILE_STORE_H_

#include "store.h"

#include <sys/mman.h>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>

#include "remote_store/internal/common.h"
#include "utils/trace.h"

namespace file_oram::storage::internal {

class MMapFileStore : public Store {
 public:
  static std::optional<MMapFileStore *> Construct(
      size_t n, size_t es,
      const std::filesystem::path &p,
      bool truncate = false) {

    auto res = new MMapFileStore(n, es, p, truncate);
    if (!res->setup_successful_) return std::nullopt;
    return res;
  }

  std::string Read(size_t i) override {
    std::string res(entry_size_, '\0');
    std::copy_n(base_addr_ + (i * entry_size_), entry_size_, res.data());
    if (always_flush_) {
      ::msync(base_addr_ + (i * entry_size_), entry_size_, MS_SYNC | MS_INVALIDATE);
      FlushCaches();
    }
    return res;
  }

  bool Write(size_t i, const std::string &data) override {
    std::copy_n(data.data(), entry_size_, base_addr_ + (i * entry_size_));
    if (always_flush_) {
      ::msync(base_addr_ + (i * entry_size_), entry_size_, MS_SYNC | MS_INVALIDATE);
      FlushCaches();
    }
    return true;
  }

  ~MMapFileStore() override {
    ::msync(base_addr_, TotalSize(), MS_SYNC);
    ::munmap(base_addr_, TotalSize());
  }

 private:
  MMapFileStore(const size_t n, const size_t es,
                const std::filesystem::path &p,
                bool truncate = false)
      : n_(n), entry_size_(es) {

    auto fcs = CheckFile(p, TotalSize(), truncate);
    if (!fcs.success) {
      return;
    }

    path_ = fcs.weakly_canonical_path_;
    always_flush_ = fcs.always_flush_;

    auto fd = ::open(path_.c_str(), fcs.file_open_flags_, kFilePerms);
    if (fd == -1) {
      std::clog << "Couldn't open fd; errno=" << errno << std::endl;
      return;
    }
    if (int ft_res = ftruncate(fd, TotalSize()); ft_res != 0) {
      std::clog << "Couldn't truncate file; errno: " << errno << std::endl;
      return;
    }

    auto mmap_res = mmap(nullptr, TotalSize(),
                         PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mmap_res == MAP_FAILED) {
      std::clog << "Couldn't mmap fd; errno=" << errno << std::endl;
      return;
    }
    ::close(fd);
    base_addr_ = static_cast<char *>(mmap_res);
    setup_successful_ = true;
  }

  const size_t n_;
  const size_t entry_size_;
  std::filesystem::path path_;
  char *base_addr_;
  bool setup_successful_ = false;
  bool always_flush_ = false;
  [[nodiscard]] size_t TotalSize() const { return n_ * entry_size_; }
};
}

#endif //FILEORAM_REMOTE_STORE_INTERNAL_MMAP_FILE_STORE_H_
