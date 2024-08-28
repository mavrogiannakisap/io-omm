#include <chrono>
#include <climits>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

#include <grpc/grpc.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>

#include "path_osm/path_osm.h"
#include "remote_store/server.h"
#include "utils/assert.h"
#include "utils/bench.h"
#include "utils/crypto.h"
#include "utils/grpc.h"
#include "utils/namegen.h"

using namespace file_oram::utils;
using namespace file_oram::path_osm;
using namespace file_oram::storage;

int main(int argc, char **argv) {
  Config c(argc, argv);
  Measurement total{"osm", c};
  auto start = klock::now();

  const std::string server_addr = "unix:///tmp/"
      + std::to_string(klock::now().time_since_epoch().count())
      + ".sock";
  auto server = MakeServer(server_addr, {new RemoteStoreImpl(c.store_path_, true)});

  auto data_store = c.is_data_mem_ ? kRamStore : kFileStore;
  auto aux_store = c.is_aux_mem_ ? kRamStore : kFileStore;

  auto ek = DumbKey();
  auto cargs = grpc::ChannelArguments();
  cargs.SetMaxReceiveMessageSize(INT_MAX);
  cargs.SetMaxSendMessageSize(INT_MAX);
  auto chan = grpc::CreateCustomChannel(
      server_addr, grpc::InsecureChannelCredentials(), cargs);

  for (uint8_t r = 1; r <= c.num_runs_; ++r) {
    std::clog << "Starting run " << r << " of " << c.num_runs_ << std::endl;
    Measurement run;
    std::clog << "Constructing OSM" << std::endl;
    auto opt_osm =
      OSM::Construct(c.capacity_, c.base_block_size_, ek, chan, data_store);
    if (!opt_osm.has_value()) {
      std::clog << "Benchmark OSM failed" << std::endl;
      std::clog << "Config: " << c << std::endl;
      server->Shutdown();
      return 1;
    }
    auto &osm = opt_osm.value();
    auto setup_took = run.Took();
    std::clog << "Setup done, took " << setup_took << std::endl;
    run.numbers_[{"setup", 0}] = setup_took;
    run.numbers_[{"setup_bytes", 0}] = double(osm.BytesMoved());

    auto prev_bytes = osm.BytesMoved();
    size_t inserted = 0;
    for (uint64_t k = 1; k < size_t(c.capacity_); k <<= 1) {
      run.Took();
      //1024
      for (uint32_t i = 1; i <= k; ++i) {
        //128
        auto v = std::make_unique<char[]>(c.base_block_size_);
        *v.get() = char(k);
        osm.Insert(k, std::move(v));
      }
      my_assert(osm.BytesMoved() >= prev_bytes);
      auto bytes = osm.BytesMoved() - prev_bytes;
      inserted += k;
      run.numbers_[{"insert", k}] = run.Took();
      run.numbers_[{"insert_bytes", k}] = double(bytes);
      std::clog << "Inserted " << k << " ( total: " << inserted << ")" << "time: " << run.numbers_[{"insert", k}] << std::endl;
      run.numbers_[{"vl", k}] = double(k);
      prev_bytes = osm.BytesMoved();
    }
    osm.prebuild_phase_ = false;
    std::clog << "Evicting.." << std::endl;
    osm.EvictAll();
    osm.ReadAll(1); // One dummy access
    osm.EvictAll();
    prev_bytes = osm.BytesMoved();
    size_t searched = 0;
    for (uint64_t k = 1; k < size_t(c.capacity_)/4; k<<=1) {

      // std::clog << "Searching for " << k << std::endl;
      run.Took();
      auto vals = osm.ReadAll(k);
      // my_assert(vals.has_value());
      // my_assert(osm.BytesMoved() >= prev_bytes);
      auto bytes = osm.BytesMoved() - prev_bytes;
      auto transferred = bytes / c.base_block_size_;
      auto res_size = k;  
      searched += res_size;
      osm.EvictAll();
      // std::clog << "\tRead: " << read << "\n\tEviction: " << evict << std::endl;

      run.numbers_[{"search", k}] = run.Took();
      run.numbers_[{"search_bytes", k}] = double(bytes);
      run.numbers_[{"search_false_pos", k}] = double(transferred - res_size);
      std::clog << "Searched " << k << " (" << searched << ")" << "time: " << run.numbers_[{"search", k}] << std::endl;
      // my_assert((run.numbers_[{"vl", k}] == double(res_size)));
      prev_bytes = osm.BytesMoved();
    }

    total += run;
    if (r != c.num_runs_)
      osm.Destroy(); // Last one gets cleaned anyway.
  }

  total /= c.num_runs_;

  std::cout << total;
  std::chrono::duration<double> total_time = klock::now() - start;
  std::clog << "took=" << total_time.count() << ", config=" << c << std::endl;

  server->Shutdown();
  return 0;
}
