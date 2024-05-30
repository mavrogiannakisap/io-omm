#include <csignal>
#include <iostream>
#include <memory>

#include <grpc/grpc.h>

#include "async_server.h"
#include "server.h"
#include "utils/grpc.h"

namespace {
volatile std::sig_atomic_t sig = 0;
std::unique_ptr<grpc::Server> server;

static const std::string kFileStoresBasePath = "/tmp/ofs-files/";
}

void signal_handler(int s) {
  sig = s;
  std::clog << "Signal received; " << strsignal(s) << std::endl;
  server->Shutdown(std::chrono::system_clock::now() + std::chrono::seconds(1));
}

int main() {
  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);

  server = file_oram::utils::MakeServer(
      {new file_oram::storage::RemoteStoreImpl(kFileStoresBasePath, true)});
//      {new file_oram::storage::AsyncCallbackRemoteStoreImpl(kFileStoresBasePath, true)});
  std::clog << "Server started." << std::endl;
  server->Wait();
  std::clog << "Server shut down; exiting." << std::endl;
  return 0;
}
