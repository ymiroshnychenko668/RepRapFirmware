#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include <cstring>
enum class OpenMode { read };
inline std::vector<uint8_t> fakeFileBytes;
inline bool fakeFileMissing=false, fakeFileShort=false, fakeCloseFailure=false, fakeFileClosed=false;
struct FileStore {
    size_t Length() const { return fakeFileBytes.size(); }
    int Read(uint8_t* out,size_t n) { const size_t size=n<fakeFileBytes.size()?n:fakeFileBytes.size(); std::memcpy(out,fakeFileBytes.data(),size); return fakeFileShort?0:static_cast<int>(size); }
    bool Close() { fakeFileClosed=true; return !fakeCloseFailure; }
};
inline FileStore fakeFile;
