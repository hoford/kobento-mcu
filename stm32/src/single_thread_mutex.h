// The Arm GNU toolchain's libstdc++ is built single-threaded: <mutex> has no std::mutex.
// ThorVG and rccore lock one anyway. This player runs them on one thread with no RTOS, and
// interrupts never touch them, so a lock is a no-op. Force-included into every C++ file.
#pragma once
#ifdef __cplusplus
#include <bits/c++config.h>
#if !defined(_GLIBCXX_HAS_GTHREADS)
#include <mutex>
namespace std {
struct mutex {
    constexpr mutex() noexcept = default;
    mutex(const mutex&) = delete;
    mutex& operator=(const mutex&) = delete;
    void lock() {}
    bool try_lock() { return true; }
    void unlock() {}
};
struct recursive_mutex : mutex {};
}
#endif
#endif
