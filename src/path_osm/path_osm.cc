#include "path_osm.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>

#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>

#include "path_oram/path_oram.h"
#include "utils/assert.h"
#include "utils/bytes.h"
#include "utils/crypto.h"
#include "utils/trace.h"

#define max(a, b) ((a) > (b) ? (a) : (b))

using namespace file_oram::path_osm;
using namespace file_oram::path_osm::internal;
using klock = std::chrono::high_resolution_clock;

BlockPointer::BlockPointer(ORKey k) : key_(k), valid_(true) {}
BlockPointer::BlockPointer(ORKey k, ORPos pos)
    : key_(k), pos_(pos), valid_(true) {}

BlockMetadata::BlockMetadata(Key k, uint32_t h)
    : key_(k), height_(h), l_count_(0), r_count_(0) {}
BlockMetadata::BlockMetadata(Key k, BP l, BP r, uint32_t h)
    : key_(k), l_(l), r_(r), height_(h), l_count_(0), r_count_(0) {}
BlockMetadata::BlockMetadata(Key k, BP l, BP r, uint32_t h, uint32_t lc,
                             uint32_t rc)
    : key_(k), l_(l), r_(r), height_(h), l_count_(lc), r_count_(rc) {}

namespace {
inline static size_t BlockSize(size_t val_len) {
  return sizeof(BlockMetadata) + val_len;
}
} // namespace

Block::Block(Key k, Val v, uint32_t h) : meta_(k, h), val_(std::move(v)) {}
Block::Block(Key k, Val v, BP l, BP r, uint32_t h)
    : meta_(k, l, r, h), val_(std::move(v)) {}
Block::Block(Key k, const Val &v, BP l, BP r, uint32_t h, uint32_t lc,
             uint32_t rc)
    : meta_(k, l, r, h, lc, rc), val_(v) {}
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

namespace {
static Block empty(0, nullptr, 0); // TODO: Hack; fix!
} // namespace

std::optional<OSM> OSM::Construct(const size_t n, const size_t val_len,
                                  const utils::Key enc_key,
                                  std::shared_ptr<grpc::Channel> channel,
                                  storage::InitializeRequest_StoreType st) {
  auto o = OSM(n, val_len, enc_key, channel, st);
  if (o.setup_successful_) {
    return o;
  }
  return std::nullopt;
}

void OSM::Destroy() { oram_->Destroy(); }
size_t OSM::Capacity() const { return capacity_; }
void OSM::FillWithDummies() { return oram_->FillWithDummies(); }

OSM::OSM(size_t n, size_t val_len, utils::Key enc_key,
         std::shared_ptr<grpc::Channel> channel,
         storage::InitializeRequest_StoreType st)
    : capacity_(n), val_len_(val_len), pad_per_op_(ceil(1.44 * 3.0 * log2(n))) {
  if (n & (n - 1)) { // Not a power of 2.
    return;
  }
  auto opt_oram = path_oram::ORam::Construct(n, BlockSize(val_len), enc_key,
                                             std::move(channel), st, st,
                                             false); // TODO st, st
  if (!opt_oram.has_value()) {
    return;
  }
  oram_ = std::unique_ptr<path_oram::ORam>(opt_oram.value());
  oram_->FillWithDummies();
  setup_successful_ = true;
}

void OSM::Insert(Key k, Val v) {
  pad_to_ += pad_per_op_;
  root_ = Insert(k, std::move(v), root_);
}

OptVal OSM::Read(Key k) {
  pad_to_ += pad_per_op_;
  auto bp = Read(k, root_);
  if (!bp.has_value())
    return std::nullopt;
  return stash_[bp->key_].val_;
}

std::vector<Val> OSM::ReadAll(Key k) {
  pad_to_ += 2 * pad_per_op_;
  auto count = Count(k);
  if (count == 0) {
    return {};
  }

  pad_to_ += 2 * count; // l * 2 + 2 *3*1.44 logN
  std::vector<Val> res;
  ReadAll(k, root_, res);

  return res;
}

uint32_t OSM::Count(Key k) {
  pad_to_ += pad_per_op_;

  my_assert(root_.valid_);
  return Count(k, root_);
}

OptVal OSM::ReadAndRemove(Key k) {
  pad_to_ += pad_per_op_;
  auto count = Count(k);
  if (k == 0) {
    return std::nullopt;
  }
  root_ = ReadAndRemove(k, root_);
  auto res = std::move(delete_res_);
  delete_res_.reset();
  return res;
}

/*
  This function is only used in EnigMap (for now).
  It takes in a prebuilt position map and the prebuilt blocks b_.
  @b_ is a vector of blocks that are prebuilt and are to be inserted into the
  ORAM.
  @bps_ is a map of key to position in the ORAM.
*/
void OSM::PrebuildEvict(std::map<ORKey, ORPos> &bps_,
                        std::vector<internal::Block> &b_) {
  // Set the root position
  root_.key_ = (capacity_ - 1) / 2;

  if (bps_.find(root_.key_) != bps_.end())
    root_.pos_ = bps_[root_.key_];

  for (int ok = 0; ok < b_.size(); ok++) {
    auto op = bps_[ok];
    auto bl = std::move(b_[ok]);
    auto ov = bl.ToBytes(val_len_);

    oram_->AddToStash(op, ok, std::move(ov));
  }

  root_.valid_ = true;
  oram_->EvictAll();
}

void OSM::EvictAll() {
  // Pad reads
  std::clog << "{ Already fetched: " << oram_->GetAlreadyFetched()
            << ", number of opers: " << num_ops_ << ", ";
  oram_->ResetAlreadyFetched();
  if (prebuild_phase_ == false) {
    for (auto cnt = num_ops_; cnt < pad_to_; ++cnt) {
      oram_->FetchDummyPath();
    }
  }
  std::clog << "dummy paths actually fetched: " << oram_->GetAlreadyFetched()
            << " }\n";
  oram_->ResetAlreadyFetched();
  // Re-position and re-write all cached
  std::map<ORKey, ORPos> pos_map;
  for (auto &stash_entry : stash_) {
    pos_map[stash_entry.first] = oram_->GeneratePos();
  }

  // Update client's state with the new position of the root
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
  prebuild_phase_ =
      false; // ensure that every operation (after the first one) is padded
}

void OSM::EvictORam() { oram_->EvictAll(); }

void OSM::DummyOp(bool evict) {
  Read(0);
  if (evict) {
    EvictAll();
  }
}

BP OSM::Insert(Key k, Val v, BP bp) {
  if (!bp.valid_) {
    bp = BP(next_key_++);
    stash_[bp.key_] = Block(k, std::move(v), 1);
    my_assert(next_key_ != 0); // Overflow; more than 2^64 insertions!
    return bp;
  }

  auto &p = FetchOrGetFromStash(bp);
  if (k < p.meta_.key_ ||
      (k == p.meta_.key_ && IsSmaller(v, p.val_, val_len_))) {
    if (p.meta_.key_ == k) {
      ++p.meta_.l_count_;
    }
    p.meta_.l_ = Insert(k, std::move(v), p.meta_.l_);
  } else {
    if (p.meta_.key_ == k) {
      ++p.meta_.r_count_;
    }
    p.meta_.r_ = Insert(k, std::move(v), p.meta_.r_);
  }

  p.meta_.height_ = 1 + max(Height(p.meta_.l_), Height(p.meta_.r_));
  return Balance(bp);
}

std::optional<BP> OSM::Read(Key k, BP bp) {
  if (!bp.valid_)
    return std::nullopt;
  auto &bl = FetchOrGetFromStash(bp);
  if (bl.meta_.key_ == k)
    return bp;
  if (k < bl.meta_.key_)
    return Read(k, bl.meta_.l_);
  return Read(k, bl.meta_.r_);
}

void OSM::ReadAll(Key k, internal::BP bp, std::vector<Val> &res) {
  auto &bl = FetchOrGetFromStash(bp);
  if (k < bl.meta_.key_) {
    ReadAll(k, bl.meta_.l_, res);
    return;
  }
  if (k > bl.meta_.key_) {
    ReadAll(k, bl.meta_.r_, res);
    return;
  }
  // k == bl.meta_.key
  if (bl.meta_.l_count_ > 0) {
    ReadAll(k, bl.meta_.l_, res);
  }
  res.push_back(bl.val_);
  if (bl.meta_.r_count_ > 0) {
    ReadAll(k, bl.meta_.r_, res);
  }
}

uint32_t OSM::Count(Key k, BP bp) {
  while (bp.valid_) {
    auto &bl = FetchOrGetFromStash(bp);
    if (bl.meta_.key_ == k) {
      return 1 + bl.meta_.l_count_ + bl.meta_.r_count_;
    }
    if (k < bl.meta_.key_) {
      bp = bl.meta_.l_;
      continue;
    }
    bp = bl.meta_.r_;
  }
  return 0;
}

BP OSM::ReadAndRemove(Key k, BP bp) {
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

  p.meta_.l_count_ = Count(p.meta_.key_, p.meta_.l_);
  p.meta_.r_count_ = Count(p.meta_.key_, p.meta_.r_);
  return Balance(bp);
}

Block &OSM::FetchOrGetFromStash(BP bp) {
  if (!bp.valid_) {
    return empty; // TODO: Hack; Fix!
  }
  if (stash_.find(bp.key_) != stash_.end()) {
    return stash_[bp.key_];
  }

  bool suc = oram_->FetchPath(bp.pos_);
  if (suc)
    ++num_ops_;

  auto opt_bl_bytes = oram_->ReadAndRemoveFromStash(bp.key_);
  my_assert(opt_bl_bytes.has_value()); // As `bp` is valid.
  Block res(opt_bl_bytes.value().get(), val_len_);
  stash_[bp.key_] = std::move(res);
  return stash_[bp.key_];
}

BP OSM::Balance(BP bp) {
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

int8_t OSM::BalanceFactor(BP bp) {
  if (!bp.valid_)
    return 0;
  auto &p = FetchOrGetFromStash(bp);
  auto l_h = Height(p.meta_.l_);
  auto r_h = Height(p.meta_.r_);
  return r_h - l_h;
}

uint8_t OSM::Height(BP bp) {
  if (!bp.valid_)
    return 0;
  return FetchOrGetFromStash(bp).meta_.height_;
}

BP OSM::LRotate(BP bp) {
  auto &p = FetchOrGetFromStash(bp); // Parent
  auto &l = FetchOrGetFromStash(p.meta_.l_);
  auto &r = FetchOrGetFromStash(p.meta_.r_);
  auto &r_l = FetchOrGetFromStash(r.meta_.l_);
  auto &r_r = FetchOrGetFromStash(r.meta_.r_);

  auto res = p.meta_.r_;

  auto new_l = Block(p.meta_.key_, p.val_, p.meta_.l_, r.meta_.l_,
                     1 + max(l.meta_.height_, r_l.meta_.height_),
                     p.meta_.l_count_, Count(p.meta_.key_, r.meta_.l_));
  p = std::move(new_l);
  auto new_r = r_r;

  auto new_p = Block(r.meta_.key_, r.val_, bp, r.meta_.r_,
                     1 + max(new_l.meta_.height_, new_r.meta_.height_),
                     Count(r.meta_.key_, bp), r.meta_.r_count_);
  r = std::move(new_p);

  return res;
}

BP OSM::RRotate(BP bp) {
  auto &p = FetchOrGetFromStash(bp); // Parent
  auto &l = FetchOrGetFromStash(p.meta_.l_);
  auto &r = FetchOrGetFromStash(p.meta_.r_);
  auto &l_l = FetchOrGetFromStash(l.meta_.l_);
  auto &l_r = FetchOrGetFromStash(l.meta_.r_);

  auto res = p.meta_.l_;

  auto new_l = l_l;
  auto new_r = Block(p.meta_.key_, p.val_, l.meta_.r_, p.meta_.r_,
                     1 + max(l_r.meta_.height_, r.meta_.height_),
                     Count(p.meta_.key_, l.meta_.r_), p.meta_.r_count_);
  p = std::move(new_r);

  auto new_p = Block(l.meta_.key_, l.val_, l.meta_.l_, bp,
                     1 + max(new_l.meta_.height_, new_r.meta_.height_),
                     l.meta_.l_count_, Count(l.meta_.key_, bp));
  l = std::move(new_p);

  return res;
}
