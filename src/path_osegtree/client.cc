#include "path_osegtree.h"

#include <cstddef>
#include <iostream>

#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>

#include "remote_store/common.h"
#include "utils/assert.h"
#include "utils/crypto.h"

using namespace file_oram;
using namespace file_oram::path_osegtree;
using namespace file_oram::storage;
using namespace file_oram::utils;

int main() {
  auto args = grpc::ChannelArguments();
  args.SetMaxReceiveMessageSize(INT_MAX);
  args.SetMaxSendMessageSize(INT_MAX);
  auto channel = grpc::CreateCustomChannel("localhost:50052",
                                           grpc::InsecureChannelCredentials(),
                                           args);
  auto ek = GenerateKey();
  constexpr static size_t n = 8;
  constexpr static size_t mv = 16;
  auto opt_ost = OSegTree::Construct(
      n, mv, ek, channel, kRamStore, kRamStore, false);
  my_assert(opt_ost);
  std::clog << "OSegTree created." << std::endl;
  auto &ost = opt_ost.value();

  std::map<uint32_t, uint32_t> mirror;
  for (uint32_t i = 0; i < n; ++i) {
    mirror[i] = mv;
  }

  uint32_t size = mv;
  for (uint32_t i = 0; i < n; ++i) {
    auto opt_key = ost.Alloc(size);
    my_assert(opt_key);
    auto k = opt_key.value();
    my_assert(mirror[k] >= size);
    mirror[k] -= size;
  }

  auto k = 3 % n;
  auto l = mv / 2;
  ost.Free(k, l);
  mirror[k] += l;

  l = size / 4;
  auto opt_key = ost.Alloc(l);
  my_assert(opt_key);
  k = opt_key.value();
  my_assert(k = 3 % n);
  my_assert(mirror[k] >= l);
  mirror[k] -= l;

  ost.Destroy();
  return 0;
}