#include "async_server.h"

#include <utility>

#define min(a, b) ((a)<(b)?(a):(b))

namespace file_oram::storage {

using namespace internal;

grpc::ServerUnaryReactor *AsyncCallbackRemoteStoreImpl::Initialize(
    grpc::CallbackServerContext *context,
    const file_oram::storage::InitializeRequest *request,
    file_oram::storage::InitializeResponse *response) {
  auto s = StoreManagerCommon::Initialize(context, request, response);
  auto *reactor = context->DefaultReactor();
  reactor->Finish(s);
  return reactor;
}

grpc::ServerUnaryReactor *AsyncCallbackRemoteStoreImpl::Destroy(
    grpc::CallbackServerContext *context,
    const google::protobuf::Empty *request,
    google::protobuf::Empty *response) {
  auto s = StoreManagerCommon::Destroy(context, request, response);
  auto *reactor = context->DefaultReactor();
  reactor->Finish(s);
  return reactor;
}

grpc::ServerWriteReactor<EntryPart> *AsyncCallbackRemoteStoreImpl::ReadMany(
    grpc::CallbackServerContext *context,
    const file_oram::storage::ReadManyRequest *request) {

  class Reader : public grpc::ServerWriteReactor<EntryPart> {
   public:
    explicit Reader(SingleStore *ss, std::vector<size_t> to_read)
        : ss_(ss), to_read_(std::move(to_read)) {
      it_ = to_read_.cbegin();
      PrepWrite();
      NextWrite();
    }

    void OnDone() override { delete this; }
    void OnWriteDone(bool /*ok*/) override { NextWrite(); }
    void OnCancel() override {}

   private:
    void NextWrite() {
      if (it_ < to_read_.cend()) {
        if (offset_ < ss_->entry_size_) {
          size_t to_send = min(kMaxEntryPartSize, ss_->entry_size_ - offset_);
          ep_.set_index(*it_);
          ep_.set_offset(offset_);
          ep_.set_data(data_.substr(offset_, to_send));
          StartWrite(&ep_);

          offset_ += to_send;
          if (offset_ == ss_->entry_size_) {
            ++it_;
            PrepWrite();
          }
          return;
        }
      }
      Finish(grpc::Status::OK);
    }

    void PrepWrite() {
      if (it_ < to_read_.cend()) {
        const auto &idx = *it_;
        if (idx >= ss_->n_) {
          Finish({grpc::StatusCode::OUT_OF_RANGE, "Out of range."});
          return;
        }
        {
          auto l = ss_->lock_manager_.AcquireLock(idx);
          data_ = ss_->store_->Read(idx);
        }
        if (data_.empty()) {
          Finish({grpc::StatusCode::INTERNAL,
                  "Read from internal store backend failed."});
          return;
        }
        offset_ = 0;
      }
    }

    SingleStore *ss_;
    size_t offset_;
    std::string data_;
    std::vector<size_t> to_read_;
    std::vector<size_t>::const_iterator it_;
    EntryPart ep_;
  };

  class EarlyFail : public grpc::ServerWriteReactor<EntryPart> {
   public:
    EarlyFail(const grpc::Status &s) {
      Finish(s);
    }
    void OnDone() override { delete this; }
  };

  const std::multimap<grpc::string_ref, grpc::string_ref> &metadata =
      context->client_metadata();
  uint32_t store_id = 0; // default
  auto it = metadata.find(kIdKey);
  if (it != metadata.end()) {
    store_id = std::stoul(std::string(it->second.begin(), it->second.end()));
  }
  SingleStore *ss;
  {
    std::shared_lock l{stores_mux_};
    ss = stores_.find(store_id) != stores_.end() ? &stores_[store_id] : nullptr;
  }
  if (!ss) {
    return new EarlyFail(
        {grpc::StatusCode::FAILED_PRECONDITION,
         "Store (id=" + std::to_string(store_id) + ") not initialized."});
  }

  return new Reader(ss, {request->indexes().begin(),
                         request->indexes().end()});
}

grpc::ServerReadReactor<EntryPart> *AsyncCallbackRemoteStoreImpl::WriteMany(
    grpc::CallbackServerContext *context,
    google::protobuf::Empty *response) {
  class Writer : public grpc::ServerReadReactor<EntryPart> {
   public:
    explicit Writer(SingleStore *ss) : ss_(ss) { StartRead(&ep_); }

    void OnDone() override { delete this; }

    void OnReadDone(bool ok) override {
      if (ok) {
        if (ep_.index() >= ss_->n_) {
          Finish({grpc::StatusCode::OUT_OF_RANGE, "Out of range."});
          return;
        }
        if (ep_.data().size() >= kMaxEntryPartSize) {
          Finish({grpc::StatusCode::INVALID_ARGUMENT, "Part too long"});
          return;
        }

        if (ss_->entry_size_ <= kMaxEntryPartSize) {
          DoSmallWrite();
          return;
        }

        if (!writes_started_) {
          writes_started_ = true;
          data_ = std::string(ss_->entry_size_, '\0');
        } else if (ep_.index() != last_idx_) {
          if (!DoWrite())
            return;
        }
        last_idx_ = ep_.index();

        if (ep_.offset() != offset_) {
          Finish({grpc::StatusCode::INVALID_ARGUMENT, "Wrong offset."});
          return;
        }
        if (offset_ + ep_.data().size() > ss_->entry_size_) {
          Finish({grpc::StatusCode::INVALID_ARGUMENT, "Entry too large."});
          return;
        }
        std::copy(ep_.data().begin(), ep_.data().end(), data_.data() + offset_);
        offset_ += ep_.data().size();
        return;
      }

      // error or last write
      if (offset_) {
        if (!DoWrite())
          return;
      }
      Finish(grpc::Status::OK);
    }

   private:
    bool DoWrite() {
      bool write_success;
      if (offset_ != ss_->entry_size_) {
        Finish({grpc::StatusCode::INVALID_ARGUMENT,
                offset_ < ss_->entry_size_
                ? "Bad entry size (too short)"
                : "Bad entry size (too long)"});
        return false;
      }
      {
        auto l = ss_->lock_manager_.AcquireLock(last_idx_);
        write_success = ss_->store_->Write(last_idx_, data_);
      }
      if (write_success) {
        offset_ = 0;
        return true;
      }
      FinishOnFailedWrite();
      return false;
    }

    void DoSmallWrite() {
      bool write_success;
      if (ep_.data().size() != ss_->entry_size_) {
        Finish({grpc::StatusCode::INVALID_ARGUMENT,
                ep_.data().size() < ss_->entry_size_
                ? "Bad entry size (too short)"
                : "Bad entry size (too long)"});
        return;
      }
      {
        auto l = ss_->lock_manager_.AcquireLock(ep_.index());
        write_success = ss_->store_->Write(ep_.index(), ep_.data());
      }
      if (!write_success) {
        FinishOnFailedWrite();
      }
    }

    void FinishOnFailedWrite() {
      Finish({grpc::StatusCode::INTERNAL,
              "Write to internal store backend failed."});
    }

    EntryPart ep_;
    SingleStore *ss_;
    std::string data_;
    size_t last_idx_;
    bool writes_started_ = false;
    size_t offset_ = 0;
  };

  class EarlyFail : public grpc::ServerReadReactor<EntryPart> {
   public:
    explicit EarlyFail(const grpc::Status &s) {
      Finish(s);
    }
    void OnDone() override { delete this; }
  };

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
    return new EarlyFail(
        {grpc::StatusCode::FAILED_PRECONDITION,
         "Store (id=" + std::to_string(store_id) + ") not initialized."});
  }

  return new Writer(ss);
}
} // file_oram::storage