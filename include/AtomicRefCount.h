#pragma once

#include <atomic>
#include <cstddef>

// Intrusive atomic reference count — the building block behind shared_ptr and
// basically every reference-counted lock-free node.
//
// Usage: inherit from RefCounted<T> (CRTP), then use add_ref / release.
// When the count hits zero, release() calls delete this.
//
// Why not just use shared_ptr?
// - shared_ptr's control block is a separate allocation — two pointers to chase.
// - This is inline in the object — one allocation, one pointer.
// - Also useful for understanding how the memory ordering actually works here.
//
// The important subtlety: add_ref uses relaxed ordering but release uses acq_rel.
// On the decrement: we need acquire to ensure all reads of the object happen
// before the delete. We need release to ensure our own writes (from wherever we
// had the object) are flushed before we touch the count. Without acq_rel on the
// last decrement you can delete the object while another thread's writes are
// still in flight. This is the classic pattern — std::shared_ptr does the same thing.
template <typename T>
class RefCounted {
public:
    RefCounted() : ref_count_(1) {}

    void add_ref() {
        // Relaxed is fine here — if someone has a valid pointer to call add_ref
        // they must have gotten it from someone who already held a reference,
        // so the synchronization already happened on that path.
        ref_count_.fetch_add(1, std::memory_order_relaxed);
    }

    void release() {
        if (ref_count_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            // We were the last reference.
            // The acq_rel above ensures:
            //   - acquire: we see all previous writes before this store (other threads' releases)
            //   - release: our own writes are visible before the delete
            delete static_cast<T*>(this);
        }
    }

    size_t use_count() const {
        return ref_count_.load(std::memory_order_relaxed);
    }

protected:
    ~RefCounted() = default;

private:
    std::atomic<size_t> ref_count_;
};

// Helper so you don't have to write "new" everywhere.
// Starts at refcount=1. Caller owns that reference and must call release().
template <typename T, typename... Args>
T* make_ref_counted(Args&&... args) {
    return new T(std::forward<Args>(args)...);
}
