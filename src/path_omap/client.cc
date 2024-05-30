#include "path_omap.h"

#include <cstddef>

#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>

const auto data_st =
    file_oram::storage::InitializeRequest_StoreType_MMAP_FILE;
const auto aux_st =
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
  constexpr static size_t val_len = 4;
  auto enc_key = file_oram::utils::GenerateKey();
  auto opt_omap =
      file_oram::path_omap::OMap::Construct(8, val_len, enc_key, channel,
                                            data_st, aux_st, false);
  if (!opt_omap.has_value()) {
    std::clog << "Failed to create OMap!" << std::endl;
    return 1;
  }
  std::clog << "OMap Created." << std::endl;
  auto &omap = opt_omap.value();

  for (file_oram::path_omap::Key k = 0; k < 4; ++k) {
    auto v = std::make_unique<char[]>(val_len);
    for (int j = 0; j < val_len; ++j) v[j] = static_cast<char>(k);
    omap.Insert(k, std::move(v));
  }
  omap.EvictAll();

  for (file_oram::path_omap::Key k = 0; k < 4; ++k) {
    auto ov = omap.Read(k);
    if (!ov.has_value()) {
      std::clog << "no value for key " << k << std::endl;
      continue;
    }
    auto v = ov.value();
    std::clog << "Got val ";
    for (int j = 0; j < val_len; ++j) std::clog << (int) v[j] << " ";
    std::clog << "for key " << k << std::endl;
  }
  omap.EvictAll();

  for (file_oram::path_omap::Key k = 0; k < 4; ++k) {
    auto ov = omap.ReadAndRemove(k);
    if (!ov.has_value()) {
      std::clog << "no value for key " << k << std::endl;
      continue;
    }
    auto v = ov.value();
    std::clog << "Got val ";
    for (int j = 0; j < val_len; ++j) std::clog << (int) v[j] << " ";
    std::clog << "for key " << k << std::endl;
    omap.Insert(k, v);
  }
  omap.EvictAll();

  for (file_oram::path_omap::Key k = 0; k < 4; ++k) {
    auto v = std::make_unique<char[]>(val_len);
    for (int j = 0; j < val_len; ++j) v[j] = static_cast<char>(k);
    omap.Insert(k, std::move(v));
  }
  omap.EvictAll();

  for (file_oram::path_omap::Key k = 0; k < 4; ++k) {
    auto ov = omap.Read(k);
    if (!ov.has_value()) {
      std::clog << "no value for key " << k << std::endl;
      continue;
    }
    auto v = ov.value();
    std::clog << "Got val ";
    for (int j = 0; j < val_len; ++j) std::clog << (int) v[j] << " ";
    std::clog << "for key " << k << std::endl;
  }
  omap.EvictAll();

  for (file_oram::path_omap::Key k = 0; k < 4; ++k) {
    auto ov = omap.ReadAndRemove(k);
    if (!ov.has_value()) {
      std::clog << "no value for key " << k << std::endl;
      continue;
    }
    auto v = ov.value();
    std::clog << "Got val ";
    for (int j = 0; j < val_len; ++j) std::clog << (int) v[j] << " ";
    std::clog << "for key " << k << std::endl;
    omap.Insert(k, v);
  }
  omap.EvictAll();

  omap.Destroy();
}