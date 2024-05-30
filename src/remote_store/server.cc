#include "server.h"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <utility>

#include <grpcpp/server_context.h>

#include "remote_store.grpc.pb.h"
#include "utils/grpc.h"

//TODO: Better storage interface given chunked data tx.

namespace file_oram::storage {

using namespace internal;

//const static uint32_t kCheckContextCancelledPeriod = 20;

grpc::Status RemoteStoreImpl::Initialize(grpc::ServerContext *context,
                                         const InitializeRequest *request,
                                         InitializeResponse *response) {
  return StoreManagerCommon::Initialize(context, request, response);
}

grpc::Status RemoteStoreImpl::Destroy(grpc::ServerContext *context,
                                      const google::protobuf::Empty *request,
                                      google::protobuf::Empty *response) {
  return StoreManagerCommon::Destroy(context, request, response);
}

grpc::Status RemoteStoreImpl::ReadMany(grpc::ServerContext *context,
                                       const ReadManyRequest *request,
                                       grpc::ServerWriter<EntryPart> *writer) {
  const std::multimap<grpc::string_ref, grpc::string_ref> &metadata =
      context->client_metadata();
  uint32_t store_id = 0; // default
  auto it = metadata.find(kIdKey);
  if (it != metadata.end()) {
    store_id = std::stoul(std::string(it->second.begin(), it->second.end()));
  }

  SingleStore *ss;
  {
    std::shared_lock stores_lock(stores_mux_);
    ss = stores_.find(store_id) != stores_.end() ? &stores_[store_id] : nullptr;
  }
  if (!ss) {
    return {grpc::StatusCode::FAILED_PRECONDITION,
            "Store (id=" + std::to_string(store_id) + ") not initialized."};
  }

  bool stream_write_success = true;
//  uint32_t cnt = 0;
  EntryPart p;
  for (const auto &index : request->indexes()) {
    if (index >= ss->n_) {
      return {grpc::StatusCode::OUT_OF_RANGE, "Out of range."};
    }

    if (!stream_write_success) {
      break;
    }
//    if (cnt++ % kCheckContextCancelledPeriod == 0) {
//      // Because this call is expensive:
//      if (context->IsCancelled()) {
//        break;
//      }
//    }
    std::string data;

    {
      auto l = ss->lock_manager_.AcquireLock(index);
      data = ss->store_->Read(index);
    }

    if (data.empty()) {
      return {grpc::StatusCode::INTERNAL,
              "Read from internal store backend failed."};
    }

    size_t offset = 0;
    while (offset < data.size()) {
      size_t to_send = kMaxEntryPartSize;
      if (to_send > data.size() - offset)
        to_send = data.size() - offset;

      p.set_index(index);
      p.set_offset(offset);
      p.set_data(data.substr(offset, to_send));
      stream_write_success &= writer->Write(p);

      offset += to_send;
      p.Clear();
    }
  }

  if (context->IsCancelled() || !stream_write_success) {
    return grpc::Status::CANCELLED;
  }
  return grpc::Status::OK;
}

grpc::Status RemoteStoreImpl::WriteMany(grpc::ServerContext *context,
                                        grpc::ServerReader<EntryPart> *reader,
                                        google::protobuf::Empty *response) {
  const std::multimap<grpc::string_ref, grpc::string_ref> &metadata =
      context->client_metadata();
  uint32_t store_id = 0; // default
  auto it = metadata.find(kIdKey);
  if (it != metadata.end()) {
    store_id = std::stoul(std::string(it->second.begin(), it->second.end()));
  }

  SingleStore *ss;
  {
    std::shared_lock stores_lock(stores_mux_);
    ss = stores_.find(store_id) != stores_.end() ? &stores_[store_id] : nullptr;
  }
  if (!ss) {
    return {grpc::StatusCode::FAILED_PRECONDITION,
            "Store (id=" + std::to_string(store_id) + ") not initialized."};
  }

  EntryPart p;
  uint64_t last_index;
  bool writes_started = false;
  std::string data;
  size_t data_offset = 0;
  while (reader->Read(&p)) {
    if (p.index() >= ss->n_)
      return {grpc::StatusCode::OUT_OF_RANGE, "Out of range."};
    if (p.data().size() > kMaxEntryPartSize)
      return {grpc::StatusCode::INVALID_ARGUMENT, "Part too long"};

    if (ss->entry_size_ <= kMaxEntryPartSize) {
      if (p.data().size() != ss->entry_size_)
        return {grpc::StatusCode::INVALID_ARGUMENT, "Bad entry size (short)"};

      bool write_success;
      {
        auto l = ss->lock_manager_.AcquireLock(p.index());
        write_success = ss->store_->Write(p.index(), p.data());
      }
      if (!write_success)
        return {grpc::StatusCode::INTERNAL,
                "Write to internal store backend failed."};
      continue;
    }

    if (!writes_started) {
      writes_started = true;
      data = std::string(ss->entry_size_, '\0');
    } else if (p.index() != last_index && ss->entry_size_ > kMaxEntryPartSize) {
      if (data_offset != ss->entry_size_)
        return {grpc::StatusCode::INVALID_ARGUMENT, "Bad entry size (long)"};
      bool write_success;
      {
        auto l = ss->lock_manager_.AcquireLock(last_index);
        write_success = ss->store_->Write(last_index, data);
      }
      if (!write_success)
        return {grpc::StatusCode::INTERNAL,
                "Write to internal store backend failed."};

      data_offset = 0;
    }
    last_index = p.index();

    if (p.offset() != data_offset)
      return {grpc::StatusCode::INVALID_ARGUMENT, "Wrong offset."};
    if (data_offset + p.data().size() > ss->entry_size_)
      return {grpc::StatusCode::INVALID_ARGUMENT, "Entry too large."};
    std::copy(p.data().begin(), p.data().end(), data.data() + data_offset);
    data_offset += p.data().size();
  }

  if (data_offset) {
    if (data_offset != ss->entry_size_)
      return {grpc::StatusCode::INVALID_ARGUMENT, "Bad entry size (long)"};
    bool write_success;
    {
      auto l = ss->lock_manager_.AcquireLock(last_index);
      write_success = ss->store_->Write(last_index, data);
    }
    if (!write_success)
      return {grpc::StatusCode::INTERNAL,
              "Write to internal store backend failed."};
  }

  return grpc::Status::OK;
}
} // namespace file_oram::storage
