#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace zeronote {

void SecureClear(void* ptr, std::size_t size);
bool LockMemory(void* ptr, std::size_t size);
void UnlockMemory(void* ptr, std::size_t size);

class SecureBuffer {
public:
    SecureBuffer() = default;
    explicit SecureBuffer(std::size_t size);
    ~SecureBuffer();

    SecureBuffer(const SecureBuffer&) = delete;
    SecureBuffer& operator=(const SecureBuffer&) = delete;

    SecureBuffer(SecureBuffer&& other) noexcept;
    SecureBuffer& operator=(SecureBuffer&& other) noexcept;

    std::uint8_t* data() { return bytes_.data(); }
    const std::uint8_t* data() const { return bytes_.data(); }
    std::size_t size() const { return bytes_.size(); }
    bool empty() const { return bytes_.empty(); }
    void resize(std::size_t size);
    void clear();

private:
    void Release();
    std::vector<std::uint8_t> bytes_;
    bool locked_ = false;
};

}