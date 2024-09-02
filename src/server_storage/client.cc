#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "path_oram/path_oram.h"
#include "server_storage/block.h"
#include "server_storage/local_server.h"
#include "utils/bench.h"
#include "utils/crypto.h"

using namespace Storage;
using namespace file_oram::utils;

int main(int argc, char **argv) {
  /*
      TODO: Parameters are small for simplicity.
      Fix by exporting these as environment variables
  */
  Config c(argc, argv);

  auto ek = DumbKey();
  uint32_t n = 1 << 4;
  uint32_t val_len = 4;

  Server<char[]> server(c.storage_path_, n, val_len, "oram-server");

  std::vector<Bucket> buckets;
  std::vector<Block> blocks;
  std::vector<uint32_t> ind; // offset on the server

  // True size of our tree
  for (Key k = 0; k < 2 * (n - 1); k++) {
    buckets.push_back(ORam::Bucket(true));
  }

  for (Key k = 1; k <= n; k++) {
    auto v = std::make_unique<char[]>(val_len);
    std::fill(v.get(), v.get() + val_len, 'a');
    Block b(k, k, std::move(v));
    blocks.push_back(b);
    ind.push_back((k - 1) * Block::BlockSize(val_len));
  }

  std::cout << "Key: " << blocks[0].meta_.key_ << "\tVal:" << blocks[0].val_
            << std::endl;
  // server.WriteMany(blocks, ind, val_len);
  // server.Write(&blocks[1], Block::BlockSize(val_len), val_len);
  server.WriteAll(blocks, F_ST, val_len);
  std::cout << "Testing Server::Read\n";
  for (Key k = 3; k < 6; k++) {
    Block bp;
    server.Read((k - 1) * Block::BlockSize(val_len), Block::BlockSize(val_len),
                bp);

    std::cout << "k:" << k << "\tbp.key:" << bp.meta_.key_
              << ", bp.val:" << bp.val_ << std::endl;
  }
  std::vector<uint32_t> index2;
  index2.push_back(3 * Block::BlockSize(val_len));
  index2.push_back(4 * Block::BlockSize(val_len));
  index2.push_back(5 * Block::BlockSize(val_len));
  index2.push_back(6 * Block::BlockSize(val_len));
  std::vector<Block> res;
  server.ReadMany(index2, val_len, res);

  std::cout << "Testing Server::ReadMany:\n";
  for (auto &b : res) {
    std::cout << "bp.key:" << b.meta_.key_ << ", bp.val:" << b.val_
              << std::endl;
  }

  std::cout << "Testing Server::ReadAll:\n";
  std::vector<Block> all;
  server.ReadAll(val_len, n, all);
  for (auto &b : all) {
    std::cout << "bp.key:" << b.meta_.key_ << ", bp.val:" << b.val_
              << std::endl;
  }
  /*
   * RAMServer
   */
  ind.clear();
  blocks.clear();
  std::cout << "RAMServer..\n";
  for (Key k = 1; k <= n; k++) {
    auto v = std::make_unique<char[]>(val_len);
    std::fill(v.get(), v.get() + val_len, 'b');
    Block b(k, k, std::move(v));
    blocks.push_back(b);
    ind.push_back(k - 1);
  }

  RAMServer<Block> rserver(n);

  rserver.WriteMany(blocks, ind);

  for (Key k = 3; k < 6; k++) {
    Block bp;
    rserver.Read(k - 1, bp);
    std::cout << "RAMServer: Response (" << bp.meta_.key_ << ", "
              << bp.val_.get() << ")\n";
  }

  index2.clear();
  std::vector<Block> rserver_res_;

  index2.push_back(3);
  index2.push_back(4);
  index2.push_back(5);
  index2.push_back(6);
  rserver_res_.reserve(index2.size());
  rserver.ReadMany(index2, rserver_res_);
  std::cout << "Ram Read Many...\n";
  for (auto &b : rserver_res_) {
    std::cout << "RAMServer: Response (" << b.meta_.key_ << ", " << b.val_.get()
              << ")\n";
  }

  std::cout << "Ram Read All...\n";
  rserver_res_.clear();
  rserver_res_.reserve(n);
  rserver.ReadAll(rserver_res_);
  assert(rserver_res_.size() > 0);
  for (auto &b : rserver_res_) {
    std::cout << "RAMServer: Response (" << b.meta_.key_ << ", " << b.val_.get()
              << ")\n";
  }
}
