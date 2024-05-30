#include "local_server.h"

using namespace Storage;

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

// ------------------
//RAMSERVER

RAMServer::RAMServer(uint32_t n) : n(n) {
    mem.reserve(n);
    std::cout << mem.size() << std::endl;
}

RAMServer::RAMServer(std::vector<Block> &blocks, uint32_t n) : n(n) {
    mem.swap(blocks);
}

RAMServer::~RAMServer() {
    mem.clear();
}

void RAMServer::WriteMany(std::vector<Block> &to_write, std::vector<uint32_t> &indexes) {
    assert(to_write.size() == indexes.size());
    auto idx = indexes.begin();
    for(auto &b: to_write) {
       mem.insert(mem.begin() + *idx, b);

       idx++;
    }

    to_write.clear();
}

void RAMServer::Write(Block &b, uint32_t idx) {
    mem.insert(mem.begin() + idx, b);
}

void RAMServer::ReadAll(std::vector<Block> &res) {
    assert(mem.size() > 0);
    assert(res.capacity() > 0);
    res = mem;
    // std::copy(mem.begin(), mem.end(), res.begin());
}


void RAMServer::ReadMany(std::vector<uint32_t> indexes, std::vector<Block> &res) {
    assert(res.capacity() > 0);
    for(auto &idx : indexes) {
        assert(idx < n && idx >= 0); 
        res.push_back(mem[idx]);
    }
}

void RAMServer::Read(uint32_t index, Block &res) {
    assert(index < n && index >= 0);
    res.Copy(mem[index]);
}

// ------------------
// SERVER
Server::Server(std::string file_path, uint32_t n) : n(n) {
    file_.open(file_path, std::ios::binary | std::ios::in | std::ios::out);

    if(!file_) {
        std::clog << "ERROR: Initialization - cannot open " << file_path << std::endl;
        std::exit(1);
    }
}

Server::~Server() {
    file_.close();
}


void Server::WriteMany(std::vector<Block> &mem, std::vector<uint32_t> &index, uint32_t val_len)
{
    assert(mem.size() == index.size());
    auto idx = index.begin();
    for(auto &b : mem) {
        file_.seekp(*idx);
        char *b_ser = (char *)malloc(Block::BlockSize(val_len));
        b.ToBytes(Block::BlockSize(val_len), b_ser);
        file_.write(b_ser, Block::BlockSize(val_len));
        
        delete[] b_ser;
        idx++;
    }
}


void Server::WriteAll(std::vector<Block> &mem, uint32_t index, uint32_t val_len) {
    file_.seekp(index);
    std::string to_write_;
    for(auto &b : mem) {
        char *b_ser = (char *)malloc(Block::BlockSize(val_len));
        b.ToBytes(Block::BlockSize(val_len), b_ser);
        to_write_.append(b_ser, Block::BlockSize(val_len));
        delete[] b_ser;
    }
    file_.write(to_write_.c_str(), Block::BlockSize(val_len) * mem.size());
}

void Server::Write(Block *b, uint32_t ind, uint32_t val_len) {
    assert(b != NULL);
    file_.seekp(ind);

    char *to_write_ = (char *)malloc(Block::BlockSize(val_len) * 1);
    b->ToBytes(Block::BlockSize(val_len), to_write_);
    file_.write(to_write_, Block::BlockSize(val_len));
    delete[] to_write_;
}

void Server::ReadAll( uint32_t val_len, uint32_t count, std::vector<Block> &res) {
    uint32_t bs_ = Block::BlockSize(val_len);
    file_.seekg(F_ST);
    char *in = (char *) malloc(bs_ * count);
    file_.read(in, bs_ * count);
    for(int i = 0; i < bs_ * count; i+=bs_) {
        char *b_ser = (char *)malloc(bs_);
        std::copy(in + i, in + i + bs_, b_ser);
        Block b(b_ser, val_len);
        res.push_back(std::move(b));
    }
    delete[] in;
}

void Server::ReadMany(std::vector<uint32_t> indexes, uint32_t val_len, std::vector<Block> &res) {
    uint32_t bs_ = Block::BlockSize(val_len);
    for (auto &idx : indexes) {
        file_.seekg(idx);
        char *in = (char *) malloc(bs_);
        file_.read(in, bs_);
        
        Block b(in, val_len);
        res.push_back(std::move(b));
        delete[] in;
    }

}

void Server::Read(uint32_t index, uint32_t val_len, Block &res) {
    file_.seekg(index);
    char *in = (char *) malloc(Block::BlockSize(val_len));
    file_.read(in, Block::BlockSize(val_len));
    assert(in);
    res = Block(in, val_len);
    free(in);
}
