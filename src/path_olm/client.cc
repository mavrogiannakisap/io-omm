
#include <cstddef>
#include <iostream>
#include <chrono>

#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>

#include "path_olm/path_olm.h"
#include "remote_store/server.h"
#include "utils/assert.h"
#include "utils/bench.h"
#include "utils/crypto.h"
#include "utils/grpc.h"
#include "utils/namegen.h"

const auto st =
    file_oram::storage::InitializeRequest_StoreType_MMAP_FILE;
//const auto st =
//    file_oram::storage::InitializeRequest_StoreType_RAM;


using klock = std::chrono::high_resolution_clock;

int main(int argc, char **argv) {
  file_oram::utils::Config c(argc, argv);
  auto start = klock::now();
  const std::string server_addr = "unix:///tmp/"
      + std::to_string(klock::now().time_since_epoch().count())
      + ".sock";
  auto server = file_oram::utils::MakeServer(server_addr, {new file_oram::storage::RemoteStoreImpl(c.store_path_, true)});
  auto args = grpc::ChannelArguments();
  args.SetMaxReceiveMessageSize(INT_MAX);
  args.SetMaxSendMessageSize(INT_MAX);
  auto channel = grpc::CreateCustomChannel(server_addr,
                                           grpc::InsecureChannelCredentials(),
                                           args);
  
  constexpr static size_t cap = 1UL << 10;
  constexpr static size_t n = cap >> 1;
  constexpr static size_t val_len = 4;
  auto enc_key = file_oram::utils::GenerateKey();
  auto opt_olm =
      file_oram::path_olm::OLM::Construct(cap, val_len, enc_key, channel, st);
  if (!opt_olm.has_value()) {
    std::clog << "Failed to create OLM!" << std::endl;
    return 1;
  }
  std::clog << "OLM Created." << std::endl;
  auto &olm = opt_olm.value();  

  for (file_oram::path_olm::Key k = 1; k <= n; k <<= 1) {
  //for (file_oram::path_olm::Key k = n; k >= 1; k >>= 1) {
    auto len = k;
    for (uint32_t i = 1; i <= len; ++i) {
    //for (uint32_t i = len; i >= 1; --i) {
      auto v = std::make_unique<char[]>(val_len);
      *v.get() = char(k + i);
      olm.Insert(k, std::move(v));
    }
  }
  olm.EvictAll();
  std::clog << "Inserted." << std::endl;

  for (file_oram::path_olm::Key k = 1; k <= n; k <<= 1) {
  //for (file_oram::path_olm::Key k = n; k >= 1; k >>= 1) {
    auto len = k;
    auto vals = olm.ReadAll(k);
    //std::clog << "k=" << k << std::endl;
    //std::clog << "len=" << len << std::endl;
    //std::clog << "vals.size()=" << vals.size() << std::endl;
    //for (uint32_t i = 1; i <= len; ++i) {
    ////for (uint32_t i = len; i >= 1; --i) {
    //  auto &v = vals[i - 1];
    //  std::cout << i << ": " << (int) *v.get() << " -- expected: " << (int) char((k + i) % 128) << std::endl;
    //  my_assert(*v.get() == char((k + i) % 128));
    //}
    std::clog << "Reading key " << k << " with len " << len << std::endl; 
    my_assert(vals.size() == len);
  }
  olm.EvictAll();
  std::clog << "ReadAll." << std::endl;

  /*
  for (file_oram::path_olm::Key k = 1; k < n; k <<= 1) {
    auto len = k;
    for (uint32_t i = 1; i <= len; ++i) {
      auto v = olm.ReadAndRemove(k);
      my_assert(v.has_value());
    }
  }
  olm.EvictAll();*/
  //std::clog << "ReadAndRemoved." << std::endl;

  olm.Destroy();
}
