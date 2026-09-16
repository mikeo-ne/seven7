#pragma once
// seven7 — Engine layer: real-time primitives (spec: docs/01 ARC-T04, ARC-RT-01…03)
//
//   SpscRing<T,N>  — lock-free single-producer/single-consumer ring (parameter
//                    events UI→RT, meter frames RT→UI). Fixed capacity, no
//                    allocation after construction, acquire/release only
//                    (`seq_cst` is prohibited in audio paths, ARC-T04).
//   RcuSlot<T>     — publish an immutable snapshot from the control thread; the RT
//                    thread borrows the current pointer for one callback (ARC-RT-02).
//                    Retired snapshots are reclaimed on the control thread once the
//                    RT thread has provably moved past them.

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace s7::engine {

template <class T, std::size_t N>
class SpscRing {
  static_assert((N & (N - 1)) == 0, "SpscRing capacity must be a power of two");

public:
  bool push(const T& v) {
    const std::size_t w = write_.load(std::memory_order_relaxed);
    const std::size_t next = (w + 1) & (N - 1);
    if (next == read_.load(std::memory_order_acquire)) return false;  // full
    buf_[w] = v;
    write_.store(next, std::memory_order_release);
    return true;
  }

  bool pop(T& out) {
    const std::size_t r = read_.load(std::memory_order_relaxed);
    if (r == write_.load(std::memory_order_acquire)) return false;  // empty
    out = buf_[r];
    read_.store((r + 1) & (N - 1), std::memory_order_release);
    return true;
  }

  std::size_t size_approx() const {
    const std::size_t w = write_.load(std::memory_order_acquire);
    const std::size_t r = read_.load(std::memory_order_acquire);
    return (w + N - r) & (N - 1);
  }

private:
  std::array<T, N> buf_{};
  alignas(64) std::atomic<std::size_t> write_{0};
  alignas(64) std::atomic<std::size_t> read_{0};
};

/// Reclamation argument (why this is safe without locks or hazard pointers):
///
///   publish():    current.exchange(new) ; g' = ++generation ; retire(old, g')
///   rt_acquire(): g = generation.load(acq) ; rt_seen.store(g, rel) ; p = current.load(acq)
///
/// Because the exchange is sequenced before the generation increment on the control
/// side, and the RT loads generation *before* current (acquire prevents reordering),
/// any pointer the RT borrows after publishing rt_seen = g was replaced at some
/// generation r ≥ g + 1. Therefore every snapshot retired at r ≤ g is unreachable by
/// the RT thread and can be deleted as soon as the control thread observes rt_seen ≥ r.
/// rt_release() republishes the latest generation after the last use in a callback.
template <class T>
class RcuSlot {
public:
  RcuSlot() = default;
  ~RcuSlot() {
    delete current_.load(std::memory_order_acquire);
    for (auto& r : retired_) delete r.ptr;
  }
  RcuSlot(const RcuSlot&) = delete;
  RcuSlot& operator=(const RcuSlot&) = delete;

  /// Control thread: publish a new snapshot; the old one is retired.
  void publish(std::unique_ptr<T> next) {
    T* old = current_.exchange(next.release(), std::memory_order_acq_rel);
    const std::uint64_t gen = generation_.fetch_add(1, std::memory_order_acq_rel) + 1;
    if (old) retired_.push_back({old, gen});
    reclaim();
  }

  /// RT thread: borrow the current snapshot for the duration of one callback.
  /// Call `rt_release()` after the last use in that callback.
  const T* rt_acquire() {
    const std::uint64_t g = generation_.load(std::memory_order_acquire);
    rt_seen_.store(g, std::memory_order_release);
    return current_.load(std::memory_order_acquire);
  }
  void rt_release() { rt_seen_.store(generation_.load(std::memory_order_acquire), std::memory_order_release); }

  /// Control thread: delete snapshots the RT thread can no longer be reading.
  void reclaim() {
    const std::uint64_t seen = rt_seen_.load(std::memory_order_acquire);
    std::size_t keep = 0;
    for (std::size_t i = 0; i < retired_.size(); ++i) {
      if (seen >= retired_[i].gen) delete retired_[i].ptr;
      else retired_[keep++] = retired_[i];
    }
    retired_.resize(keep);
  }

  const T* control_peek() const { return current_.load(std::memory_order_acquire); }
  std::size_t retired_count() const { return retired_.size(); }

private:
  struct Retired { T* ptr; std::uint64_t gen; };
  std::atomic<T*> current_{nullptr};
  std::atomic<std::uint64_t> generation_{0};
  alignas(64) std::atomic<std::uint64_t> rt_seen_{0};
  std::vector<Retired> retired_;  // control-thread only
};

}  // namespace s7::engine
