#include <chrono>
#include <climits>
#include <cmath>
#include <iostream>
#include <map>
#include <memory>
#include <string>

#include <grpc/grpc.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>

#include "o_file_store/o_file_store.h"
#include "path_oram/path_oram.h"
#include "remote_store/common.h"
#include "remote_store/server.h"
#include "utils/assert.h"
#include "utils/bytes.h"
#include "utils/crypto.h"
#include "utils/grpc.h"

using namespace file_oram;
using namespace file_oram::storage;

using file_oram::path_omap::OMap;
using OMapVal = file_oram::path_omap::Val;
using file_oram::path_oram::Key;
using file_oram::path_oram::ORam;
using file_oram::path_oram::Pos;
using file_oram::storage::RemoteStoreImpl;
using file_oram::utils::GenerateKey;
using file_oram::utils::MakeServer;

using std::chrono::seconds;
using std::chrono::system_clock;
using std::chrono::steady_clock;

int RunTests(ORam &, OMap &);

size_t block_size = 12;
size_t n = 6;

int main(int argc, char *argv[]) {
  const std::string server_addr = "unix:///tmp/"
      + std::to_string(system_clock::now().time_since_epoch().count())
      + ".sock";
  std::string base_path = "/tmp/ofilestore_allinone/";
  auto ram_st = kRamStore;
  auto map_st = kRamStore;
  if (argc >= 2) {
    n = std::stoul(argv[1]);
    std::clog << "Using n=2^" << n << std::endl;
  } else {
    std::clog << "Using default n=2^" << n << std::endl;
  }
  if (argc >= 3) {
    block_size = std::stoul(argv[2]);
    std::clog << "Using block_size=2^" << block_size << std::endl;
  } else {
    std::clog << "Using default block_size=2^" << block_size << std::endl;
  }
  if (argc >= 4) {
    std::string ram_st_str = argv[3];
    if (ram_st_str == "file")
      ram_st = kFileStore;
  }
  if (argc >= 5) {
    std::string map_st_str = argv[4];
    if (map_st_str == "file")
      map_st = kFileStore;
  }
  if (argc >= 6) {
    base_path = argv[5];
    std::clog << "Using base path: " << base_path << std::endl;
  } else {
    std::clog << "Using default base path: " << base_path << std::endl;
  }

  auto server = MakeServer(server_addr, {new RemoteStoreImpl(base_path)});
  std::clog << "Server started, running client." << std::endl;

  auto args = grpc::ChannelArguments();
  args.SetMaxReceiveMessageSize(INT_MAX);
  args.SetMaxSendMessageSize(INT_MAX);
  auto channel = grpc::CreateCustomChannel(
      server_addr, grpc::InsecureChannelCredentials(), args);
  auto enc_key = GenerateKey();
  auto opt_oram = ORam::Construct(
      1UL << n, 1UL << block_size, enc_key, channel, ram_st, ram_st, false);
  if (!opt_oram.has_value()) {
    std::clog << "test=pathoram, n=" << n << ", bs=" << block_size
              << ": Failed to create ORam client!" << std::endl;
    server->Shutdown();
    return 1;
  }
  std::clog << "ORam client Created." << std::endl;
  auto &oram = opt_oram.value();

  auto opt_omap =
      OMap::Construct(1UL << n, sizeof(Pos), enc_key, channel, map_st, map_st, false);
  if (!opt_omap.has_value()) {
    std::clog << "test=pathoram, n=" << n << ", bs=" << block_size
              << ": Failed to create OMap client!" << std::endl;
    server->Shutdown();
    return 1;
  }
  std::clog << "OMap client Created." << std::endl;
  auto &omap = opt_omap.value();

  auto start = steady_clock::now();
  auto mul = RunTests(*oram, omap);
  my_assert(mul != 0);
  std::chrono::duration<double> took = mul * (steady_clock::now() - start);
  std::cout << "test,bs,n,ram_st,map_st,time" << std::endl;
  std::cout << "por" << ","
            << block_size << ","
            << n << ","
            << ram_st << ","
            << map_st << ","
            << took.count() << std::endl;

  oram->Destroy();
  server->Shutdown(system_clock::now() + seconds(1));
  server->Wait();
  std::clog << "Server shut down; exiting." << std::endl;
  return 0;
}

int RunTests(ORam &oram, OMap &omap) {
  std::vector<Key> fid_map;
  const uint64_t min_file = n;
  const uint64_t max_file = n;

  Key k = 0;

  size_t len = (1UL << n) << block_size;
  uint64_t fid = n;
  fid_map.push_back(k);
  uint64_t num_parts = len >> block_size;
  for (uint64_t p = 0; p < num_parts; ++p) {
    oram.FetchDummyPath();
    auto part_key = k++;
    auto pos = oram.AddToStash(part_key, nullptr);
    auto pos_b = new char[sizeof(Pos)];
    const auto begin = reinterpret_cast<const char *> (std::addressof(pos));
    const auto end = begin + sizeof(pos);
    std::copy(begin, end, pos_b);
    auto omv = std::shared_ptr<char[]>(pos_b);
    omap.Insert(part_key, std::move(omv));
    oram.EvictAll();
    omap.EvictAll();
    break; // Simulating...
  }
  std::clog << "Wrote file " << n << std::endl;

  auto it = fid_map.begin();
  k = *it;
  ++it;
  for (uint64_t p = 0; p < num_parts; ++p) {
    auto part_key = k + p;
    auto pos_b = omap.ReadAndRemove(part_key);
    my_assert(pos_b.has_value());
    omap.EvictAll();
    Pos pos;
    file_oram::utils::FromBytes(pos_b->get(), pos);
    oram.FetchPath(pos);
    auto ov = oram.ReadAndRemoveFromStash(part_key);
    my_assert(ov.has_value());
    oram.EvictAll();
    break; // Simulating...
  }
  std::clog << "Read file " << n << std::endl;

  return num_parts;
}
