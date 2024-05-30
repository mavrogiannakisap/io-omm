#include "internal/posix_single_file_store.h"

#include <string>

#include "utils/assert.h"

namespace {
const int n = 1UL << 10;
const int es = 1UL << 20;
const std::string p = "/tmp/store-test/0";
} // namespace

int main() {
  auto os = file_oram::storage::internal::PosixSingleFileStore::Construct(
      n, es, "/tmp/store-test/0", true);
  if (!os) {
    std::clog << "Couldn't make store." << std::endl;
    return 1;
  }
  auto s = os.value();

  for (int i = 0; i < n; ++i) {
    char data[es];
    for (char &j : data) j = static_cast<char>(i);
    auto w = s->Write(i, {&data[0], &data[0] + es});
    if (!w) {
      std::clog << "Failed to write, i=" << i << std::endl;
    }
  }

  for (int i = 0; i < n; ++i) {
    auto data = s->Read(i);
    for (int j = 0; j < es; ++j) {
      char c = data[j];
      my_assert(c == static_cast<char>(i));
    }
    for (char &j : data) my_assert(j == static_cast<char>(i));
    if (data.empty()) {
      std::clog << "Failed to read, i=" << i << std::endl;
    }
  }

  return 0;
}
