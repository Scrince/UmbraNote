#include <zeronote/secure_memory.h>

#include <cstring>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace zeronote {

void SecureClear(void* ptr, std::size_t size) {
    if (!ptr || size == 0) return;
#ifdef _WIN32
    SecureZeroMemory(ptr, size);
#else
    volatile unsigned char* p = static_cast<volatile unsigned char*>(ptr);
    while (size--) {
        *p++ = 0;
    }
#endif
}

bool LockMemory(void* ptr, std::size_t size) {
    if (!ptr || size == 0) return false;
#ifdef _WIN32
    return VirtualLock(ptr, size) != 0;
#else
    return mlock(ptr, size) == 0;
#endif
}

void UnlockMemory(void* ptr, std::size_t size) {
    if (!ptr || size == 0) return;
#ifdef _WIN32
    VirtualUnlock(ptr, size);
#else
    munlock(ptr, size);
#endif
}

SecureBuffer::SecureBuffer(std::size_t size) {
    resize(size);
}

SecureBuffer::~SecureBuffer() {
    Release();
}

SecureBuffer::SecureBuffer(SecureBuffer&& other) noexcept
    : bytes_(std::move(other.bytes_)), locked_(other.locked_) {
    other.locked_ = false;
}

SecureBuffer& SecureBuffer::operator=(SecureBuffer&& other) noexcept {
    if (this != &other) {
        Release();
        bytes_ = std::move(other.bytes_);
        locked_ = other.locked_;
        other.locked_ = false;
    }
    return *this;
}

void SecureBuffer::resize(std::size_t size) {
    if (locked_ && !bytes_.empty()) {
        UnlockMemory(bytes_.data(), bytes_.size());
        locked_ = false;
    }
    if (!bytes_.empty()) {
        SecureClear(bytes_.data(), bytes_.size());
    }
    bytes_.resize(size);
    
    if (!bytes_.empty()) {
        locked_ = LockMemory(bytes_.data(), bytes_.size());
    }
}

void SecureBuffer::clear() {
    resize(0);
}

void SecureBuffer::Release() {
    if (!bytes_.empty()) {
        if (locked_) {
            UnlockMemory(bytes_.data(), bytes_.size());
            locked_ = false;
        }
        SecureClear(bytes_.data(), bytes_.size());
        bytes_.clear();
    }
}

}