#ifndef FILEORAM_REMOTE_STORE_INTERNAL_RAM_STORE_H_
#define FILEORAM_REMOTE_STORE_INTERNAL_RAM_STORE_H_

#include "store.h"

#include <cstddef>
#include <memory>
#include <string>

#include "utils/trace.h"

namespace file_oram::storage::internal {

class RamStore : public Store {
 public:
  RamStore(size_t n, size_t entry_size)
      : n_(n), entry_size_(entry_size), data_(new char[n * entry_size]) {}

  std::string Read(size_t i) {
    if (i >= n_)
      return {};
    auto begin = data_.get() + (i * entry_size_);
    auto end = begin + entry_size_;
    return {begin, end};
  }

  bool Write(size_t i, const std::string &data) {
    if ((i >= n_) || (data.size() != entry_size_))
      return false;
    std::copy_n(data.begin(), entry_size_, data_.get() + (i * entry_size_));
    return true;
  }

 protected:
  size_t n_;
  size_t entry_size_;
  std::unique_ptr<char[]> data_;
};

} // namespace file_oram::storage::internal

#endif //FILEORAM_REMOTE_STORE_INTERNAL_RAM_STORE_H_
