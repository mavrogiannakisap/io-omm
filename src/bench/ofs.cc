#include <chrono>
#include <climits>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

#include <grpc/grpc.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>

#include "o_file_store/o_file_store.h"
// #include "remote_store/server.h"
#include "remote_store/async_server.h"
#include "utils/assert.h"
#include "utils/bench.h"
#include "utils/crypto.h"
#include "utils/grpc.h"
#include "utils/namegen.h"

using namespace file_oram;
using namespace file_oram::o_file_store;
using namespace file_oram::storage;
using namespace file_oram::utils;

int main(int argc, char **argv) {
  Config c(argc, argv);
  Measurement total{"ofs", c};
  auto start = klock::now();

  const std::string server_addr = "unix:///tmp/"
      + std::to_string(klock::now().time_since_epoch().count())
      + ".sock";
  auto server = MakeServer(server_addr, {new AsyncCallbackRemoteStoreImpl(c.store_path_, true)});
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
    size_t lf = c.locality_factor_;
    // std::clog << "Creating OFileStore with lf=" << lf << ", s=" << int(c.num_levels_) << std::endl;
    auto opt_ofs = OFileStore::SConstruct(
        c.capacity_, c.num_levels_, lf, c.base_block_size_,
        ek, chan, data_store, aux_store, true, true, c.storage_type_, r, c.initial_level_, c.store_path_);
    if (!opt_ofs.has_value()) {
      std::clog << "Benchmark OMM failed" << std::endl;
      std::clog << "Config: " << c << std::endl;
      server->Shutdown();
      return 1;
    }
    auto &ofs = opt_ofs.value();
    if(!ofs.SetupCheck()) {
      std::clog << "Benchmark OMM failed" << std::endl;
      std::clog << "Config: " << c << std::endl;
      server->Shutdown();
      return 1;
    }
    auto setup_took = run.Took();
    std::clog << "OFileStore created, levels=[";
    for (auto &l : ofs.levels_) {
      std::clog << int(l) << " ";
    }
    std::clog << "]" << std::endl;
    std::clog << "Setup done, took " << setup_took << std::endl;
    run.numbers_[{"setup", 0}] = setup_took;
    run.numbers_[{"setup_bytes", 0}] = double(ofs.BytesMoved());

    auto prev_bytes = ofs.BytesMoved();
    size_t inserted = 0;

    for (uint64_t k = 1; k < size_t(c.capacity_); k <<= 1) {

      run.Took();
      auto v = Val(k * c.base_block_size_); // lists of size k
      my_assert(v.l_ == c.base_block_size_ * k);
      ofs.AppendSingleLevel(k, std::move(v));
      my_assert(ofs.BytesMoved() >= prev_bytes);
      auto bytes = ofs.BytesMoved() - prev_bytes;
      inserted += k;
      run.numbers_[{"insert", k}] = run.Took();
      // run.numbers_[{"insert_bytes", k}] = double(bytes);
      std::clog << "Inserted " << k << " (" << inserted << ") took " << run.numbers_[{"insert", k}] << std::endl;
      run.numbers_[{"vl", k}] = double(k);
      prev_bytes = ofs.BytesMoved();
    }
    std::clog << "Evicting.." << std::endl;
    ofs.BatchEvict();

    prev_bytes = ofs.BytesMoved();
    size_t searched = 0;
    for (uint64_t k = 1; k < size_t(c.capacity_); k <<= 1) {
      run.Took();
      OptVal opt_v;
      ofs.ReadUpdate(k, OFileStore::MakeReader(opt_v));
      ofs.EvictAll();
      // ofs.Search(k, OFileStore::MakeReader(opt_v));
//      ofs.EvictAll();
      my_assert(opt_v.has_value());
      my_assert(ofs.BytesMoved() >= prev_bytes);
      auto bytes = ofs.BytesMoved() - prev_bytes;
      auto transferred = bytes / c.base_block_size_;
      auto res_size = opt_v->l_ / c.base_block_size_;
      my_assert(transferred >= res_size); // Assuming no compression
      searched += res_size;
      run.numbers_[{"search", k}] = run.Took();
      // run.numbers_[{"search_bytes", k}] = double(bytes);
      // run.numbers_[{"search_false_pos", k}] = double(transferred - res_size);
      std::clog << "Searched " << k << " (" << searched << ") took " << run.numbers_[{"search", k}] << std::endl;
      my_assert((run.numbers_[{"vl", k}] == double(res_size)));
      prev_bytes = ofs.BytesMoved();
    }
    total += run;
    if (r != c.num_runs_)
      ofs.Destroy(); // Last one gets cleaned anyway.
  }
  total /= c.num_runs_;

  std::cout << total;
  std::chrono::duration<double> total_time = klock::now() - start;
  std::clog << "took=" << total_time.count() << ", config=" << c << std::endl;

  server->Shutdown();
  return 0;
}
