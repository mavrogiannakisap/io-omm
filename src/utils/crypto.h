#ifndef FILEORAM_UTILS_CRYPTO_H_
#define FILEORAM_UTILS_CRYPTO_H_

#include <array>
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

#include <openssl/aes.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include "utils/assert.h"
#include "utils/trace.h"

// Hack (_)
#define min_(a, b) ((a)<(b)?(a):(b))

namespace file_oram::utils {

static const auto kDigest = EVP_sha256;
static const auto kCipher = EVP_aes_256_cbc;
static const unsigned int kKeySize = 32;
static const int kDigestSize = 32; // EVP_MD_size(kDigest());
static const unsigned int kBlockSize = AES_BLOCK_SIZE;
static const unsigned int kIvSize = AES_BLOCK_SIZE;
// Because OpenSSL uses int length arguments
static const int kEncDecBlockSize = (INT_MAX / 2) + 1;

using Key = std::array<unsigned char, kKeySize>;
using Iv = std::array<unsigned char, kIvSize>;

template<size_t n>
inline std::array<unsigned char, n> GenRandBytes() {
  std::array<unsigned char, n> res;
  RAND_bytes(res.data(), n);
  return res;
}

template<size_t n>
inline std::array<unsigned char, n> GenFFs() {
  std::array<unsigned char, n> res;
  res.fill(0xFF);
  return res;
}

inline auto GenerateKey = GenRandBytes<sizeof(Key)>;
inline auto GenerateIv = GenRandBytes<sizeof(Iv)>;

inline auto DumbKey = GenFFs<sizeof(Key)>;
inline auto DumbIv = GenFFs<sizeof(Iv)>;

inline bool Hash(const unsigned char *val,
                 const size_t val_len,
                 unsigned char *res) {
  EVP_MD_CTX *ctx;

  if ((ctx = EVP_MD_CTX_new()) == nullptr) {
    ERR_print_errors_fp(stderr);
    return false;
  }

  if (EVP_DigestInit_ex(ctx, kDigest(), nullptr) != 1) {
    ERR_print_errors_fp(stderr);
    return false;
  }

  if (EVP_DigestUpdate(ctx, val, val_len) != 1) {
    ERR_print_errors_fp(stderr);
    return false;
  }

  unsigned int res_len = 0;
  if (EVP_DigestFinal_ex(ctx, res, &res_len) != 1) {
    ERR_print_errors_fp(stderr);
    return false;
  }
  my_assert(res_len == kDigestSize);

  EVP_MD_CTX_free(ctx);
  return true;
}

constexpr inline size_t CiphertextLen(
    size_t plaintext_len) {
  return (((plaintext_len + kBlockSize)
      / kBlockSize) * kBlockSize)
      + kIvSize;
}

// Chooses a random IV, and returns the ciphertext with the IV appended to the end of it.
inline bool Encrypt(const char *val, const size_t val_len,
                    const Key key, char *res) {
  EVP_CIPHER_CTX *ctx;
  Iv iv = DumbIv();
  Iv iv_copy = Iv(iv); // Because OpenSSL may mess with the IV passed to it.

  if (!(ctx = EVP_CIPHER_CTX_new())) {
    ERR_print_errors_fp(stderr);
    EVP_CIPHER_CTX_free(ctx);
    return false;
  }

  if (1 != EVP_EncryptInit_ex(ctx, kCipher(), nullptr,
                              key.data(), iv_copy.data())) {
    ERR_print_errors_fp(stderr);
    EVP_CIPHER_CTX_free(ctx);
    return false;
  }

  int len;
  size_t done = 0;
  size_t res_offset = 0;
  while (done < val_len) {
    size_t to_encrypt = val_len - done;
    if (to_encrypt > kEncDecBlockSize) {
      to_encrypt = kEncDecBlockSize;
    }

    if (1 != EVP_EncryptUpdate(
        ctx,
        reinterpret_cast<unsigned char *>(res + res_offset), &len,
        reinterpret_cast<const unsigned char *>(val + done), to_encrypt)) {
      ERR_print_errors_fp(stderr);
      EVP_CIPHER_CTX_free(ctx);
      return false;
    }

    done += to_encrypt;
    res_offset += len;
  }

  if (1 != EVP_EncryptFinal_ex(
      ctx,
      reinterpret_cast<unsigned char *>(res + res_offset), &len)) {
    ERR_print_errors_fp(stderr);
    EVP_CIPHER_CTX_free(ctx);
    return false;
  }
  res_offset += len;
  my_assert(res_offset == (CiphertextLen(val_len) - kIvSize));

  // Append IV to the end of the ciphertext.
  std::copy(iv.begin(), iv.end(), res + res_offset);

  EVP_CIPHER_CTX_free(ctx);
  return true;
}

// Assumes the last bytes of val are the IV.
// Returns plaintext len.
inline size_t Decrypt(const std::string val, const Key key, char *res) {
  EVP_CIPHER_CTX *ctx;
  auto data = reinterpret_cast<const unsigned char *>(val.data());
  size_t clen = val.size() - kIvSize;

  if (!(ctx = EVP_CIPHER_CTX_new())) {
    ERR_print_errors_fp(stderr);
    EVP_CIPHER_CTX_free(ctx);
    return 0;
  }

  if (1 != EVP_DecryptInit_ex(
      ctx, kCipher(), nullptr, key.data(), data + clen)) {
    ERR_print_errors_fp(stderr);
    EVP_CIPHER_CTX_free(ctx);
    return 0;
  }

  int len;
  size_t done = 0;
  size_t res_offset = 0;
  while (done < clen) {
    size_t to_decrypt = clen - done;
    if (to_decrypt > kEncDecBlockSize) {
      to_decrypt = kEncDecBlockSize;
    }
    if (1 != EVP_DecryptUpdate(
        ctx,
        reinterpret_cast<unsigned char *>(res + res_offset), &len,
        data + done, to_decrypt)) {
      ERR_print_errors_fp(stderr);
      EVP_CIPHER_CTX_free(ctx);
      return 0;
    }

    done += to_decrypt;
    res_offset += len;
  }

  if (1 != EVP_DecryptFinal_ex(
      ctx,
      reinterpret_cast<unsigned char *>(res + res_offset), &len)) {
    ERR_print_errors_fp(stderr);
    EVP_CIPHER_CTX_free(ctx);
    return 0;
  }
  res_offset += len;

  EVP_CIPHER_CTX_free(ctx);

  my_assert(CiphertextLen(res_offset) == val.size());
  return res_offset;
}

inline size_t Decrypt(const char *val, size_t val_len,
                      const Key key, char *res) {
  EVP_CIPHER_CTX *ctx;
  auto data = reinterpret_cast<const unsigned char *>(val);
  size_t clen = val_len - kIvSize;

  if (!(ctx = EVP_CIPHER_CTX_new())) {
    ERR_print_errors_fp(stderr);
    EVP_CIPHER_CTX_free(ctx);
    return 0;
  }

  if (1 != EVP_DecryptInit_ex(
      ctx, kCipher(), nullptr, key.data(), data + clen)) {
    ERR_print_errors_fp(stderr);
    EVP_CIPHER_CTX_free(ctx);
    return 0;
  }

  int len;
  size_t done = 0;
  size_t res_offset = 0;
  while (done < clen) {
    size_t to_decrypt = clen - done;
    if (to_decrypt > kEncDecBlockSize) {
      to_decrypt = kEncDecBlockSize;
    }
    if (1 != EVP_DecryptUpdate(
        ctx,
        reinterpret_cast<unsigned char *>(res + res_offset), &len,
        data + done, to_decrypt)) {
      ERR_print_errors_fp(stderr);
      EVP_CIPHER_CTX_free(ctx);
      return 0;
    }

    done += to_decrypt;
    res_offset += len;
  }

  if (1 != EVP_DecryptFinal_ex(
      ctx,
      reinterpret_cast<unsigned char *>(res + res_offset), &len)) {
    ERR_print_errors_fp(stderr);
    EVP_CIPHER_CTX_free(ctx);
    return 0;
  }
  res_offset += len;

  EVP_CIPHER_CTX_free(ctx);

  my_assert(CiphertextLen(res_offset) == val_len);
  return res_offset;
}

inline size_t DecryptStrArray(const std::vector<std::string *> &data,
                              size_t skip_bytes, size_t val_len,
                              const Key key, char *res) {
  my_assert(!data.empty());
  EVP_CIPHER_CTX *ctx;
  size_t clen = val_len - kIvSize;

  if (!(ctx = EVP_CIPHER_CTX_new())) {
    ERR_print_errors_fp(stderr);
    EVP_CIPHER_CTX_free(ctx);
    return 0;
  }

  // Find start of ctext
  size_t ctext_start_idx = 0;
  size_t ctext_start_offset = 0;
  for (size_t i = 0; i < data.size(); ++i) {
    const auto &s = data[i];
    if (skip_bytes >= s->size()) {
      skip_bytes -= s->size();
      continue;
    }
    ctext_start_idx = i;
    ctext_start_offset = skip_bytes;
    break;
  }

  // Find start of IV
  size_t ctext_offset = 0;
  size_t iv_container_idx = 0;
  size_t iv_start_offset = 0;
  for (size_t i = ctext_start_idx; i < data.size(); ++i) {
    const auto &s = data[i];
    size_t s_offset = i == ctext_start_idx
                      ? ctext_start_offset
                      : 0;
    if (clen - ctext_offset > s->size() - s_offset) {
      ctext_offset += s->size() - s_offset;
      continue;
    }

    iv_container_idx = i;
    iv_start_offset = s_offset + clen - ctext_offset;
    break;
  }

  if (1 != EVP_DecryptInit_ex(
      ctx, kCipher(), nullptr, key.data(),
      reinterpret_cast<const unsigned char *>(data[iv_container_idx]->data() + iv_start_offset))) {
    ERR_print_errors_fp(stderr);
    EVP_CIPHER_CTX_free(ctx);
    return 0;
  }

  int len;
  ctext_offset = 0;
  size_t res_offset = 0;
  for (size_t i = ctext_start_idx; i < data.size(); ++i) {
    const auto &s = data[i];
    size_t s_offset = i == ctext_start_idx
                      ? ctext_start_offset
                      : 0;
    while (s_offset < s->size() && ctext_offset < clen) {
      size_t to_decrypt = min_(clen - ctext_offset, s->size() - s_offset);
      to_decrypt = min_(to_decrypt, kEncDecBlockSize);
      if (1 != EVP_DecryptUpdate(
          ctx,
          reinterpret_cast<unsigned char *>(res + res_offset),
          &len,
          reinterpret_cast<const unsigned char *>(s->data() + s_offset),
          to_decrypt)) {
        ERR_print_errors_fp(stderr);
        EVP_CIPHER_CTX_free(ctx);
        return 0;
      }

      ctext_offset += to_decrypt;
      s_offset += to_decrypt;
      res_offset += len;
    }
  }

  if (1 != EVP_DecryptFinal_ex(
      ctx, reinterpret_cast<unsigned char *>(res + res_offset), &len)) {
    ERR_print_errors_fp(stderr);
    EVP_CIPHER_CTX_free(ctx);
    return 0;
  }
  res_offset += len;

  EVP_CIPHER_CTX_free(ctx);

  my_assert(CiphertextLen(res_offset) == val_len);
  return res_offset;
}
} // namespace file_oram::utils

#endif //FILEORAM_UTILS_CRYPTO_H_
