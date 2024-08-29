#include "path_oram.h"

#include <cstddef>

#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>

#include "utils/assert.h"

const auto data_st = file_oram::storage::InitializeRequest_StoreType_MMAP_FILE;
const auto aux_st = file_oram::storage::InitializeRequest_StoreType_MMAP_FILE;

int main() {
  auto args = grpc::ChannelArguments();
  args.SetMaxReceiveMessageSize(INT_MAX);
  args.SetMaxSendMessageSize(INT_MAX);
  auto channel = grpc::CreateCustomChannel(
      "localhost:50052", grpc::InsecureChannelCredentials(), args);
  const size_t val_len = 1UL << 10;
  const size_t n = 8;
  auto enc_key = file_oram::utils::GenerateKey();
  auto opt_oram = file_oram::path_oram::ORam::Construct(
      n, val_len, enc_key, channel, data_st, aux_st, false);
  if (!opt_oram.has_value()) {
    std::clog << "Failed to create ORAM!" << std::endl;
    return 1;
  }
  std::clog << "ORAM created." << std::endl;
  auto &oram = opt_oram.value();

  std::map<file_oram::path_oram::Key, file_oram::path_oram::Pos> pos_map;
  for (file_oram::path_oram::Key k = 0; k < n; ++k) {
    auto to_fetch = oram->GeneratePos();
    std::clog << "Fetching pos " << to_fetch << std::endl;
    oram->FetchPath(to_fetch);
    std::clog << "Adding key " << k << " to stash" << std::endl;
    auto v = std::make_unique<char[]>(val_len);
    for (int j = 0; j < val_len; ++j)
      v[j] = static_cast<char>(k);
    auto p = oram->AddToStash(k, std::move(v));
    std::clog << "Got pos " << p << " for key " << k << std::endl;
    pos_map[k] = p;
  }
  oram->EvictAll();

  for (file_oram::path_oram::Key k = 0; k < n; ++k) {
    auto p = pos_map[k];
    oram->FetchPath(p);
    auto ov = oram->ReadAndRemoveFromStash(k);
    my_assert(ov.has_value());
    auto v = std::move(ov.value());
    for (int j = 0; j < val_len; ++j)
      my_assert(v[j] == static_cast<char>(k));

    p = oram->AddToStash(k, std::move(v));
    std::clog << "Got pos " << p << " for key " << k << std::endl;
    pos_map[k] = p;
  }
  oram->EvictAll();

  for (auto p = oram->min_pos_; p <= oram->max_pos_; ++p) {
    oram->FetchPath(p);
  }
  for (file_oram::path_oram::Key k = 0; k < n; ++k) {
    auto ov = oram->ReadAndRemoveFromStash(k);
    my_assert(ov.has_value());
    auto v = std::move(ov.value());
    for (int j = 0; j < val_len; ++j)
      my_assert(v[j] == static_cast<char>(k));

    auto p = oram->AddToStash(k, std::move(v));
    std::clog << "Got pos " << p << " for key " << k << std::endl;
    pos_map[k] = p;
  }
  oram->EvictAll();

  oram->Destroy();

  return 0;
}
