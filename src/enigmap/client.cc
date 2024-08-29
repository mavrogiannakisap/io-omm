#include "enigmap.h"

#include <cstddef>
#include <iostream>

#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>

#include "utils/assert.h"

const auto st = file_oram::storage::InitializeRequest_StoreType_MMAP_FILE;
// const auto st =
//    file_oram::storage::InitializeRequest_StoreType_RAM;

int main() {
  auto args = grpc::ChannelArguments();
  args.SetMaxReceiveMessageSize(INT_MAX);
  args.SetMaxSendMessageSize(INT_MAX);
  auto channel = grpc::CreateCustomChannel(
      "localhost:50052", grpc::InsecureChannelCredentials(), args);
  constexpr static size_t cap = 4;
  constexpr static size_t n = cap >> 1;
  constexpr static size_t val_len = 1;
  auto enc_key = file_oram::utils::GenerateKey();
  std::string store_path("/Users/apostolosmavrogiannakis/Documents/ssdmount");
  auto opt_em = file_oram::path_em::EM::Construct(cap, val_len, enc_key,
                                                  channel, st, store_path);
  if (!opt_em.has_value()) {
    std::clog << "Failed to create EM!" << std::endl;
    return 1;
  }

  std::cout << "Success!" << std::endl;
}
