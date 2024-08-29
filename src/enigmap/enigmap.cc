#include "enigmap.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <optional>

#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <utility>
#include <vector>

#include "path_oram/path_oram.h"
#include "path_osm/path_osm.h"
#include "utils/assert.h"
#include "utils/bytes.h"
#include "utils/crypto.h"
#include "utils/trace.h"

using namespace file_oram::path_osm;
using namespace file_oram::path_osm::internal;
using namespace file_oram::path_em;

std::optional<EM> EM::Construct(size_t n, size_t val_len, utils::Key enc_key,
                                std::shared_ptr<grpc::Channel> channel,
                                storage::InitializeRequest_StoreType st,
                                std::string store_path) {

  auto em = EM(n, val_len, enc_key, channel, st, store_path);
  if (em.setup_successful_) {
    return em;
  }
  return std::nullopt;
}

void EM::Initialization(
    std::vector<std::pair<path_osm::Key, path_osm::Val>> arr_) {
  bool success = Initialize(arr_);
  if (!success) {
    std::cerr << "EM Initialization failed" << std::endl;
    return;
  }
}

void EM::Destroy() { osm_->Destroy(); }

void EM::Placing(std::vector<path_osm::internal::Block> b_,
                 std::map<ORKey, ORPos> bps_) {
  /* TBA */
  // osm->Placing(b_, bps_);
  return;
}

void EM::InOrderTraversal(std::vector<path_osm::internal::Block> &b_,
                          std::map<ORKey, ORPos> &bps_, size_t idx, size_t l,
                          size_t r) {
  if (l >= r || l < 0 || r >= n) {
    return;
  }

  size_t m = l + (r - l) / 2;

  InOrderTraversal(b_, bps_, b_[idx].meta_.l_.key_, l, m - 1);

  b_[idx].meta_.l_ = BlockPointer((l + m - 1) / 2, bps_[(l + m - 1) / 2]);
  b_[idx].meta_.r_ = BlockPointer((m + r) / 2), bps_[(m + r) / 2];
  assert(b_[idx].meta_.l_.key_ < idx && b_[idx].meta_.l_.key_ >= 0);
  assert(b_[idx].meta_.r_.key_ > idx && b_[idx].meta_.r_.key_ <= r);

  InOrderTraversal(b_, bps_, b_[idx].meta_.r_.key_, m, r);
}

bool EM::Initialize(std::vector<std::pair<path_osm::Key, path_osm::Val>> arr_) {
  if (arr_.empty()) {
    return false;
  }

  assert(arr_.size() == n);
  std::vector<path_osm::internal::Block> b_;
  std::map<ORKey, ORPos> bps_; // block pointers, BP = { ORKey, ORPos }
  ORKey next_key_ = 0;

  /* We can skip this since the experiments are on synthesized data, i.e., input
   * is already sorted. */
  /*std::sort(arr_.begin(), arr_.end(), [](const std::pair<path_osm::Key,
     path_osm::Val>& a, const std::pair<path_osm::Key, path_osm::Val>& b) {
      return a.first < b.first;
      }); */

  for (auto &elem : arr_) {
    auto &[k, v] = elem;
    path_osm::internal::Block block(k, v, 1);
    b_.push_back(block);
    auto pos = osm_->GeneratePos();
    bps_[next_key_++] = pos;
  }

  InOrderTraversal(b_, bps_, n / 2, 0, n - 1);

  for (auto bl : b_) {
    std::cout << bl.meta_.key_ << " " << bl.val_ << " " << bl.meta_.l_.key_
              << " " << bl.meta_.r_.key_ << std::endl;
  }
  return true;
}

EM::EM(size_t n, size_t val_len,
            utils::Key enc_key,
            std::shared_ptr<grpc::Channel> channel,
            storage::InitializeRequest_StoreType st)
            : n(n), val_len(val_len), enc_key(enc_key), channel(channel), st(st)){
  auto opt_osm =
      path_osm::OSM::Construct(n, val_len, enc_key, channel, st, store_path);
  if (!opt_osm.has_value()) {
    setup_successful_ = false;
    return;
  }
  osm_ = std::make_unique<path_osm::OSM>(std::move(opt_osm.value()));
  setup_successful_ = true;
}
