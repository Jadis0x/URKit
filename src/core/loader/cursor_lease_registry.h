#pragma once

#include <cstddef>
#include <unordered_map>

class CursorLeaseRegistry {
  public:
    // Used for per-frame UI capture state without accumulating leases.
    bool Set(void *owner, bool active) {
        if (!owner)
            return false;
        if (active) {
            owners_[owner] = 1;
            return true;
        }
        owners_.erase(owner);
        return true;
    }

    bool Acquire(void *owner) {
        if (!owner)
            return false;
        ++owners_[owner];
        return true;
    }

    bool Release(void *owner) {
        const auto found = owners_.find(owner);
        if (!owner || found == owners_.end())
            return false;
        if (--found->second == 0)
            owners_.erase(found);
        return true;
    }

    std::size_t ReleaseOwner(void *owner) {
        const auto found = owners_.find(owner);
        if (!owner || found == owners_.end())
            return 0;
        const std::size_t released = found->second;
        owners_.erase(found);
        return released;
    }

    bool AnyOpen() const {
        return !owners_.empty();
    }

    std::size_t OwnerCount() const {
        return owners_.size();
    }

    std::size_t LeaseCount(void *owner) const {
        const auto found = owners_.find(owner);
        return found == owners_.end() ? 0 : found->second;
    }

    void Clear() {
        owners_.clear();
    }

  private:
    std::unordered_map<void *, std::size_t> owners_;
};
