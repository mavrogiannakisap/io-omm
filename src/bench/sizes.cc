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

#include "o_file_store/o_file_store.h"
#include "remote_store/server.h"
#include "utils/assert.h"
#include "utils/bench.h"
#include "utils/crypto.h"
#include "utils/grpc.h"
#include "utils/csv.h"

using namespace file_oram;
using namespace file_oram::o_file_store;
using namespace file_oram::path_omap;
using namespace file_oram::storage;
using namespace file_oram::utils;

//auto kCapacity = 1ULL << 23;
auto kMaxValLen = 300;
auto min_s = 1;
auto max_s = 24;
auto min_ln = 1;
auto max_ln = 23;
auto lf_list = std::vector<size_t>{1, 8, 16, 32};

int main(int argc, char **argv) {
  const std::string server_addr = "unix:///tmp/"
      + std::to_string(klock::now().time_since_epoch().count())
      + ".sock";
  auto server = MakeServer(server_addr, {new RemoteStoreImpl("", true)});
  auto data_store = kRamStore;
  auto aux_store = kRamStore;
  auto ek = DumbKey();
  auto cargs = grpc::ChannelArguments();
  cargs.SetMaxReceiveMessageSize(INT_MAX);
  cargs.SetMaxSendMessageSize(INT_MAX);
  auto chan = grpc::CreateCustomChannel(
      server_addr, grpc::InsecureChannelCredentials(), cargs);

  std::cout << "test,n,s,lf,total_size" << std::endl;

  for (auto ln = min_ln; ln <= max_ln; ++ln) {
    size_t n = 1ULL << ln;
    for (auto s = min_s; s <= max_s; ++s) {
      for (auto &lf_loop : lf_list) {
        auto lf = lf_loop;
        auto opt_ofs = OFileStore::Construct(
            n, s, lf, kMaxValLen,
            ek, chan, data_store, aux_store);
        my_assert(opt_ofs.has_value());
        std::cout << "ofs," << n << "," << int(s) << "," << lf << "," << opt_ofs->TotalSizeOfStore() << std::endl;
        opt_ofs->Destroy();
      }

      auto opt_omap = OMap::Construct(1ULL << s, kMaxValLen, ek, chan,
                                      data_store, aux_store, false);
      my_assert(opt_omap.has_value());
      std::cout << "omap," << n << "," << 0 << "," << 0 << "," << opt_omap->TotalSizeOfStore() << std::endl;
      opt_omap->Destroy();
    }
  }
}