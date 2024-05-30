#ifndef FILEORAM_UTILS_RLIMIT_H_
#define FILEORAM_UTILS_RLIMIT_H_

#include <sys/resource.h>
#include <cstdio>
#include <cstdlib>
#include <cerrno>

using Limit = rlimit;
using LimitType = rlim_t; // u64 on mac, u32 on linux
static const LimitType kNofileLimit = 65535;

static inline Limit GetNoFile() {
  rlimit limit{0, 0};
  if (getrlimit(RLIMIT_NOFILE, &limit) != 0) {
    printf("getrlimit() failed with errno=%d\n", errno);
  }
  return limit;
}

static inline bool SetNoFile(LimitType n = kNofileLimit) {
  rlimit limit;
  limit.rlim_cur = n;
  limit.rlim_max = n;
  if (setrlimit(RLIMIT_NOFILE, &limit) != 0) {
    printf("setrlimit() failed with errno=%d\n", errno);
    return false;
  }
  return true;
}

static inline bool UpdateNoFile(LimitType n = kNofileLimit) {
  auto curr = GetNoFile();
  if (curr.rlim_cur == 0) {
    return false;
  }
  if (curr.rlim_cur >= n) {
    printf("Old Nofile: %llu; not changing\n", uint64_t(curr.rlim_cur));
    return true;
  }
  return SetNoFile(n);
}

#endif //FILEORAM_UTILS_RLIMIT_H_
