#ifndef FILEORAM_PATH_OMAP_PATH_OMAP_H_
#define FILEORAM_PATH_OMAP_PATH_OMAP_H_

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>

#include <grpcpp/channel.h>

#include "path_oram/path_oram.h"
#include "utils/crypto.h"

namespace file_oram::path_omap {

using Key = uint64_t;
using Val = std::shared_ptr<char[]>;
using OptVal = std::optional<Val>;

namespace internal {

using ORPos = path_oram::Pos;
using ORKey = path_oram::Key;
using ORVal = path_oram::Val;

class BlockPointer {
 public:
  ORKey key_;
  ORPos pos_;
  bool valid_ = false;

  BlockPointer() = default;
  explicit BlockPointer(ORKey k);
};
using BP = BlockPointer;

class BlockMetadata {
 public:
  Key key_;
  BP l_, r_;
  uint8_t height_;

  BlockMetadata() = default;
  BlockMetadata(Key k, uint32_t h);
  BlockMetadata(Key k, BP l, BP r, uint32_t h);
};

class Block {
 public:
  BlockMetadata meta_;
  Val val_;

  Block() = default;
  Block(Key k, Val v, uint32_t h);
  Block(Key k, Val v, BP l, BP r, uint32_t h);
  Block(char *data, size_t val_len);

  ORVal ToBytes(size_t val_len);
};
} // namespace internal

class OMap {
 public:
  static std::optional<OMap> Construct(
      size_t n, size_t val_len,
      utils::Key enc_key,
      std::shared_ptr<grpc::Channel> channel,
      storage::InitializeRequest_StoreType data_st,
      storage::InitializeRequest_StoreType aux_st,
      bool upload_stash = false,
      bool first_build = false);
  void Destroy();

  void Insert(Key k, Val v);
  OptVal Read(Key k);
  OptVal ReadAndRemove(Key k);
  void EvictAll();
  void BatchEvict();
  void EvictORam();
  void DummyOp(bool evict = false, size_t extra_fetches = 0);
  [[nodiscard]] size_t Capacity() const;
  [[nodiscard]] size_t TotalSizeOfStore() const { return oram_->TotalSizeOfStore(); }
  void FillWithDummies();
  [[nodiscard]] size_t BytesMoved() const { return oram_->BytesMoved(); }
  [[nodiscard]] bool WasPrebuilt() const { return oram_->WasPrebuilt(); }

 private:
  OMap(size_t n, size_t val_len,
       utils::Key enc_key, const std::shared_ptr<grpc::Channel>& channel,
       storage::InitializeRequest_StoreType data_st,
       storage::InitializeRequest_StoreType aux_st,
       bool upload_stash = false,
       bool first_build = false);
  std::unique_ptr<path_oram::ORam> oram_;
  const size_t capacity_;
  const size_t val_len_;
  const uint32_t pad_per_op_;
  size_t pad_to_ = 0;
  size_t num_ops_ = 0;
  // TODO for inserted prebuilds: keep track of root key.
  internal::BP root_;
  std::map<internal::ORKey, internal::Block> stash_;
  bool setup_successful_ = false;
  // This limits the number of possible insertions to (2^64 - 1); Need an additional
  // stack for freed `ORKey`s to increase the limit.
  internal::ORKey next_key_ = 0;
  std::optional<Val> delete_res_;

  internal::BP Insert(Key k, Val v, internal::BP bp);
  std::optional<internal::BP> Read(Key k, internal::BP bp);
  internal::BP ReadAndRemove(Key k, internal::BP bp);
  internal::Block &FetchOrGetFromStash(internal::BP bp);
  internal::BP Balance(internal::BP bp);
  int8_t BalanceFactor(internal::BP bp);
  uint8_t Height(internal::BP bp);
  internal::BP LRotate(internal::BP bp);
  internal::BP RRotate(internal::BP bp);
};
} // namespace file_oram::path_omap

#endif //FILEORAM_PATH_OMAP_PATH_OMAP_H_