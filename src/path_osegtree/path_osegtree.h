#ifndef FILEORAM_PATH_OSEGTREE_PATH_OSEGTREE_H_
#define FILEORAM_PATH_OSEGTREE_PATH_OSEGTREE_H_

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>

#include <grpcpp/channel.h>

#include "path_oram/path_oram.h"
#include "utils/crypto.h"

namespace file_oram::path_osegtree {

using Key = uint32_t;
using OptKey = std::optional<Key>;
using Len = uint32_t;

namespace internal {

using ORPos = path_oram::Pos;
using ORKey = path_oram::Key;
using ORVal = path_oram::Val;

class Block {
 public:
  Len lv_, rv_;
  ORPos lp_, rp_;

  Block() = default;
  Block(Len lv, Len rv, ORPos lp, ORPos rp);
  Block(char *data);

  ORVal ToBytes();
};

static Key Parent(Key k) { return (k - 1) / 2; }
static Key LChild(Key k) { return (2 * k) + 1; }
static Key RChild(Key k) { return (2 * k) + 2; }
static Key KeyToLeaf(Key k, size_t n) { return k + (n - 1); }
static Key LeafToKey(Key k, size_t n) { return k - (n - 1); }
}

class OSegTree {
 public:
  static std::optional<OSegTree> Construct(
      size_t n, Len max_val,
      utils::Key enc_key, std::shared_ptr<grpc::Channel> channel,
      storage::InitializeRequest_StoreType data_st,
      storage::InitializeRequest_StoreType aux_st,
      bool upload_stash = false,
      bool first_build = false);
  OptKey Alloc(Len req_len);
  void Free(Key k, Len len);
  void DummyOp();
  void Destroy();
  [[nodiscard]] size_t TotalSizeOfStore() const;
  [[nodiscard]] size_t BytesMoved() const;
  [[nodiscard]] bool WasPrebuilt() const { return oram_->WasPrebuilt(); }

 private:
  OSegTree(size_t n, Len max_val,
           utils::Key enc_key, const std::shared_ptr<grpc::Channel>& channel,
           storage::InitializeRequest_StoreType data_st,
           storage::InitializeRequest_StoreType aux_st,
           bool upload_stash = false,
           bool first_build = false);
  std::unique_ptr<path_oram::ORam> oram_;
  const size_t capacity_;
  const Len max_val_;
  Len root_val_;
  const internal::ORPos root_pos_;
  bool setup_successful_ = false;
};

} // namespace file_oram::path_osegtree

#endif //FILEORAM_PATH_OSEGTREE_PATH_OSEGTREE_H_
