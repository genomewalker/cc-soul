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

        // Reject empty files (mmap with size 0 is undefined behavior)
        if (size_ == 0) {
            close();
            return false;
        }

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

    // Create new file (truncates if exists) - use only for fresh creation
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

    // Atomically create new file - fails if file already exists (safe for concurrency)
    // Returns: true if created, false if exists or error
    bool create_exclusive(const std::string& path, size_t size) {
        fd_ = ::open(path.c_str(), O_RDWR | O_CREAT | O_EXCL, 0644);
        if (fd_ < 0) {
            return false;  // File exists or permission error
        }

        if (ftruncate(fd_, size) < 0) {
            close();
            ::unlink(path.c_str());  // Clean up failed creation
            return false;
        }
        size_ = size;

        data_ = mmap(nullptr, size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
        if (data_ == MAP_FAILED) {
            data_ = nullptr;
            close();
            ::unlink(path.c_str());
            return false;
        }

        return true;
    }

    // Open existing or create new (safe for concurrency)
    // Grows file if it exists but is smaller than requested size
    bool open_or_create(const std::string& path, size_t min_size) {
        // Try to open existing first
        fd_ = ::open(path.c_str(), O_RDWR);
        if (fd_ >= 0) {
            struct stat st;
            if (fstat(fd_, &st) == 0 && st.st_size > 0) {
                // File exists with content - use it
                size_ = st.st_size;
                // Grow if needed
                if (static_cast<size_t>(st.st_size) < min_size) {
                    if (ftruncate(fd_, min_size) == 0) {
                        size_ = min_size;
                    }
                }
                data_ = mmap(nullptr, size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
                if (data_ != MAP_FAILED) {
                    return true;
                }
                data_ = nullptr;
            }
            ::close(fd_);
            fd_ = -1;
        }

        // Try atomic create
        return create_exclusive(path, min_size);
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

    // Resize the mapped region (only for writable files)
    bool resize(size_t new_size) {
        if (fd_ < 0 || new_size == 0) return false;
        if (new_size == size_) return true;

        // Sync before resize
        sync();

        // Unmap current region
        if (data_) {
            munmap(data_, size_);
            data_ = nullptr;
        }

        // Extend file
        if (ftruncate(fd_, new_size) < 0) {
            return false;
        }

        // Remap with new size
        data_ = mmap(nullptr, new_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
        if (data_ == MAP_FAILED) {
            data_ = nullptr;
            return false;
        }

        size_ = new_size;
        return true;
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

// ═══════════════════════════════════════════════════════════════════════════
// Grow Lock: Cross-process exclusive lock for resize operations
// Uses fcntl (NFS-compatible) instead of flock
// ═══════════════════════════════════════════════════════════════════════════
class GrowLock {
public:
    explicit GrowLock(const std::string& base_path)
        : lock_path_(base_path + ".grow.lock"), fd_(-1), locked_(false) {}

    ~GrowLock() { unlock(); }

    // Non-copyable, non-movable
    GrowLock(const GrowLock&) = delete;
    GrowLock& operator=(const GrowLock&) = delete;

    bool lock_exclusive() {
        fd_ = ::open(lock_path_.c_str(), O_RDWR | O_CREAT, 0644);
        if (fd_ < 0) return false;

        struct flock fl;
        fl.l_type = F_WRLCK;
        fl.l_whence = SEEK_SET;
        fl.l_start = 0;
        fl.l_len = 0;  // Lock entire file

        // Non-blocking try
        if (fcntl(fd_, F_SETLK, &fl) < 0) {
            ::close(fd_);
            fd_ = -1;
            return false;
        }

        locked_ = true;
        return true;
    }

    void unlock() {
        if (locked_ && fd_ >= 0) {
            struct flock fl;
            fl.l_type = F_UNLCK;
            fl.l_whence = SEEK_SET;
            fl.l_start = 0;
            fl.l_len = 0;
            fcntl(fd_, F_SETLK, &fl);
            locked_ = false;
        }
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

    bool is_locked() const { return locked_; }

private:
    std::string lock_path_;
    int fd_;
    bool locked_;
};

// Helper: Extend file without affecting existing mmaps
// Opens file, extends, closes. Does not mmap.
inline bool extend_file(const std::string& path, size_t new_size) {
    int fd = ::open(path.c_str(), O_RDWR);
    if (fd < 0) return false;

    struct stat st;
    if (fstat(fd, &st) < 0) {
        ::close(fd);
        return false;
    }

    // Only extend, never shrink
    if (static_cast<size_t>(st.st_size) >= new_size) {
        ::close(fd);
        return true;  // Already large enough
    }

    if (ftruncate(fd, new_size) < 0) {
        ::close(fd);
        return false;
    }

    ::close(fd);
    return true;
}

} // namespace chitta
