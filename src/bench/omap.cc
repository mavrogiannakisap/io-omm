#include <chrono>
#include <climits>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <grpc/grpc.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>

#include "path_omap/path_omap.h"
#include "remote_store/common.h"
#include "remote_store/async_server.h"
#include "remote_store/server.h"
#include "utils/assert.h"
#include "utils/bench.h"
#include "utils/crypto.h"
#include "utils/grpc.h"
#include "utils/csv.h"

using namespace file_oram;
using namespace file_oram::path_omap;
using namespace file_oram::storage;
using namespace file_oram::utils;

const double kOpRepeats = 100.0;

int main(int argc, char **argv) {
  Config c(argc, argv);
  Measurement total{"omap", c};
  auto start = klock::now();
  total.numbers_[{"load_data", 0}] = 0; // To keep the column

  // The actual benchmark:
  const std::string server_addr = "unix:///tmp/"
      + std::to_string(klock::now().time_since_epoch().count())
      + ".sock";
  auto server = MakeServer(
      server_addr, {new RemoteStoreImpl(c.store_path_, true)});
//      server_addr, {new AsyncCallbackRemoteStoreImpl(c.store_path_, true)});
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
    auto opt_omap = OMap::Construct(c.capacity_, c.base_block_size_, ek, chan,
                                    data_store, aux_store, true); // data_store = {RAM, HDD, SSD}
    if (!opt_omap.has_value()) {
      std::clog << "Benchmark PathOMap failed" << std::endl;
      std::clog << "Config: " << c << std::endl;
      server->Shutdown();
    }
    auto &omap = opt_omap.value();
    omap.FillWithDummies();
    run.numbers_[{"setup", 0}] = kOpRepeats * run.Took();
    run.numbers_[{"setup_bytes", 0}] = kOpRepeats * double(omap.BytesMoved());

    auto prev_bytes = omap.BytesMoved();
    double insert = 0;
    double insert_bytes = 0;
    for (int i = 0; i < kOpRepeats; ++i) {
      run.Took();
      auto v = std::make_unique<char[]>(c.base_block_size_);
      omap.Insert(i, std::move(v));
      omap.EvictAll();
      my_assert(omap.BytesMoved() >= prev_bytes);
      auto bytes = omap.BytesMoved() - prev_bytes;
      insert += run.Took();
      insert_bytes += double(bytes);
      prev_bytes = omap.BytesMoved();
    }
    run.numbers_[{"vl", 1}] = double(1);
    run.numbers_[{"insert", 1}] += insert / kOpRepeats;
    run.numbers_[{"insert_bytes", 1}] += insert_bytes / kOpRepeats;
    std::clog << "Inserted (" << kOpRepeats << ")" << std::endl;

    prev_bytes = omap.BytesMoved();
    size_t searched = 0;
    for (uint64_t k = 1; k < size_t(c.capacity_); k <<= 1) {
      run.Took();
      omap.DummyOp(false);
      omap.DummyOp(true, 2 * k);
      searched += k;
      my_assert(omap.BytesMoved() >= prev_bytes);
      auto bytes = omap.BytesMoved() - prev_bytes;
      auto transferred = bytes / c.base_block_size_;
      auto res_size = 1;
      run.numbers_[{"vl", k}] = double(k);
      run.numbers_[{"search", k}] += run.Took();
      run.numbers_[{"search_bytes", k}] += double(bytes);
      run.numbers_[{"search_false_pos", k}] += double(transferred - res_size);
      std::clog << "Searched " << k << " (" << searched << ")" << std::endl;
      prev_bytes = omap.BytesMoved();
    }

    total += run;
    if (r != c.num_runs_)
      omap.Destroy(); // Last one gets cleaned anyway.
  }
  total /= c.num_runs_;

  std::cout << total;
  std::chrono::duration<double> total_time = klock::now() - start;
  std::clog << "took=" << total_time.count() << ", config=" << c << std::endl;

//  server->Shutdown();
  return 0;
}