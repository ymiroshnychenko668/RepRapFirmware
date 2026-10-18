#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include <cstring>
#if defined(CARTOGRAPHER_REGISTER_HOOKS)
#include <atomic>
#include <mutex>
#include <condition_variable>
inline std::atomic<bool> fakeBlockFileRead{false}, fakeFileReadEntered{false}, fakeContinueFileRead{false};
inline std::mutex fakeFileReadMutex;
inline std::condition_variable fakeFileReadCondition;
#endif
enum class OpenMode { read };
inline std::vector<uint8_t> fakeFileBytes;
inline bool fakeFileMissing=false, fakeFileShort=false, fakeCloseFailure=false, fakeFileClosed=false;
struct FileStore {
    size_t Length() const { return fakeFileBytes.size(); }
    int Read(uint8_t* out,size_t n) {
#if defined(CARTOGRAPHER_REGISTER_HOOKS)
        if (fakeBlockFileRead.load()) {
            std::unique_lock<std::mutex> lock(fakeFileReadMutex);
            fakeFileReadEntered.store(true); fakeFileReadCondition.notify_all();
            fakeFileReadCondition.wait(lock,[] { return fakeContinueFileRead.load(); });
        }
#endif
        const size_t size=n<fakeFileBytes.size()?n:fakeFileBytes.size(); std::memcpy(out,fakeFileBytes.data(),size); return fakeFileShort?0:static_cast<int>(size); }
    bool Close() { fakeFileClosed=true; return !fakeCloseFailure; }
};
inline FileStore fakeFile;
