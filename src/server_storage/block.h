#pragma once
#include <iostream>

using Pos = uint32_t;
using Key = uint32_t;
using Val = std::unique_ptr<char[]>;

class BlockMetadata {
 public:
  Pos pos_;
  Key key_;

  explicit BlockMetadata(bool zero_fill = false);
  BlockMetadata(Pos p, Key k);
};

class Block {
 public:
  BlockMetadata meta_;
  Val val_;

  explicit Block(bool zero_fill = false);
  Block(Pos p, Key k, Val v);
  Block(char *data, size_t val_len);
  Block& operator=(const Block &other);
  Block(const Block&& other);
  Block(const Block& other);

  void ToBytes(size_t val_len, char *out);
  void Copy(Block &from);
  static size_t BlockSize(size_t val_len);
};

