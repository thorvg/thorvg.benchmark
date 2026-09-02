#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>

namespace bench {
namespace sha256_detail {

inline constexpr std::array<uint32_t, 64> kRoundConstants = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

inline uint32_t rotate_right(uint32_t value, uint32_t count) {
  return (value >> count) | (value << (32u - count));
}

class Hasher {
public:
  void update(const uint8_t *data, size_t size) {
    total_bytes_ += static_cast<uint64_t>(size);

    if (buffer_size_ != 0) {
      while (size != 0 && buffer_size_ < buffer_.size()) {
        buffer_[buffer_size_++] = *data++;
        --size;
      }
      if (buffer_size_ == buffer_.size()) {
        transform(buffer_.data());
        buffer_size_ = 0;
      }
    }

    while (size >= buffer_.size()) {
      transform(data);
      data += buffer_.size();
      size -= buffer_.size();
    }
    while (size != 0) {
      buffer_[buffer_size_++] = *data++;
      --size;
    }
  }

  std::string finish() {
    const uint64_t bit_length = total_bytes_ * 8u;
    buffer_[buffer_size_++] = 0x80u;
    if (buffer_size_ > 56u) {
      while (buffer_size_ < buffer_.size()) buffer_[buffer_size_++] = 0u;
      transform(buffer_.data());
      buffer_size_ = 0;
    }
    while (buffer_size_ < 56u) buffer_[buffer_size_++] = 0u;
    for (uint32_t index = 0; index < 8u; ++index) {
      const uint32_t shift = (7u - index) * 8u;
      buffer_[buffer_size_++] =
          static_cast<uint8_t>((bit_length >> shift) & 0xffu);
    }
    transform(buffer_.data());

    constexpr char kHex[] = "0123456789abcdef";
    std::string result;
    result.reserve(64);
    for (uint32_t word : state_) {
      for (int shift = 28; shift >= 0; shift -= 4) {
        result.push_back(kHex[(word >> shift) & 0x0fu]);
      }
    }
    return result;
  }

private:
  void transform(const uint8_t *block) {
    std::array<uint32_t, 64> words{};
    for (size_t index = 0; index < 16; ++index) {
      const size_t offset = index * 4;
      words[index] = (static_cast<uint32_t>(block[offset]) << 24u) |
                     (static_cast<uint32_t>(block[offset + 1]) << 16u) |
                     (static_cast<uint32_t>(block[offset + 2]) << 8u) |
                     static_cast<uint32_t>(block[offset + 3]);
    }
    for (size_t index = 16; index < words.size(); ++index) {
      const uint32_t previous = words[index - 15];
      const uint32_t sigma0 = rotate_right(previous, 7u) ^
                              rotate_right(previous, 18u) ^ (previous >> 3u);
      const uint32_t recent = words[index - 2];
      const uint32_t sigma1 = rotate_right(recent, 17u) ^
                              rotate_right(recent, 19u) ^ (recent >> 10u);
      words[index] =
          words[index - 16] + sigma0 + words[index - 7] + sigma1;
    }

    uint32_t a = state_[0];
    uint32_t b = state_[1];
    uint32_t c = state_[2];
    uint32_t d = state_[3];
    uint32_t e = state_[4];
    uint32_t f = state_[5];
    uint32_t g = state_[6];
    uint32_t h = state_[7];
    for (size_t index = 0; index < words.size(); ++index) {
      const uint32_t sum1 =
          rotate_right(e, 6u) ^ rotate_right(e, 11u) ^ rotate_right(e, 25u);
      const uint32_t choice = (e & f) ^ (~e & g);
      const uint32_t temp1 =
          h + sum1 + choice + kRoundConstants[index] + words[index];
      const uint32_t sum0 =
          rotate_right(a, 2u) ^ rotate_right(a, 13u) ^ rotate_right(a, 22u);
      const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
      const uint32_t temp2 = sum0 + majority;
      h = g;
      g = f;
      f = e;
      e = d + temp1;
      d = c;
      c = b;
      b = a;
      a = temp1 + temp2;
    }

    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
  }

  std::array<uint32_t, 8> state_ = {
      0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
      0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
  };
  std::array<uint8_t, 64> buffer_{};
  uint64_t total_bytes_ = 0;
  size_t buffer_size_ = 0;
};

} // namespace sha256_detail

inline std::string sha256_bytes(const uint8_t *data, size_t size) {
  sha256_detail::Hasher hasher;
  hasher.update(data, size);
  return hasher.finish();
}

inline std::string sha256_file(const std::string &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input.is_open()) return {};

  sha256_detail::Hasher hasher;
  std::array<char, 64 * 1024> buffer{};
  while (input) {
    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const std::streamsize bytes_read = input.gcount();
    if (bytes_read > 0) {
      hasher.update(reinterpret_cast<const uint8_t *>(buffer.data()),
                    static_cast<size_t>(bytes_read));
    }
  }
  if (!input.eof()) return {};
  return std::string("sha256:") + hasher.finish();
}

} // namespace bench
