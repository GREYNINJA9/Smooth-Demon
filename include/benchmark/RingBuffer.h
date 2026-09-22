#pragma once

#include <array>
#include <cstddef>

namespace smoothdemon {

    // Fixed-capacity, zero-allocation circular buffer
    // Push() is O(1) and never allocates once full oldest sample is overwritten
    // Indexing: [0] = oldest retained sample, [Size()-1] = newest
    template <typename T, std::size_t Capacity>
    class RingBuffer {
    public:
        static_assert(Capacity > 0, "RingBuffer capacity must be > 0");

        void Push(const T& value) noexcept {
            data_[head_] = value;
            head_ = (head_ + 1) % Capacity;
            if (count_ < Capacity) ++count_;
        }

        std::size_t Size() const noexcept { return count_; }
        std::size_t CapacityValue() const noexcept { return Capacity; }
        bool Empty() const noexcept { return count_ == 0; }

        void Clear() noexcept {
            head_ = 0;
            count_ = 0;
        }

        // 0 = oldest retained sample; Size()-1 = newest
        const T& operator[](std::size_t i) const noexcept {
            if (count_ < Capacity) {
                return data_[i];
            }
            return data_[(head_ + i) % Capacity];
        }

        // Append all active samples to dest (oldest -> newest)
        template <typename Container>
        void CopyTo(Container& dest) const {
            for (std::size_t i = 0; i < count_; ++i) {
                dest.push_back((*this)[i]);
            }
        }

    private:
        std::array<T, Capacity> data_{};
        std::size_t head_{0};
        std::size_t count_{0};
    };

}
