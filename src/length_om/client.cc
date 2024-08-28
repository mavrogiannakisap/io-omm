#include "length_om.h"

#include <cstddef>

#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>

#include "utils/assert.h"

const auto st =
    file_oram::storage::InitializeRequest_StoreType_MMAP_FILE;
//const auto st =
//    file_oram::storage::InitializeRequest_StoreType_RAM;

int main() {
  auto args = grpc::ChannelArguments();
  args.SetMaxReceiveMessageSize(INT_MAX);
  args.SetMaxSendMessageSize(INT_MAX);
  auto channel = grpc::CreateCustomChannel("localhost:50052",
                                           grpc::InsecureChannelCredentials(),
                                           args);
  constexpr static size_t cap = 1UL << 10;
  constexpr static size_t n = cap >> 1;
  constexpr static size_t val_len = 4;
  auto enc_key = file_oram::utils::GenerateKey();
  auto opt_osm =
      file_oram::path_osm::OSM::Construct(cap, val_len, enc_key, channel, st);
  if (!opt_osm.has_value()) {
    std::clog << "Failed to create OSM!" << std::endl;
    return 1;
  }
  std::clog << "OSM Created." << std::endl;
  auto &osm = opt_osm.value();

  for (file_oram::path_osm::Key k = 1; k <= n; k <<= 1) {
  //for (file_oram::path_osm::Key k = n; k >= 1; k >>= 1) {
    auto len = k;
    for (uint32_t i = 1; i <= len; ++i) {
    //for (uint32_t i = len; i >= 1; --i) {
      auto v = std::make_unique<char[]>(val_len);
      *v.get() = char(k + i);
      osm.Insert(k, std::move(v));
    }
  }
  osm.EvictAll();
  std::clog << "Inserted." << std::endl;

  for (file_oram::path_osm::Key k = 1; k <= n; k <<= 1) {
  //for (file_oram::path_osm::Key k = n; k >= 1; k >>= 1) {
    auto len = k;
    auto vals = osm.ReadAll(k);
    //std::clog << "k=" << k << std::endl;
    //std::clog << "len=" << len << std::endl;
    //std::clog << "vals.size()=" << vals.size() << std::endl;
    //for (uint32_t i = 1; i <= len; ++i) {
    ////for (uint32_t i = len; i >= 1; --i) {
    //  auto &v = vals[i - 1];
    //  std::cout << i << ": " << (int) *v.get() << " -- expected: " << (int) char((k + i) % 128) << std::endl;
    //  my_assert(*v.get() == char((k + i) % 128));
    //}
    my_assert(vals.size() == len);
  }
  osm.EvictAll();
  std::clog << "ReadAll." << std::endl;

  for (file_oram::path_osm::Key k = 1; k < n; k <<= 1) {
    auto len = k;
    for (uint32_t i = 1; i <= len; ++i) {
      auto v = osm.ReadAndRemove(k);
      my_assert(v.has_value());
    }
  }
  osm.EvictAll();
  std::clog << "ReadAndRemoved." << std::endl;

  osm.Destroy();
}
