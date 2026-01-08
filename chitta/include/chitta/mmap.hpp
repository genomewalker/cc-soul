#pragma once
// Memory-mapped region utility class
//
// Extracted to break circular dependency between storage.hpp and connection_pool.hpp

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string>
#include <cstddef>

namespace chitta {

// Memory-mapped region
class MappedRegion {
public:
    MappedRegion() = default;

    bool open(const std::string& path, bool readonly = true) {
        int flags = readonly ? O_RDONLY : O_RDWR;
        fd_ = ::open(path.c_str(), flags);
        if (fd_ < 0) return false;

        struct stat st;
        if (fstat(fd_, &st) < 0) {
            close();
            return false;
        }
        size_ = st.st_size;

        int prot = readonly ? PROT_READ : (PROT_READ | PROT_WRITE);
        data_ = mmap(nullptr, size_, prot, MAP_SHARED, fd_, 0);
        if (data_ == MAP_FAILED) {
            data_ = nullptr;
            close();
            return false;
        }

        // Advise sequential read for initial load
        madvise(data_, size_, MADV_SEQUENTIAL);
        return true;
    }

    bool create(const std::string& path, size_t size) {
        fd_ = ::open(path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
        if (fd_ < 0) return false;

        if (ftruncate(fd_, size) < 0) {
            close();
            return false;
        }
        size_ = size;

        data_ = mmap(nullptr, size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
        if (data_ == MAP_FAILED) {
            data_ = nullptr;
            close();
            return false;
        }

        return true;
    }

    void close() {
        if (data_) {
            munmap(data_, size_);
            data_ = nullptr;
        }
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
        size_ = 0;
    }

    void sync() {
        if (data_) {
            msync(data_, size_, MS_SYNC);
        }
    }

    ~MappedRegion() { close(); }

    // Non-copyable
    MappedRegion(const MappedRegion&) = delete;
    MappedRegion& operator=(const MappedRegion&) = delete;

    // Movable
    MappedRegion(MappedRegion&& o) noexcept
        : data_(o.data_), size_(o.size_), fd_(o.fd_) {
        o.data_ = nullptr;
        o.size_ = 0;
        o.fd_ = -1;
    }

    MappedRegion& operator=(MappedRegion&& o) noexcept {
        if (this != &o) {
            close();
            data_ = o.data_;
            size_ = o.size_;
            fd_ = o.fd_;
            o.data_ = nullptr;
            o.size_ = 0;
            o.fd_ = -1;
        }
        return *this;
    }

    void* data() { return data_; }
    const void* data() const { return data_; }
    size_t size() const { return size_; }
    bool valid() const { return data_ != nullptr; }

    template<typename T>
    T* as() { return static_cast<T*>(data_); }

    template<typename T>
    const T* as() const { return static_cast<const T*>(data_); }

    template<typename T>
    T* at(size_t offset) {
        return reinterpret_cast<T*>(static_cast<char*>(data_) + offset);
    }

    template<typename T>
    const T* at(size_t offset) const {
        return reinterpret_cast<const T*>(static_cast<const char*>(data_) + offset);
    }

private:
    void* data_ = nullptr;
    size_t size_ = 0;
    int fd_ = -1;
};

} // namespace chitta
