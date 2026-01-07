#pragma once

#include <cstdint>

namespace bench {

/**
 * PCG32 Random Number Generator
 * 
 * A minimal, fast, statistically good PRNG with deterministic behavior.
 * Algorithm: PCG-XSH-RR (permuted congruential generator)
 * Reference: https://www.pcg-random.org/
 */
class PCG32 {
public:
    static constexpr uint64_t DEFAULT_SEED = 12345ULL;
    static constexpr uint64_t DEFAULT_STREAM = 1ULL;

    explicit PCG32(uint64_t seed = DEFAULT_SEED, uint64_t stream = DEFAULT_STREAM) {
        seed_with(seed, stream);
    }

    void seed_with(uint64_t seed, uint64_t stream = DEFAULT_STREAM) {
        state_ = 0U;
        inc_ = (stream << 1U) | 1U;
        next_u32();
        state_ += seed;
        next_u32();
    }

    /// Generate a random 32-bit unsigned integer
    uint32_t next_u32() {
        uint64_t old_state = state_;
        // Advance internal state
        state_ = old_state * 6364136223846793005ULL + inc_;
        // Calculate output function (XSH-RR)
        uint32_t xorshifted = static_cast<uint32_t>(((old_state >> 18U) ^ old_state) >> 27U);
        uint32_t rot = static_cast<uint32_t>(old_state >> 59U);
        return (xorshifted >> rot) | (xorshifted << ((~rot + 1U) & 31U));
    }

    /// Generate a random float in the range [0.0, 1.0)
    float next_float_01() {
        return static_cast<float>(next_u32()) / static_cast<float>(0xFFFFFFFFU);
    }

    /// Generate a random float in the range [min, max)
    float next_float(float min_val, float max_val) {
        return min_val + next_float_01() * (max_val - min_val);
    }

    /// Generate a random integer in the range [min, max] inclusive
    int32_t next_int(int32_t min_val, int32_t max_val) {
        if (min_val > max_val) {
            return min_val;
        }
        uint32_t range = static_cast<uint32_t>(max_val - min_val + 1);
        return min_val + static_cast<int32_t>(next_u32() % range);
    }

    /// Generate a random uint8_t in the range [min, max] inclusive
    uint8_t next_u8(uint8_t min_val, uint8_t max_val) {
        if (min_val > max_val) {
            return min_val;
        }
        uint32_t range = static_cast<uint32_t>(max_val - min_val + 1);
        return static_cast<uint8_t>(min_val + (next_u32() % range));
    }

private:
    uint64_t state_;
    uint64_t inc_;
};

} // namespace bench
