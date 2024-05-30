#ifndef FILEORAM_REMOTE_STORE_INTERNAL_LOCK_MANAGER_H_
#define FILEORAM_REMOTE_STORE_INTERNAL_LOCK_MANAGER_H_

#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <unordered_set>

namespace file_oram::storage::internal {

class LockManager {
 private:
  class Lock {
   private:
    size_t id_;
    LockManager &manager_;

   public:
    Lock(size_t id, LockManager &mgr) : id_(id), manager_(mgr) {}
    ~Lock() { manager_.ReleaseLock(id_); }
  };

 public:
  Lock AcquireLock(size_t id) {
    std::unique_lock<std::mutex> lock{mux_};
    while (locked_.count(id)) {
      cv_.wait(lock);
    }
    locked_.insert(id);
    return {id, *this};
  }

 private:
  mutable std::mutex mux_;
  mutable std::condition_variable cv_;
  std::unordered_set<size_t> locked_;

  void ReleaseLock(size_t id) {
    std::unique_lock<std::mutex> lock{mux_};
    locked_.erase(id);
    cv_.notify_all();
  }
};
} // namespace file_oram::storage::internal

#endif //FILEORAM_REMOTE_STORE_INTERNAL_LOCK_MANAGER_H_
