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
#include "remote_store/server.h"
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
  Measurement total{"ofs-update", c};
  auto start = klock::now();

  const std::string server_addr = "unix:///tmp/"
      + std::to_string(klock::now().time_since_epoch().count())
      + ".sock";
  std::clog << "Making server at: " << server_addr << std::endl;
  auto server = MakeServer(server_addr, {new RemoteStoreImpl(c.store_path_, true)});
  std::clog << "Made server" << std::endl;
  auto data_store = c.is_data_mem_ ? kRamStore : kFileStore;
  auto aux_store = c.is_aux_mem_ ? kRamStore : kFileStore;
  auto ek = DumbKey();
  auto cargs = grpc::ChannelArguments();
  cargs.SetMaxReceiveMessageSize(INT_MAX);
  cargs.SetMaxSendMessageSize(INT_MAX);
  std::clog << "Making channel" << std::endl;
  auto chan = grpc::CreateCustomChannel(
      server_addr, grpc::InsecureChannelCredentials(), cargs);
  std::clog << "Made channel" << std::endl;

  for (uint8_t r = 1; r <= c.num_runs_; ++r) {
    std::clog << "Starting run " << r << " of " << c.num_runs_ << std::endl;
    Measurement run;
    size_t lf = c.locality_factor_;
    std::clog << "Creating OFileStore with lf=" << lf << ", s=" << int(c.num_levels_) << std::endl;
   auto opt_ofs = OFileStore::SConstruct(
        c.capacity_, c.num_levels_, lf, c.base_block_size_,
        ek, chan, data_store, aux_store, true, true, c.storage_type_, c.num_runs_, c.initial_level_);
   if (!opt_ofs.has_value()) {
      std::clog << "Benchmark OFS-Update failed" << std::endl;
      std::clog << "Config: " << c << std::endl;
      server->Shutdown();
      return 1;
    }
    auto &ofs = opt_ofs.value();
    auto setup_took = run.Took();
    std::clog << "OFileStore created with lf=" << lf << ", s=" << int(c.num_levels_) << ", levels=[";
    for (auto &l : ofs.levels_) {
      std::clog << int(l) << " ";
    }
    std::clog << "]" << std::endl;
    std::clog << "Setup done, took " << setup_took << std::endl;
    run.numbers_[{"setup", 0}] = setup_took;
    run.numbers_[{"setup_bytes", 0}] = double(ofs.BytesMoved());

    auto prev_bytes = ofs.BytesMoved();
    size_t inserted = 0;
      if(c.capacity_ == 1 << 22) {
        for (const auto &k : InsertValueSizes()) {
          run.Took();
          auto v = Val(k * c.base_block_size_); // lists of size k
          my_assert(v.l_ == c.base_block_size_ * k);
          ofs.Append(k, std::move(v));
          ofs.EvictAll();
          my_assert(ofs.BytesMoved() >= prev_bytes);
          auto bytes = ofs.BytesMoved() - prev_bytes;
          inserted += k;
          run.numbers_[{"insert", k}] = run.Took();
          run.numbers_[{"insert_bytes", k}] = double(bytes);
          std::clog << "Inserted " << k << " (" << inserted << ") took " << run.numbers_[{"insert", k}] << std::endl;
          run.numbers_[{"vl", k}] = double(k);
          prev_bytes = ofs.BytesMoved();
        }
      }
      else {
        for (const auto &k : AppendValueSizes()) {
          run.Took();
          auto append_val = Val(k * c.base_block_size_);
          my_assert(append_val.l_ == c.base_block_size_ * k);
          ofs.Append(k, std::move(append_val));
          my_assert(ofs.BytesMoved() >= prev_bytes);
          auto bytes = ofs.BytesMoved() - prev_bytes;
          run.numbers_[{"insert", k}] = run.Took();
          run.numbers_[{"insert_bytes", k}] = double(bytes);
          std::clog << "Inserted to " << k << std::endl;
          prev_bytes = ofs.BytesMoved();
        }
        ofs.EvictAll();
      }

    prev_bytes = ofs.BytesMoved();
    for (const auto &k : AppendValueSizes()) {
      if (k > c.capacity_) {
        break;
      }
      run.Took();
      auto append_val = Val(c.base_block_size_);
      ofs.Append(k, std::move(append_val));
      my_assert(ofs.BytesMoved() >= prev_bytes);
      auto bytes = ofs.BytesMoved() - prev_bytes;
      run.numbers_[{"append", k}] = run.Took();
      run.numbers_[{"append_bytes", k}] = double(bytes);
      std::clog << "Appended to " << k << std::endl;
      prev_bytes = ofs.BytesMoved();
    }

    total += run;
    std::clog << "Done w/ experiments, destroying..." << std::endl;
    ofs.Destroy();
  }
  total /= c.num_runs_;

  std::cout << total;
  std::chrono::duration<double> total_time = klock::now() - start;
  std::clog << "took=" << total_time.count() << ", config=" << c << std::endl;

  server->Shutdown();
  return 0;
}
