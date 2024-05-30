#include <chrono>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "o_file_store/o_file_store.h"
#include "remote_store/server.h"
#include "utils/assert.h"
#include "utils/bench.h"

using namespace file_oram;
using namespace file_oram::o_file_store;
using namespace file_oram::path_omap;
using namespace file_oram::path_oram;
using namespace file_oram::path_oram;
using namespace file_oram::storage;
using namespace file_oram::utils;

using Clock = std::chrono::high_resolution_clock; // name clock exists in time.h
using Duration = std::chrono::duration<double>;
using TimePoint = std::chrono::time_point<Clock, Duration>;

bool Build(size_t n, size_t s, size_t lf, size_t vl, const std::string &base_path);

int main(int argc, char **argv) {
  Config c(argc, argv);
  std::clog << "Config: " << c << std::endl;
  TimePoint start = Clock::now();

  size_t n = c.capacity_;
  for (auto [s, lf] : BenchSizes()) {
    auto built = Build(n, s, lf, c.base_block_size_, c.store_path_);
    my_assert(built);
    Duration took = Clock::now() - start;
    std::clog << "So far: " << took.count() << std::endl;
  }
  Duration took = Clock::now() - start;
  std::clog << "All done; took: " << took.count() << std::endl;
  return 0;
}

bool Build(size_t n, size_t s, size_t lf, size_t vl, const std::string &base_path) {
  TimePoint run_start = Clock::now();
  auto [server, chan] = storage::RunLocalServer(base_path);
  std::clog << "(Prebuild) creating OFileStore with lf=" << lf << ", s=" << s << std::endl;
  auto opt_ofs = OFileStore::Construct(
      n, s, lf, vl, DumbKey(), chan,
      kFileStore, kFileStore, true, true);
  if (!opt_ofs.has_value()) {
    std::clog << "Store prebuild failed" << std::endl;
    server->Shutdown();
    return false;
  }
  auto &ofs = opt_ofs.value();
  ofs.Destroy();
  server->Shutdown();
  Duration took = Clock::now() - run_start;
  std::clog << "Took: " << took.count() << std::endl;
  return true;
}

