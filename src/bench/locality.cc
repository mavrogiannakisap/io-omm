#include <cstdlib>
#include <iostream>
#include <random>

#include "remote_store/internal/mmap_file_store.h"
#include "remote_store/internal/store.h"
#include "utils/assert.h"

using namespace file_oram::storage;
using namespace file_oram::storage::internal;
using klock = std::chrono::high_resolution_clock; // name clock exists in time.h


int main() {
  const int runs = 1;
  const size_t n = 1ULL << 20;
  const size_t batch_size = 1UL << 10;
  my_assert(batch_size < n);
  const size_t es = 1ULL << 12; // 4KiB; 1 page
  const std::filesystem::path base = "~/Development/ofs-tester_delete-me/nocache/file_name";

  auto count = n;
  auto each = es;
  std::chrono::duration<double> total = std::chrono::seconds(0);
  std::cout << "sequential (" << count << " x " << each << "):" << std::endl;
  std::string s(each, 0x00);
  for (int r = 0; r < runs; ++r) {
    std::clog << "Starting run " << r << " of " << runs << std::endl;
    auto op = MMapFileStore::Construct(count, each, base);
    if (!op.has_value()) return 1;
    auto &p = op.value();

    auto start = klock::now();
    for (size_t i = 0; i < count; ++i) {
      p->Write(i, s);
    }
    auto end = klock::now();
    std::chrono::duration<double> took = end - start;
    total += took;
    std::cout << "- " << took.count() << std::endl;
    delete p;
  }
  std::cout << "* " << total.count() / runs << std::endl;

  count = n / batch_size;
  each = es * batch_size;
  total = std::chrono::seconds(0);
  std::cout << "sequential (" << count << " x " << each << "):" << std::endl;
  s = std::string(each, 0x00);
  for (int r = 0; r < runs; ++r) {
    auto op = MMapFileStore::Construct(count, each, base);
    if (!op.has_value()) return 1;
    auto &p = op.value();

    auto start = klock::now();
    for (size_t i = 0; i < count; ++i) {
      p->Write(i, s);
    }
    auto end = klock::now();
    std::chrono::duration<double> took = end - start;
    total += took;
    std::cout << "- " << took.count() << std::endl;
    delete p;
  }
  std::cout << "* " << total.count() / runs << std::endl;

  count = n;
  each = es;
  total = std::chrono::seconds(0);
  std::mt19937 gen(1);
  std::uniform_int_distribution<size_t> dist(0, count - 1);
  std::cout << "Random (" << count << " x " << each << "):" << std::endl;
  s = std::string(each, 0x00);
  for (int r = 0; r < runs; ++r) {
    auto op = MMapFileStore::Construct(count, each, base);
    if (!op.has_value()) return 1;
    auto &p = op.value();

    auto start = klock::now();
    for (size_t i = 0; i < count; ++i) {
      auto j = dist(gen);
      p->Write(j, s);
    }
    auto end = klock::now();
    std::chrono::duration<double> took = end - start;
    total += took;
    std::cout << "- " << took.count() << std::endl;
    delete p;
  }
  std::cout << "* " << total.count() / runs << std::endl;

  count = n / batch_size;
  each = es * batch_size;
  total = std::chrono::seconds(0);
  gen = std::mt19937(1);
  dist = std::uniform_int_distribution<size_t>(0, count - 1);
  std::cout << "Random (" << count << " x " << each << "):" << std::endl;
  s = std::string(each, 0x00);
  for (int r = 0; r < runs; ++r) {
    auto op = MMapFileStore::Construct(count, each, base);
    if (!op.has_value()) return 1;
    auto &p = op.value();

    auto start = klock::now();
    for (size_t i = 0; i < count; ++i) {
      auto j = dist(gen);
      p->Write(j, s);
    }
    auto end = klock::now();
    std::chrono::duration<double> took = end - start;
    total += took;
    std::cout << "- " << took.count() << std::endl;
    delete p;
  }
  std::cout << "* " << total.count() / runs << std::endl;
}
