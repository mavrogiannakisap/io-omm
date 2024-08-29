#include "path_omap.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <utility>

#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>

#include "path_oram/path_oram.h"
#include "utils/assert.h"
#include "utils/bytes.h"
#include "utils/crypto.h"
#include "utils/namegen.h"
#include "utils/trace.h"

#define max(a, b) ((a) > (b) ? (a) : (b))

namespace file_oram::path_omap {

namespace internal {

BlockPointer::BlockPointer(ORKey k) : key_(k), valid_(true) {}

BlockMetadata::BlockMetadata(Key k, uint32_t h) : key_(k), height_(h) {}
BlockMetadata::BlockMetadata(Key k, BP l, BP r, uint32_t h)
    : key_(k), l_(l), r_(r), height_(h) {}

inline static size_t BlockSize(size_t val_len) {
  return sizeof(BlockMetadata) + val_len;
}

Block::Block(Key k, Val v, uint32_t h) : meta_(k, h), val_(std::move(v)) {}
Block::Block(Key k, Val v, BP l, BP r, uint32_t h)
    : meta_(k, l, r, h), val_(std::move(v)) {}
Block::Block(char *data, size_t val_len) {
  utils::FromBytes(data, meta_);
  val_ = std::make_unique<char[]>(val_len);
  std::copy(data + sizeof(BlockMetadata),
            data + sizeof(BlockMetadata) + val_len, val_.get());
}

ORVal Block::ToBytes(size_t val_len) {
  ORVal res = std::make_unique<char[]>(BlockSize(val_len));
  const auto meta_f = reinterpret_cast<const char *>(std::addressof(meta_));
  const auto meta_l = meta_f + sizeof(BlockMetadata);
  std::copy(meta_f, meta_l, res.get());
  if (val_)
    std::copy(val_.get(), val_.get() + val_len,
              res.get() + sizeof(BlockMetadata));
  return std::move(res);
}

static internal::Block empty(0, nullptr, 0); // TODO: Hack; fix!
} // namespace internal

std::optional<OMap>
OMap::Construct(const size_t n, const size_t val_len, const utils::Key enc_key,
                std::shared_ptr<grpc::Channel> channel,
                storage::InitializeRequest_StoreType data_st,
                storage::InitializeRequest_StoreType aux_st, bool upload_stash,
                bool first_build) {
  auto o = OMap(n, val_len, enc_key, std::move(channel), data_st, aux_st,
                upload_stash, first_build);
  if (o.setup_successful_) {
    return o;
  }
  return std::nullopt;
}

void OMap::Destroy() { oram_->Destroy(); }
size_t OMap::Capacity() const { return capacity_; }
void OMap::FillWithDummies() { oram_->FillWithDummies(); }

OMap::OMap(size_t n, size_t val_len, utils::Key enc_key,
           const std::shared_ptr<grpc::Channel> &channel,
           storage::InitializeRequest_StoreType data_st,
           storage::InitializeRequest_StoreType aux_st, bool upload_stash,
           bool first_build)
    : capacity_(n), val_len_(val_len), pad_per_op_(ceil(1.44 * 3.0 * log2(n))) {
  if (n & (n - 1)) { // Not a power of 2.
    return;
  }
  auto opt_oram = path_oram::ORam::Construct(n, internal::BlockSize(val_len),
                                             enc_key, std::move(channel),
                                             data_st, aux_st, upload_stash,
                                             utils::GenName({
                                                 "omap",
                                                 "",
                                                 "n",
                                                 std::to_string(n),
                                                 "v",
                                                 std::to_string(val_len),
                                             }),
                                             first_build);
  if (!opt_oram.has_value()) {
    return;
  }
  oram_ = std::unique_ptr<path_oram::ORam>(opt_oram.value());
  setup_successful_ = true;
}

void OMap::Insert(Key k, Val v) {
  pad_to_ += pad_per_op_;
  root_ = Insert(k, std::move(v), root_);
}

OptVal OMap::Read(Key k) {
  pad_to_ += pad_per_op_;
  auto bp = Read(k, root_);
  if (!bp.has_value())
    return std::nullopt;
  return stash_[bp->key_].val_;
}

OptVal OMap::ReadAndRemove(Key k) {
  pad_to_ += pad_per_op_;
  root_ = ReadAndRemove(k, root_);
  auto res = std::move(delete_res_);
  delete_res_.reset();
  return res;
}

void OMap::EvictAll() {
  // Pad reads
  for (auto cnt = num_ops_; cnt < pad_to_; ++cnt)
    oram_->FetchDummyPath();

  // Re-position and re-write all cached
  std::map<internal::ORKey, internal::ORPos> pos_map;
  for (auto &stash_entry : stash_) {
    auto ok = stash_entry.first;
    pos_map[ok] = ok == root_.key_ ? oram_->min_pos_ : oram_->GeneratePos();
  }

  if (pos_map.find(root_.key_) != pos_map.end())
    root_.pos_ = pos_map[root_.key_];

  for (auto &stash_entry : stash_) {
    auto ok = stash_entry.first;
    auto op = pos_map[ok];
    auto bl = std::move(stash_entry.second);
    if (pos_map.find(bl.meta_.l_.key_) != pos_map.end())
      bl.meta_.l_.pos_ = pos_map[bl.meta_.l_.key_];
    if (pos_map.find(bl.meta_.r_.key_) != pos_map.end())
      bl.meta_.r_.pos_ = pos_map[bl.meta_.r_.key_];
    auto ov = bl.ToBytes(val_len_);
    oram_->AddToStash(op, ok, std::move(ov));
  }

  pad_to_ = 0;
  num_ops_ = 0;
  stash_.clear();
  EvictORam();
}

void OMap::EvictORam() { oram_->EvictAll(); }

void OMap::DummyOp(bool evict, size_t extra_fetches) {
  Read(0);
  for (int i = 0; i < extra_fetches; ++i) {
    oram_->FetchDummyPath();
  }
  if (evict) {
    EvictAll();
  }
}

internal::BP OMap::Insert(Key k, Val v, internal::BP bp) {
  if (!bp.valid_) {
    bp = internal::BP(next_key_++);
    stash_[bp.key_] = internal::Block(k, std::move(v), 1);
    my_assert(next_key_ != 0); // Overflow; more than 2^64 insertions!
    return bp;
  }

  auto &p = FetchOrGetFromStash(bp);
  if (p.meta_.key_ == k) {
    p.val_ = std::move(v);
    return bp;
  }

  if (k < p.meta_.key_)
    p.meta_.l_ = Insert(k, std::move(v), p.meta_.l_);
  else
    p.meta_.r_ = Insert(k, std::move(v), p.meta_.r_);

  p.meta_.height_ = 1 + max(Height(p.meta_.l_), Height(p.meta_.r_));
  return Balance(bp);
}

std::optional<internal::BP> OMap::Read(Key k, internal::BP bp) {
  if (!bp.valid_)
    return std::nullopt;
  auto &bl = FetchOrGetFromStash(bp);
  if (bl.meta_.key_ == k)
    return bp;
  if (k < bl.meta_.key_)
    return Read(k, bl.meta_.l_);
  return Read(k, bl.meta_.r_);
}

internal::BP OMap::ReadAndRemove(Key k, internal::BP bp) {
  if (!bp.valid_) // Empty subtree
    return bp;

  auto &p = FetchOrGetFromStash(bp);

  if (k < p.meta_.key_) {
    p.meta_.l_ = ReadAndRemove(k, p.meta_.l_);
    return Balance(bp);
  }
  if (k > p.meta_.key_) {
    p.meta_.r_ = ReadAndRemove(k, p.meta_.r_);
    return Balance(bp);
  }

  // key == current_block->k_
  if (!delete_res_.has_value()) {
    delete_res_ = std::move(p.val_);
  } // Else the actual node had two children, and we're deleting the successor.

  if (!p.meta_.l_.valid_ && !p.meta_.r_.valid_) { // No children
    stash_.erase(bp.key_);
    return {};
  }
  if (p.meta_.l_.valid_ && !p.meta_.r_.valid_) { // LChild only
    auto res = p.meta_.l_;
    stash_.erase(bp.key_);
    return res;
  }
  if (!p.meta_.l_.valid_ && p.meta_.r_.valid_) { // RChild only
    auto res = p.meta_.r_;
    stash_.erase(bp.key_);
    return res;
  }

  // Two children
  //   1. Find the successor
  auto it = p.meta_.r_;
  auto successor = FetchOrGetFromStash(it);
  while (successor.meta_.l_.valid_) {
    it = successor.meta_.l_;
    successor = FetchOrGetFromStash(it);
  }
  //   2. Set current node's value to the successor's
  p.meta_.key_ = successor.meta_.key_;
  p.val_ = std::move(successor.val_);
  //   3. Delete the successor -- this is done this way because the rebalance of
  //      the successor may cascade to its parents.
  p.meta_.r_ = ReadAndRemove(successor.meta_.key_, p.meta_.r_);
  return Balance(bp);
}

internal::Block &OMap::FetchOrGetFromStash(internal::BP bp) {
  if (!bp.valid_) {
    return internal::empty; // TODO: Hack; Fix!
  }
  if (stash_.find(bp.key_) != stash_.end()) {
    return stash_[bp.key_];
  }

  bool suc = oram_->FetchPath(bp.pos_);
  if (suc)
    num_ops_++;

  auto opt_bl_bytes = oram_->ReadAndRemoveFromStash(bp.key_);
  my_assert(opt_bl_bytes.has_value()); // As `bp` is valid.
  internal::Block res(opt_bl_bytes.value().get(), val_len_);
  stash_[bp.key_] = std::move(res);
  return stash_[bp.key_];
}

internal::BP OMap::Balance(internal::BP bp) {
  auto bf = BalanceFactor(bp);
  if (-1 <= bf && bf <= 1) // No rebalance necessary.
    return bp;

  auto &p = FetchOrGetFromStash(bp);
  if (bf < -1) { // left-heavy
    auto l_bf = BalanceFactor(p.meta_.l_);
    if (l_bf > 0) // left-right
      p.meta_.l_ = LRotate(p.meta_.l_);
    return RRotate(bp);
  }
  // right-heavy
  auto r_bf = BalanceFactor(p.meta_.r_);
  if (r_bf < 0)
    p.meta_.r_ = RRotate(p.meta_.r_);
  return LRotate(bp);
}

int8_t OMap::BalanceFactor(internal::BP bp) {
  if (!bp.valid_)
    return 0;
  auto &p = FetchOrGetFromStash(bp);
  auto l_h = Height(p.meta_.l_);
  auto r_h = Height(p.meta_.r_);
  return r_h - l_h;
}

uint8_t OMap::Height(internal::BP bp) {
  if (!bp.valid_)
    return 0;
  return FetchOrGetFromStash(bp).meta_.height_;
}

internal::BP OMap::LRotate(internal::BP bp) {
  auto &p = FetchOrGetFromStash(bp); // Parent
  auto &l = FetchOrGetFromStash(p.meta_.l_);
  auto &r = FetchOrGetFromStash(p.meta_.r_);
  auto &r_l = FetchOrGetFromStash(r.meta_.l_);
  auto &r_r = FetchOrGetFromStash(r.meta_.r_);

  auto res = p.meta_.r_;
  auto new_l =
      internal::Block(p.meta_.key_, std::move(p.val_), p.meta_.l_, r.meta_.l_,
                      1 + max(l.meta_.height_, r_l.meta_.height_));
  auto new_r = r_r;
  auto new_p =
      internal::Block(r.meta_.key_, std::move(r.val_), bp, r.meta_.r_,
                      1 + max(new_l.meta_.height_, new_r.meta_.height_));

  stash_[p.meta_.r_.key_] = std::move(new_p);
  stash_[bp.key_] = std::move(new_l);
  return res;
}

internal::BP OMap::RRotate(internal::BP bp) {
  auto &p = FetchOrGetFromStash(bp); // Parent
  auto &l = FetchOrGetFromStash(p.meta_.l_);
  auto &r = FetchOrGetFromStash(p.meta_.r_);
  auto &l_l = FetchOrGetFromStash(l.meta_.l_);
  auto &l_r = FetchOrGetFromStash(l.meta_.r_);

  auto res = p.meta_.l_;
  auto new_l = l_l;
  auto new_r =
      internal::Block(p.meta_.key_, std::move(p.val_), l.meta_.r_, p.meta_.r_,
                      1 + max(l_r.meta_.height_, r.meta_.height_));
  auto new_p =
      internal::Block(l.meta_.key_, std::move(l.val_), l.meta_.l_, bp,
                      1 + max(new_l.meta_.height_, new_r.meta_.height_));

  stash_[p.meta_.l_.key_] = std::move(new_p);
  stash_[bp.key_] = std::move(new_r);
  return res;
}
} // namespace file_oram::path_omap
