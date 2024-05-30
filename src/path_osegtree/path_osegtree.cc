#include "path_osegtree.h"

#include <algorithm>
#include <cstddef>
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

#define max(a, b) ((a)>(b)?(a):(b))

namespace file_oram::path_osegtree {

namespace internal {
Block::Block(Len lv, Len rv, ORPos lp, ORPos rp)
    : lv_(lv), rv_(rv), lp_(lp), rp_(rp) {}
Block::Block(char *data) {
  utils::FromBytes(data, *this);
}

ORVal Block::ToBytes() {
  ORVal res = std::make_unique<char[]>(sizeof(Block));
  const auto f = reinterpret_cast<const char *> (std::addressof(*this));
  std::copy_n(f, sizeof(Block), res.get());
  return std::move(res);
}
} // namespace internal

std::optional<OSegTree> OSegTree::Construct(
    size_t n, Len max_val,
    utils::Key enc_key, std::shared_ptr<grpc::Channel> channel,
    storage::InitializeRequest_StoreType data_st,
    storage::InitializeRequest_StoreType aux_st,
    bool upload_stash,
    bool first_build) {
  auto o = OSegTree(n, max_val, enc_key, std::move(channel),
                    data_st, aux_st, upload_stash, first_build);
  if (o.setup_successful_) { return o; }
  return std::nullopt;
}

OSegTree::OSegTree(
    const size_t n, const Len max_val,
    const utils::Key enc_key, const std::shared_ptr<grpc::Channel> &channel,
    storage::InitializeRequest_StoreType data_st,
    storage::InitializeRequest_StoreType aux_st,
    bool upload_stash,
    bool first_build)
    : capacity_(n),
      max_val_(max_val),
      root_val_(max_val_),
    // root_pos_ = oram_->min_pos_ --> n-1
      root_pos_(n - 1) {
  my_assert(!(n & (n - 1)));
  my_assert(n > 0);
  if (n == 1) { // Handle offline
    setup_successful_ = true;
    return;
  }
  auto opt_oram = path_oram::ORam::Construct(
      n, sizeof(internal::Block), enc_key,
      channel, data_st, aux_st, upload_stash,
      utils::GenName({
                         "ost", "",
                         "n", std::to_string(n),
                         "v", std::to_string(max_val)
                     }), first_build);
  if (!opt_oram.has_value()) {
    return;
  }
  oram_ = std::unique_ptr<path_oram::ORam>(opt_oram.value());
//  root_pos_ = oram_->min_pos_;
  if (oram_->WasPrebuilt()) {
    setup_successful_ = true;
    return;
  }

  std::map<Key, internal::ORPos> pm;
  for (Key k = n - 2; k > 0; --k) {
    internal::ORPos lp = 0, rp = 0;
    if (k <= (n / 2) - 2) { // Above last level
      lp = pm[internal::LChild(k)];
      rp = pm[internal::RChild(k)];
    }
    pm[k] = oram_->AddToStash(k, internal::Block(max_val, max_val, lp, rp).ToBytes());
  }
  oram_->AddToStash(
      root_pos_, 0,
      internal::Block(max_val, max_val,
                      pm[internal::LChild(0)],
                      pm[internal::RChild(0)]
      ).ToBytes());
  oram_->BatchSetupEvictAll();

  setup_successful_ = true;
}

OptKey OSegTree::Alloc(Len req_len) {
  if (root_val_ < req_len) {
    DummyOp();
    return std::nullopt;
  }
  if (capacity_ == 1) {
    root_val_ -= req_len;
    return 0;
  }

  my_assert(root_val_ <= max_val_);

  std::map<Key, Len> lengths;
  std::map<Key, internal::ORPos> pm;
  Key k = 0;
  internal::ORPos p = root_pos_;
  while (k < capacity_ - 1) {
    oram_->FetchPath(p);
    auto ov = oram_->ReadAndRemoveFromStash(k);
    my_assert(ov.has_value());
    internal::Block b(ov->get());

    auto l = internal::LChild(k);
    lengths[l] = b.lv_;
    pm[l] = b.lp_;
    auto r = internal::RChild(k);
    lengths[r] = b.rv_;
    pm[r] = b.rp_;

    k = l;
    p = b.lp_;
    if (b.lv_ < req_len) {
      k = r;
      p = b.rp_;
    }
  }

  my_assert(lengths[k] >= req_len);
  auto res = internal::LeafToKey(k, capacity_);
  lengths[k] -= req_len;

  while (k > 0) {
    k = internal::Parent(k);
    auto l = internal::LChild(k);
    auto r = internal::RChild(k);
    internal::Block bl(lengths[l], lengths[r], pm[l], pm[r]);
    lengths[k] = max(bl.lv_, bl.rv_);
    auto pos = k == 0 ? root_pos_ : oram_->GeneratePos();
    oram_->AddToStash(pos, k, bl.ToBytes());
    pm[k] = pos;
  }
  oram_->EvictAll();
  root_val_ = lengths[0];

  my_assert(root_val_ <= max_val_);

  return res;
}

void OSegTree::Free(Key k, Len len) {
  auto leaf = internal::KeyToLeaf(k, capacity_);
  my_assert(leaf >= capacity_ - 1);
  my_assert(leaf <= (2 * capacity_) - 2);
  if (capacity_ == 1) {
    my_assert(root_val_ <= max_val_ - len);
    root_val_ += len;
    return;
  }

  my_assert(root_val_ <= max_val_);

  auto node = leaf;
  std::vector<Key> path;
  while (node > 0) {
    node = internal::Parent(node);
    path.push_back(node);
  }

  std::map<Key, Len> lengths;
  std::map<Key, internal::ORPos> pm;
  my_assert(node == 0);
  pm[0] = root_pos_;
  for (auto it = path.rbegin(); it < path.rend(); ++it) {
    node = *it;
    auto p = pm[node];
    oram_->FetchPath(p);
    auto ov = oram_->ReadAndRemoveFromStash(node);
    my_assert(ov.has_value());
    internal::Block b(ov->get());

    auto l = internal::LChild(node);
    lengths[l] = b.lv_;
    pm[l] = b.lp_;
    auto r = internal::RChild(node);
    lengths[r] = b.rv_;
    pm[r] = b.rp_;
  }

  my_assert(node == internal::Parent(leaf));
  my_assert(lengths[leaf] <= max_val_ - len);
  lengths[leaf] += len;

  for (auto it = path.begin(); it < path.end(); ++it) {
    node = *it;
    auto l = internal::LChild(node);
    auto r = internal::RChild(node);
    internal::Block bl(lengths[l], lengths[r], pm[l], pm[r]);
    lengths[node] = max(bl.lv_, bl.rv_);
    auto pos = node == 0 ? root_pos_ : oram_->GeneratePos();
    oram_->AddToStash(pos, node, bl.ToBytes());
    pm[node] = pos;
  }
  oram_->EvictAll();
  root_val_ = lengths[0];

  my_assert(root_val_ <= max_val_);
}

void OSegTree::DummyOp() {
  Free(0, 0);
}

void OSegTree::Destroy() {
  if (oram_)
    oram_->Destroy();
}

size_t OSegTree::TotalSizeOfStore() const {
  if (oram_) return oram_->TotalSizeOfStore();
  return sizeof(root_val_);
}

size_t OSegTree::BytesMoved() const {
  if (oram_) return oram_->TotalSizeOfStore();
  return 0;
}
} // namespace file_oram::path_osegtree
