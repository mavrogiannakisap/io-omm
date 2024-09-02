#ifndef FILEORAM_PATH_EM_PATH_EM_H_
#define FILEORAM_PATH_EM_PATH_EM_H_

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>

#include <grpcpp/channel.h>
#include <sys/_types/_u_int32_t.h>
#include <utility>

#include "path_oram/path_oram.h"
#include "path_osm/path_osm.h"
#include "utils/crypto.h"
#include "utils/trace.h"

using namespace file_oram::path_osm;

namespace file_oram::path_em {
/*
    This is a Wrapper-Class for EnigMap.
    It operates identically to OSM, but with an optimized intialization
   technique.
*/
class EM {
private:
  std::unique_ptr<path_osm::OSM> osm_;
  bool setup_successful_ = false;
  u_int8_t next_key = 0;
  size_t n;
  size_t val_len;
  utils::Key enc_key;
  std::shared_ptr<grpc::Channel> channel;
  storage::InitializeRequest_StoreType st;
  std::string store_path;

  EM(size_t n, size_t val_len, utils::Key enc_key,
     std::shared_ptr<grpc::Channel> channel,
     storage::InitializeRequest_StoreType st, std::string store_path);

  bool Initialize(std::vector<std::pair<path_osm::Key, path_osm::Val>> arr_);
  void InOrderTraversal(
      std::vector<path_osm::internal::Block> &b_,
      std::map<path_osm::internal::ORKey, path_osm::internal::ORPos> &bps_,
      size_t idx, size_t l, size_t r);
  void Placing(std::vector<path_osm::internal::Block> b_,
               std::map<path_osm::internal::ORKey, path_osm::internal::ORPos>
                   bps_); // Stage two of Enigmap initialization

public:
  static std::optional<EM> Construct(size_t n, size_t val_len,
                                     utils::Key enc_key,
                                     std::shared_ptr<grpc::Channel> channel,
                                     storage::InitializeRequest_StoreType st);
  void Destroy();

  void Initialization(std::vector<std::pair<path_osm::Key, path_osm::Val>>
                          arr_); // Enigmap initialization technique

  /* Inherited/Wrapped Functions */
  path_osm::OptVal Read(path_osm::Key k) { return osm_->Read(k); }
  std::vector<path_osm::Val> ReadAll(path_osm::Key k) {
    return osm_->ReadAll(k);
  }
  void Insert(path_osm::Key k, path_osm::Val v) { osm_->Insert(k, v); }
  void EvictAll() { osm_->EvictAll(); }
};
} // namespace file_oram::path_em

#endif // FILEORAM_PATH_EM_PATH_EM_H_
