#ifndef FILEORAM_REMOTE_STORE_ASYNC_SERVER_H_
#define FILEORAM_REMOTE_STORE_ASYNC_SERVER_H_

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <shared_mutex>
#include <string>

#include <grpc/grpc.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/server_context.h>

#include "internal/lock_manager.h"
#include "internal/store.h"
#include "remote_store/common.h"
#include "remote_store.grpc.pb.h"
#include "utils/crypto.h"
#include "utils/grpc.h"

namespace file_oram::storage {

class AsyncCallbackRemoteStoreImpl final : public RemoteStore::CallbackService, public internal::StoreManagerCommon {
 public:
  explicit AsyncCallbackRemoteStoreImpl(std::filesystem::path p, bool more_logs = false)
      : StoreManagerCommon(std::move(p), more_logs) {}

  grpc::ServerUnaryReactor *Initialize(
      grpc::CallbackServerContext *context,
      const file_oram::storage::InitializeRequest *request,
      file_oram::storage::InitializeResponse *response) override;

  grpc::ServerUnaryReactor *Destroy(grpc::CallbackServerContext *context,
                                    const google::protobuf::Empty *request,
                                    google::protobuf::Empty *response) override;

  grpc::ServerWriteReactor<EntryPart> *ReadMany(
      grpc::CallbackServerContext *context,
      const file_oram::storage::ReadManyRequest *request) override;

  grpc::ServerReadReactor<EntryPart> *WriteMany(
      grpc::CallbackServerContext *context,
      google::protobuf::Empty *response) override;
};

inline auto RunLocalAsyncServer = RunLocalServerOf<AsyncCallbackRemoteStoreImpl>;

} // file_oram::storage

#endif //FILEORAM_REMOTE_STORE_ASYNC_SERVER_H_
