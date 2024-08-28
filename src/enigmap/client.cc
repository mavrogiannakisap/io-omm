#include "enigmap/enigmap.h"

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

  const std::string server_addr =
      "unix:///tmp/" + std::to_string(klock::now().time_since_epoch().count()) +
      ".sock";
  auto server =
      MakeServer(server_addr, {new RemoteStoreImpl(c.store_path_, true)});

  auto data_store = kRamStore;
  auto aux_store = kRamStore;

  auto ek = DumbKey();
  auto cargs = grpc::ChannelArguments();
  cargs.SetMaxReceiveMessageSize(INT_MAX);
  cargs.SetMaxSendMessageSize(INT_MAX);
  auto chan = grpc::CreateCustomChannel(
      server_addr, grpc::InsecureChannelCredentials(), cargs);

  size_t cap = c.capacity_;
  size_t val_len = c.base_block_size_;
  auto opt_em =
      file_oram::path_em::EM::Construct(cap, val_len, ek, chan, data_store);
  if (!opt_em.has_value()) {
    std::clog << "Failed to create EM!" << std::endl;
    return 1;
  }

  auto em = std::move(opt_em.value());
  std::vector<std::pair<file_oram::path_osm::Key, file_oram::path_osm::Val>>
      arr_;
  for (uint64_t k = 1; k <= size_t(cap); k++) {
    std::cout << k << std::endl;
    // for (uint32_t i = 1; i <= k; ++i) {
    auto v = std::make_unique<char[]>(c.base_block_size_);
    *v.get() = char(1);
    arr_.push_back({1, std::move(v)});
    // }
  }
  em.Initialization(arr_);
  em.EvictAll();

  for (auto el : arr_) {
    // for (file_oram::path_osm::Key k = n; k >= 1; k >>= 1) {
    auto k = el.first;
    auto vals = em.ReadAll(k);
    auto len = vals.size();
    std::clog << "k=" << k << std::endl;
    std::clog << "len=" << len << std::endl;
    std::clog << "vals.size()=" << vals.size() << std::endl;
    // assert(vals.size() == len);
    for (uint32_t i = 1; i <= len; ++i) {
      // for (uint32_t i = len; i >= 1; --i) {
      auto &v = vals[i - 1];
      std::cout << i << ": " << (int)*v.get() << std::endl
                << "-------" << std::endl;
      assert((int)*v.get() == (int)*el.second.get());
    }
    // my_assert(vals.size() == len);
  }
  em.EvictAll();

  std::cout << "Success!" << std::endl;
}
