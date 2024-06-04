#include <cassert>
#include "block.h"


BlockMetadata::BlockMetadata(bool zero_fill) {
  if (zero_fill) {
    pos_ = 0;
    key_ = 0;
  }
}

BlockMetadata::BlockMetadata(Pos p, Key k) : pos_(p), key_(k) {}

size_t Block::BlockSize(size_t val_len) {
  return sizeof(BlockMetadata) + val_len;
}

template<typename T>
T &FromBytes(const char *data, T &object) {
  auto begin_object =
      reinterpret_cast<unsigned char *> (std::addressof(object));
  std::copy(data, data + sizeof(T), begin_object);

  return object;
}

Block& Block::operator=(const Block &other) {
    if(this == &other) return *this;

    meta_.key_ = other.meta_.key_;
    meta_.pos_ = other.meta_.pos_;

    if(other.val_.get()) {
        size_t len = strlen(other.val_.get());
        val_.reset(new char[len+1]);
        std::strcpy(val_.get(), other.val_.get());
    }

    return *this;
}

Block::Block(const Block&& other) {
    meta_.key_ = other.meta_.key_;
    meta_.pos_ = other.meta_.pos_;
    if(other.val_.get()) {
        size_t len = strlen(other.val_.get());
        val_.reset(new char[len+1]);
        std::strcpy(val_.get(), other.val_.get());
    }
}


Block::Block(const Block& other) {
    meta_.key_ = other.meta_.key_;
    meta_.pos_ = other.meta_.pos_;
    if(other.val_.get()) {
        size_t len = strlen(other.val_.get());
        val_.reset(new char[len+1]);
        std::strcpy(val_.get(), other.val_.get());
    }
}


Block::Block(bool zero_fill) : meta_(zero_fill) {}
Block::Block(Pos p, Key k, Val v) : meta_(p, k), val_(std::move(v)) {}
Block::Block(char *data, size_t val_len) {
  FromBytes(data, meta_);
  assert(data);
  val_ = std::make_unique<char[]>(val_len);
  std::copy(data + sizeof(BlockMetadata),
            data + sizeof(BlockMetadata) + val_len,
            val_.get());
}

void Block::Copy(Block &from) {
    meta_.key_ = from.meta_.key_;
    meta_.pos_ = from.meta_.pos_;
    
     if(from.val_.get()) {
        size_t len = strlen(from.val_.get());
        val_.reset(new char[len+1]);
        std::strcpy(val_.get(), from.val_.get());
    }
}

void Block::ToBytes(size_t val_len, char *out) {
  const auto meta_f = reinterpret_cast<char *> (std::addressof(meta_));
  const auto meta_l = meta_f + sizeof(BlockMetadata);
  std::copy(meta_f, meta_l, out);

  if (val_) {
    std::copy(val_.get(), val_.get() + val_len, out + sizeof(BlockMetadata));
  }
}
