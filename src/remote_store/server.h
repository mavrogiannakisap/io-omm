#ifndef FILEORAM_REMOTE_STORE_SERVER_H_
#define FILEORAM_REMOTE_STORE_SERVER_H_

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <shared_mutex>
#include <string>
#include <utility>

#include <grpc/grpc.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/server_context.h>

#include "common.h"
#include "internal/lock_manager.h"
#include "internal/store.h"
#include "remote_store.grpc.pb.h"
#include "utils/crypto.h"
#include "utils/grpc.h"

namespace file_oram::storage {

class RemoteStoreImpl final : public RemoteStore::Service, public internal::StoreManagerCommon {
 public:
  explicit RemoteStoreImpl(std::filesystem::path p, bool more_logs = false)
      : StoreManagerCommon(std::move(p), more_logs) {}

  grpc::Status Initialize(grpc::ServerContext *context,
                          const InitializeRequest *request,
                          InitializeResponse *response) override;

  grpc::Status Destroy(grpc::ServerContext *context,
                       const google::protobuf::Empty *request,
                       google::protobuf::Empty *response) override;

  grpc::Status ReadMany(grpc::ServerContext *context,
                        const ReadManyRequest *request,
                        grpc::ServerWriter<EntryPart> *writer) override;

  grpc::Status WriteMany(grpc::ServerContext *context,
                         grpc::ServerReader<EntryPart> *reader,
                         google::protobuf::Empty *response) override;
};

inline auto RunLocalServer = RunLocalServerOf<RemoteStoreImpl>;

}; // namespace file_oram::storage

#endif //FILEORAM_REMOTE_STORE_SERVER_H_
