#ifndef FILEORAM_REMOTE_STORE_INTERNAL_MMAP_RAM_STORE_H_
#define FILEORAM_REMOTE_STORE_INTERNAL_MMAP_RAM_STORE_H_

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

class MMapRamStore : public Store {
 public:
  static std::optional<MMapRamStore *> Construct(size_t n, size_t es) {
    auto res = new MMapRamStore(n, es);
    if (!res->setup_successful_) return std::nullopt;
    return res;
  }

  std::string Read(size_t i) override {
    std::string res(entry_size_, '\0');
    std::copy_n(base_addr_ + (i * entry_size_), entry_size_, res.data());
    return res;
  }

  bool Write(size_t i, const std::string &data) override {
    std::copy_n(data.data(), entry_size_, base_addr_ + (i * entry_size_));
    return true;
  }

  ~MMapRamStore() override {
    ::munmap(base_addr_, TotalSize());
  }

 private:
  MMapRamStore(const size_t n, const size_t es)
      : n_(n), entry_size_(es) {

    auto mmap_res = mmap(
        nullptr, TotalSize(), PROT_READ | PROT_WRITE,
        MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (mmap_res == MAP_FAILED) {
      std::clog << "Couldn't mmap (mem)" << errno << std::endl;
      return;
    }
    base_addr_ = static_cast<char *>(mmap_res);
    setup_successful_ = true;
  }

  const size_t n_;
  const size_t entry_size_;
  char *base_addr_;
  bool setup_successful_ = false;
  [[nodiscard]] size_t TotalSize() const { return n_ * entry_size_; }
};
}

#endif //FILEORAM_REMOTE_STORE_INTERNAL_MMAP_RAM_STORE_H_
