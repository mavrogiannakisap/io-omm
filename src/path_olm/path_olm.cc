#include "path_olm.h"

#include <algorithm>
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

#define max(a, b) ((a)>(b)?(a):(b))

using namespace file_oram::path_olm;
using namespace file_oram::path_olm::internal;

BlockPointer::BlockPointer(ORKey k) : key_(k), valid_(true) {}
BlockPointer::BlockPointer(ORKey k, ORPos p, bool v) : key_(k), pos_(p), valid_(v) {}

BlockMetadata::BlockMetadata(Key k, uint32_t h)
    : key_(k), height_(h) {next_ = std::nullopt;}
BlockMetadata::BlockMetadata(Key k, BP l, BP r, uint32_t h)
    : key_(k), l_(l), r_(r), height_(h) {next_ = std::nullopt;}
    
BlockMetadata::BlockMetadata(Key k, BP l, BP r, OptBP next, uint32_t h) : key_(k), l_(l), r_(r), next_(next), height_(h) {}

namespace {
inline static size_t BlockSize(size_t val_len) {
  return sizeof(BlockMetadata) + val_len;
}
}//namespace

Block::Block(Key k, Val v, uint32_t h) : meta_(k, h), val_(std::move(v)) {}
Block::Block(Key k, Val v, BP l, BP r, uint32_t h)
    : meta_(k, l, r, h), val_(std::move(v)) {}
Block::Block(char *data, size_t val_len) {
  utils::FromBytes(data, meta_);
  val_ = std::make_unique<char[]>(val_len);
  std::copy(data + sizeof(BlockMetadata),
            data + sizeof(BlockMetadata) + val_len,
            val_.get());
}
Block::Block(Key k, const Val &v, BP l, BP r, OptBP next, uint32_t h):
    meta_(k, l, r, h), val_(v) {
  meta_.next_.swap(next);
}

ORVal Block::ToBytes(size_t val_len) {
  ORVal res = std::make_unique<char[]>(BlockSize(val_len));
  const auto meta_f = reinterpret_cast<const char *> (std::addressof(meta_));
  const auto meta_l = meta_f + sizeof(BlockMetadata);
  std::copy(meta_f, meta_l, res.get());
  if (val_)
    std::copy(val_.get(), val_.get() + val_len,
              res.get() + sizeof(BlockMetadata));
  return std::move(res);
}

namespace {
static Block empty(0, nullptr, 0);// TODO: Hack; fix!
} // namespace

std::optional<OLM> OLM::Construct(
    const size_t n, const size_t val_len,
    const utils::Key enc_key, std::shared_ptr<grpc::Channel> channel,
    storage::InitializeRequest_StoreType st) {
  auto o = OLM(n, val_len, enc_key, channel, st);
  if (o.setup_successful_) { return o; }
  return std::nullopt;
}

void OLM::Destroy() { oram_->Destroy(); }
size_t OLM::Capacity() const { return capacity_; }
void OLM::FillWithDummies() { return oram_->FillWithDummies(); }

OLM::OLM(size_t n, size_t val_len,
         utils::Key enc_key, std::shared_ptr<grpc::Channel> channel,
         storage::InitializeRequest_StoreType st)
    : capacity_(n), val_len_(val_len), pad_per_op_(ceil(1.44 * 3.0 * log2(n))) {
  if (n & (n - 1)) { // Not a power of 2.
    return;
  }
  auto opt_oram = path_oram::ORam::Construct(
      n, BlockSize(val_len), enc_key, std::move(channel),
      st, st, false); // TODO st, st
  if (!opt_oram.has_value()) {
    return;
  }
  std::clog << "Created ORAM" << std::endl;
  oram_ = std::unique_ptr<path_oram::ORam>(opt_oram.value());
  oram_->FillWithDummies();
  setup_successful_ = true;
}

void OLM::Insert(Key k, Val v) {
  pad_to_ += pad_per_op_;
  root_ = Insert(k, std::move(v), root_, std::nullopt);
}

OptVal OLM::Read(Key k) {
  pad_to_ += pad_per_op_ + 2;
  auto bp = Read(k, root_);
  if (!bp.has_value()) return std::nullopt;
  return stash_[bp->key_].val_;
}

std::vector<Val> OLM::ReadAll(Key k) {
  pad_to_ += pad_per_op_;
  std::vector<Val> res;
  ReadAll(k, root_, res);
  pad_to_ += 2 * res.size();
  return res;
}

/*
OptVal OLM::ReadAndRemove(Key k) {
  pad_to_ += pad_per_op_;
  root_ = ReadAndRemove(k, root_);
  auto res = std::move(delete_res_);
  pad_to_ += 2;
  delete_res_.reset();
  return res;
}
*/

void OLM::EvictAll() {
  // Pad reads
  for (auto cnt = num_ops_; cnt < pad_to_; ++cnt)
    oram_->FetchDummyPath();

  // Re-position and re-write all cached
  if (pos_map_.find(root_.key_) != pos_map_.end())
    root_.pos_ = pos_map_[root_.key_];

  for (auto &stash_entry : stash_) {
    auto ok = stash_entry.first;
    auto op = pos_map_[ok];
    auto bl = std::move(stash_entry.second);
    if (pos_map_.find(bl.meta_.l_.key_) != pos_map_.end())
      bl.meta_.l_.pos_ = pos_map_[bl.meta_.l_.key_];
    if (pos_map_.find(bl.meta_.r_.key_) != pos_map_.end())
      bl.meta_.r_.pos_ = pos_map_[bl.meta_.r_.key_];
    auto ov = bl.ToBytes(val_len_);
    oram_->AddToStash(op, ok, std::move(ov));
  }

  pad_to_ = 0;
  num_ops_ = 0;
  stash_.clear();
  EvictORam();
}

void OLM::EvictORam() {
  oram_->EvictAll();
}

void OLM::DummyOp(bool evict) {
  Read(0);
  if (evict) {
    EvictAll();
  }
}

BP OLM::Insert(Key k, Val v, BP bp, std::optional<internal::Block> opt_parent) {
  if (!bp.valid_) {
    bp = BP(next_key_++);
    pos_map_[bp.key_] = oram_->GeneratePos();
    stash_[bp.key_] = Block(k, std::move(v), 1);
    if (opt_parent.has_value()) {
      auto parent = opt_parent.value();
      // must insert at the end of the list
      my_assert(!parent.meta_.next_.has_value() 
              && parent.meta_.key_ == k);
      parent.meta_.next_ = BP{bp.key_, pos_map_[bp.key_], true};
      my_assert(parent.meta_.next_.has_value());
    }
    my_assert(next_key_ != 0); // Overflow; more than 2^64 insertions!
    return bp;
  }

  auto &p = FetchOrGetFromStash(bp);
  if (k > p.meta_.key_) { 
       p.meta_.r_ = Insert(k, std::move(v), p.meta_.r_, opt_parent);
  } else if (k == p.meta_.key_){
    opt_parent = p;
    p.meta_.r_ = Insert(k, std::move(v), p.meta_.r_, opt_parent);
  } else { 
      p.meta_.l_ = Insert(k, std::move(v), p.meta_.l_, opt_parent);
  }

  p.meta_.height_ = 1 + max(Height(p.meta_.l_), Height(p.meta_.r_));
  return Balance(bp);
}

// Returns the first occurrence of `k` in the AVL-tree.
std::optional<BP> OLM::Read(Key k, BP bp) {
  if (!bp.valid_)
    return std::nullopt;
  auto &bl = FetchOrGetFromStash(bp);
  if (bl.meta_.key_ == k)
    return bp;
  if (k < bl.meta_.key_)
    return Read(k, bl.meta_.l_);
  return Read(k, bl.meta_.r_);
}

void OLM::ReadAll(Key k, internal::BP bp, std::vector<Val> &res) {
  if(!bp.valid_) {
    return;
  }
  auto &bl = FetchOrGetFromStash(bp);
  if (k < bl.meta_.key_) {
    ReadAll(k, bl.meta_.l_, res);
    return;
  }
  if (k > bl.meta_.key_) {
    ReadAll(k, bl.meta_.r_, res);
    return;
  }
  // k == _bl.meta_.key
  std::cout << "Reached a node with the same key" << k << ", looking at pos " << bl.meta_.next_->pos_ << std::endl;
  res.push_back(bl.val_);
  if(bl.meta_.next_.has_value()) {
    std::cout << "Going to next node " << bl.meta_.next_->pos_ << std::endl;
    ReadAll(k, bl.meta_.next_.value(), res);
  }
  ReadAll(k, bl.meta_.l_, res);
}

Block &OLM::FetchOrGetFromStash(BP bp) {
  if (!bp.valid_) {
    return empty; // TODO: Hack; Fix!
  }
  if (stash_.find(bp.key_) != stash_.end()) {
    return stash_[bp.key_];
  }

  ++num_ops_;
  oram_->FetchPath(bp.pos_);
  auto opt_bl_bytes = oram_->ReadAndRemoveFromStash(bp.key_);
  my_assert(opt_bl_bytes.has_value()); // As `bp` is valid.
  Block res(opt_bl_bytes.value().get(), val_len_);
  stash_[bp.key_] = std::move(res);
  return stash_[bp.key_];
}

BP OLM::Balance(BP bp) {
  auto bf = BalanceFactor(bp);
  if (-1 <= bf && bf <= 1) // No rebalance necessary.
    return bp;

  auto &p = FetchOrGetFromStash(bp);
  if (bf < -1) { // left-heavy
    auto l_bf = BalanceFactor(p.meta_.l_);
    if (l_bf > 0)  // left-right
      p.meta_.l_ = LRotate(p.meta_.l_);
    return RRotate(bp);
  }
  // right-heavy
  auto r_bf = BalanceFactor(p.meta_.r_);
  if (r_bf < 0)
    p.meta_.r_ = RRotate(p.meta_.r_);
  return LRotate(bp);
}

int8_t OLM::BalanceFactor(BP bp) {
  if (!bp.valid_)
    return 0;
  auto &p = FetchOrGetFromStash(bp);
  auto l_h = Height(p.meta_.l_);
  auto r_h = Height(p.meta_.r_);
  return r_h - l_h;
}

uint8_t OLM::Height(BP bp) {
  if (!bp.valid_)
    return 0;
  return FetchOrGetFromStash(bp).meta_.height_;
}

BP OLM::LRotate(BP bp) {
  auto &p = FetchOrGetFromStash(bp); // Parent
  auto &l = FetchOrGetFromStash(p.meta_.l_);
  auto &r = FetchOrGetFromStash(p.meta_.r_);
  auto &r_l = FetchOrGetFromStash(r.meta_.l_);
  auto &r_r = FetchOrGetFromStash(r.meta_.r_);

  auto res = p.meta_.r_;

  auto new_l = Block(p.meta_.key_, p.val_, p.meta_.l_, r.meta_.l_, 
                     p.meta_.next_, 1 + max(l.meta_.height_, r_l.meta_.height_));
  p = std::move(new_l);
  auto new_r = r_r;

  auto new_p = Block(r.meta_.key_, r.val_, bp, r.meta_.r_, r.meta_.next_,
                     1 + max(new_l.meta_.height_, new_r.meta_.height_));
  r = std::move(new_p);

  return res;
}

BP OLM::RRotate(BP bp) {
  auto &p = FetchOrGetFromStash(bp); // Parent
  auto &l = FetchOrGetFromStash(p.meta_.l_);
  auto &r = FetchOrGetFromStash(p.meta_.r_);
  auto &l_l = FetchOrGetFromStash(l.meta_.l_);
  auto &l_r = FetchOrGetFromStash(l.meta_.r_);

  auto res = p.meta_.l_;

  auto new_l = l_l;
  auto new_r = Block(p.meta_.key_, p.val_, l.meta_.r_, p.meta_.r_,
                     p.meta_.next_, 1 + max(l_r.meta_.height_, r.meta_.height_));
  p = std::move(new_r);

  auto new_p = Block(l.meta_.key_, l.val_, l.meta_.l_, bp,
                     l.meta_.next_, 1 + max(new_l.meta_.height_, new_r.meta_.height_));
  l = std::move(new_p);

  return res;
}
