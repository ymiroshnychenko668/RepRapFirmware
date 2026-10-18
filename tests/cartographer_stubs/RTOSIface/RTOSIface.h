#pragma once
#include <mutex>
#include <cstdint>
class Mutex;
inline Mutex* fakeCreatedMutex=nullptr;
class Mutex {
    std::recursive_mutex mutex;
    friend class MutexLocker;
public:
    static constexpr uint32_t TimeoutUnlimited=UINT32_MAX;
    void Create(const char*) noexcept { fakeCreatedMutex=this; }
};
class MutexLocker {
    std::unique_lock<std::recursive_mutex> lock;
public:
    explicit MutexLocker(Mutex& mutex,uint32_t timeout=Mutex::TimeoutUnlimited) : lock(mutex.mutex,std::defer_lock) {
        if (timeout==0) lock.try_lock(); else lock.lock();
    }
    bool IsAcquired() const noexcept { return lock.owns_lock(); }
    void Release() noexcept { if(lock.owns_lock())lock.unlock(); }
};
