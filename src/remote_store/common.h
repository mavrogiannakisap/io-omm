#ifndef FILEORAM_REMOTE_STORE_COMMON_H_
#define FILEORAM_REMOTE_STORE_COMMON_H_

#include <regex>

#include <grpc/grpc.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>

#include "internal/lock_manager.h"
#include "internal/mmap_file_store.h"
#include "internal/ram_store.h"
#include "internal/posix_single_file_store.h"
#include "internal/store.h"
#include "remote_store.grpc.pb.h"
#include "utils/crypto.h"
#include "utils/grpc.h"

namespace file_oram::storage {

static const auto kRamStore =
    InitializeRequest_StoreType::InitializeRequest_StoreType_MMAP_RAM;
static const auto kFileStore =
    InitializeRequest_StoreType::InitializeRequest_StoreType_POSIX_SINGLE_FILE;
using Clock = std::chrono::high_resolution_clock; // name clock exists in time.h

const size_t kMaxEntryPartSize = 1UL << 30;
const static std::string kIdKey = "id";

namespace internal {
static std::vector<std::string> kByteSuffixes =
    {"B", "KiB", "MiB", "GiB", "TiB", "PiB"};

inline std::string HumanReadableBytes(size_t s) {
  long double res = s;
  int i = 0;
  while ((res > 1024.0L) && (i < kByteSuffixes.size() - 1)) {
    res /= 1024.0L;
    ++i;
  }

  std::string tmp = std::to_string(res);
  return tmp.substr(0, tmp.find('.') + 2) + kByteSuffixes[i];
}

inline bool MkdirAndCopy(const std::filesystem::path &src,
                         const std::filesystem::path &dst) {

  std::error_code ec;
  std::filesystem::create_directories(dst.parent_path(), ec);
  if (ec) {
    fprintf(
        stderr,
        "Failed to create parent path of copy dst (%s), Error code: %d - %s\n",
        dst.parent_path().c_str(), ec.value(), ec.message().c_str());
    return false;
  }
  auto ok = std::filesystem::copy_file(
      src, dst,
      std::filesystem::copy_options::overwrite_existing, ec);
  if (!ok || ec) {
    fprintf(stderr, "Failed to copy from (%s) to (%s), Error code: %d - %s\n",
            src.c_str(), dst.c_str(), ec.value(), ec.message().c_str());
    return false;
  }

  return true;
}

class SingleStore {
 public:
  size_t n_ = 0;
  size_t entry_size_ = 0;
  InitializeRequest_StoreType store_type_;
  std::string name_;
  bool first_build_ = false;
  std::unique_ptr<Store> store_;
  LockManager lock_manager_;
};

class StoreManagerCommon {
 public:
  explicit StoreManagerCommon(std::filesystem::path p, bool more_logs = false)
      : base_path_(std::move(p)), more_logs_(more_logs) {}

  grpc::Status Initialize(grpc::ServerContextBase *context,
                          const InitializeRequest *request,
                          InitializeResponse *response) {
    if (request->n() == 0 || request->entry_size() == 0) {
      return {grpc::StatusCode::INVALID_ARGUMENT,
              "n or entry_size too small."};
    }

    uint32_t store_id;
    SingleStore *store;
    size_t stores_size;
    {
      std::unique_lock stores_lock(stores_mux_);
      store_id = NextStoreId();
      store = &stores_[store_id];
      stores_size = stores_.size();
    }
    if (more_logs_) {
      std::clog << "Initializing with id=" << store_id
                << ", n=" << request->n()
                << ", entry_size=" << HumanReadableBytes(request->entry_size())
                << ", total store size="
                << HumanReadableBytes(request->n() * request->entry_size())
                << ", store_type=" << request->store_type()
                << ", first_build=" << int(request->first_build())
                << ", name=" << request->name()
                << "; num_stores=" << stores_size
                << std::endl;
    }

    context->AddInitialMetadata(kIdKey, std::to_string(store_id));
    store->n_ = request->n();
    store->entry_size_ = request->entry_size();
    store->store_type_ = request->store_type();
    store->name_ = request->name();
    store->first_build_ = request->first_build();

    std::string store_type_name = "posix_single_file";
    if (request->store_type() == InitializeRequest_StoreType_MMAP_FILE)
      store_type_name = "mmap_file";
    std::filesystem::path path = base_path_ / store_type_name / std::to_string(store_id);

    // If requested, prebuild or copy prebuilt
    if ((request->store_type() == InitializeRequest_StoreType_POSIX_SINGLE_FILE
        || request->store_type() == InitializeRequest_StoreType_MMAP_FILE)
        && !request->name().empty()) {
      // Many benchmarks happened in a directory structure like this:
      // some-parent-path
      // ├── cache
      // └── nocache
      //
      // To enable testing with always fsyncing and automatic OS caches disabled.
      // To reduce file redundancy, try to create the prebuilts in elsewhere.

      // remove trailing slashes
      auto base = std::filesystem::path(
          std::regex_replace(base_path_.string(), std::regex("/*$"), ""));
      auto base_parent = base.parent_path();
      if (base_parent.string().find("cache") == std::string::npos
          && base_path_.string().find("cache") != std::string::npos) {
        base = base_parent;
      }
      auto prebuilt_path = base / "prebuilt" / request->name();
      auto check_res = CheckFile(
          prebuilt_path, request->n() * request->entry_size(), false);
      if (request->first_build()) {
        path = prebuilt_path;
      }
      response->set_found_prebuilt(false);
      if (check_res.success && check_res.had_same_size_) {
        std::clog << "Found prebuilt store state" << std::endl;
        std::error_code ec;
        if (path != prebuilt_path) {
          auto ok = MkdirAndCopy(prebuilt_path, path);
          if (ok) {
            response->set_found_prebuilt(true);
            if (more_logs_) {
              fprintf(stderr, "Copied prebuilt store state\n");
            }
          } else {
            fprintf(stderr, "Could not use prebuilt store state\n");
          }
        }
      } else {
        fprintf(stderr, "No prebuild state found\n");
      }

    }

    switch (request->store_type()) {
      case InitializeRequest_StoreType_POSIX_SINGLE_FILE: {
        auto os = PosixSingleFileStore::Construct(
            request->n(), request->entry_size(), path, false);
        if (!os.has_value()) {
          std::clog << "Failed to use Posix Single File storage backend."
                    << std::endl;
          goto default_case;
        }
        if (more_logs_) {
          std::clog << "Using Posix Single File storage backend." << std::endl;
        }
        store->store_ = std::unique_ptr<PosixSingleFileStore>(os.value());
        break;
      }
      case InitializeRequest_StoreType_MMAP_FILE: {
        auto os = MMapFileStore::Construct(
            request->n(), request->entry_size(), path, false);
        if (!os.has_value()) {
          std::clog << "Failed to use MMap File storage backend."
                    << std::endl;
          goto default_case;
        }
        if (more_logs_) {
          std::clog << "Using MMap File storage backend." << std::endl;
        }
        store->store_ = std::unique_ptr<MMapFileStore>(os.value());
        break;
      }
      default_case:
      default:
      case InitializeRequest_StoreType_RAM: {
        if (more_logs_) {
          std::clog << "Using RAM storage backend." << std::endl;
        }
        store->store_ = std::make_unique<RamStore>(
            request->n(), request->entry_size());
        break;
      }
    }
    return grpc::Status::OK;
  }

  grpc::Status Destroy(grpc::ServerContextBase *context,
                       const google::protobuf::Empty *request,
                       google::protobuf::Empty *response) {
    if (more_logs_) {
      std::clog << "Store destroy invoked." << std::endl;
    }
    const std::multimap<grpc::string_ref, grpc::string_ref> &metadata =
        context->client_metadata();
    uint32_t store_id = 0; // default
    auto it = metadata.find(kIdKey);
    if (it != metadata.end()) {
      store_id = std::stoul(std::string(it->second.begin(), it->second.end()));
    }
    if (more_logs_) {
      std::clog << "Store id to destroy: " << store_id << std::endl;
    }

    bool found;
    size_t stores_size;
    {
      std::unique_lock stores_lock(stores_mux_);
      found = stores_.find(store_id) != stores_.end();
      if (found)
        stores_.erase(store_id);
      stores_size = stores_.size();
    }

    if (!found) {
      return {grpc::StatusCode::FAILED_PRECONDITION,
              "Store (id=" + std::to_string(store_id) + ") not initialized."};
    }
    if (more_logs_) {
      std::clog << "Store with id=" << store_id << " destroyed; "
                << "Remaining stores: " << stores_size << std::endl;
    }
    return grpc::Status::OK;
  }

  std::map<uint32_t, SingleStore> stores_;
  mutable std::shared_mutex stores_mux_;
  uint32_t next_store_id_ = 0;
  uint32_t NextStoreId() {
    auto res = next_store_id_++;
    while (stores_.find(res) != stores_.end()) res = next_store_id_++;
    return res;
  }
  std::filesystem::path base_path_;
  const bool more_logs_ = false;
};

} // namespace internal


template<class T>
inline auto RunLocalServerOf(const std::string &path) {
  const std::string server_addr = "unix:///tmp/"
      + std::to_string(Clock::now().time_since_epoch().count())
      + ".sock";
  auto server = utils::MakeServer(
      server_addr, {new T(path, true)});
  auto ek = utils::DumbKey();
  auto cargs = grpc::ChannelArguments();
  cargs.SetMaxReceiveMessageSize(INT_MAX);
  cargs.SetMaxSendMessageSize(INT_MAX);
  auto chan = grpc::CreateCustomChannel(
      server_addr, grpc::InsecureChannelCredentials(), cargs);

  return std::pair(std::move(server), std::move(chan));
}

} // namespace file_oram::storage

#endif //FILEORAM_REMOTE_STORE_COMMON_H_
