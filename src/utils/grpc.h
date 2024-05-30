#ifndef FILEORAM_UTILS_GRPC_H_
#define FILEORAM_UTILS_GRPC_H_

#include <climits>
#include <cstdarg>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>

#include "rlimit.h"

namespace file_oram::utils {

inline std::unique_ptr<grpc::Server> MakeServer(
    const std::string &server_address,
    const std::vector<grpc::Service *> &services) {

  if (!SetNoFile()) {
    return nullptr;
  }
  grpc::ServerBuilder builder;
  builder.SetMaxReceiveMessageSize(INT_MAX);
  builder.SetMaxSendMessageSize(INT_MAX);
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  for (auto s : services) {
    builder.RegisterService(s);
  }
  std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
  return std::move(server);
}

inline std::unique_ptr<grpc::Server> MakeServer(
    const std::vector<grpc::Service *> &services) {
  return std::move(MakeServer("0.0.0.0:50052", services));
}
} // namespace file_oram::utils

#endif //FILEORAM_UTILS_GRPC_H_
