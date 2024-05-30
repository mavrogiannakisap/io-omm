#ifndef FILEORAM_REMOTE_STORE_INTERNAL_STORE_H_
#define FILEORAM_REMOTE_STORE_INTERNAL_STORE_H_

#include <cstddef>
#include <string>

namespace file_oram::storage::internal {

class Store {
 public:
  virtual ~Store() = default;
  virtual std::string Read(size_t i) = 0;
  virtual bool Write(size_t i, const std::string &data) = 0;
};

} // namespace file_oram::storage::internal

#endif //FILEORAM_REMOTE_STORE_INTERNAL_STORE_H_
