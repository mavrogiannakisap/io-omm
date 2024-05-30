#include "crypto.h"

#include <cstddef>

#include "utils/assert.h"

int main() {
  auto k = file_oram::utils::GenerateKey();
  size_t len = 1UL << 33;
  auto data = new char[len];
  auto clen = file_oram::utils::CiphertextLen(len);
  auto ctext = new char[clen];
  auto decr = new char[clen];

  file_oram::utils::Encrypt(data, len, k, ctext);
  file_oram::utils::Decrypt({ctext, ctext + clen}, k, decr);

  for (size_t i = 0; i < len; ++i) {
    my_assert(data[i] == decr[i]);
  }
  return 0;
}