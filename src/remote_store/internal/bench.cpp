#include "mmap_file_store.h"
#include "posix_single_file_store.h"
#include "store.h"

using namespace file_oram::storage;
using namespace file_oram::storage::internal;
using klock = std::chrono::high_resolution_clock; // name clock exists in time.h

std::chrono::duration<double> total;

int main() {
  const int runs = 2;
  const size_t n = 1ULL << 20;
  const size_t es = 1ULL << 12;
  const std::filesystem::path base = "~/ofs-tester_delete-me.dir/";
  const std::filesystem::path pp = base / "psf";
  const std::filesystem::path mp = base / "mf";
  std::string s(es, '\0');

  std::cout << "PosixSingleFileStore:" << std::endl;
  for (int r = 0; r < runs; ++r) {
    auto op = PosixSingleFileStore::Construct(n, es, pp);
    if (!op.has_value()) return 1;
    auto &p = op.value();

    auto start = klock::now();
    for (size_t i = 0; i < n; ++i) {
      std::fill(s.begin(), s.end(), char(i));
      p->Write(i, s);
    }
    auto end = klock::now();
    std::chrono::duration<double> took = end - start;
    if (r == 0) {
      total = took;
    } else {
      total += took;
    }
    std::cout << "- " << took.count() << std::endl;
  }
  std::cout << "* " << total.count() / runs << std::endl;

  std::cout << "MMapFileStore:" << std::endl;
  for (int r = 0; r < runs; ++r) {
    auto op = MMapFileStore::Construct(n, es, mp);
    if (!op.has_value()) return 1;
    auto &p = op.value();

    auto start = klock::now();
    for (size_t i = 0; i < n; ++i) {
      std::fill(s.begin(), s.end(), char(i));
      p->Write(i, s);
    }
    auto end = klock::now();
    std::chrono::duration<double> took = end - start;
    if (r == 0) {
      total = took;
    } else {
      total += took;
    }
    std::cout << "- " << took.count() << std::endl;
  }
  std::cout << "* " << total.count() / runs << std::endl;
}