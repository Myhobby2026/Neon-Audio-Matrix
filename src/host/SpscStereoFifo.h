#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

class SpscStereoFifo final
{
public:
    static constexpr uint64_t capacity = 1u << 15;

    void reset() noexcept
    {
        readPosition_.store(0, std::memory_order_relaxed);
        writePosition_.store(0, std::memory_order_relaxed);
    }

    void push(const float* left, const float* right, int frameCount) noexcept
    {
        if (left == nullptr || right == nullptr || frameCount <= 0)
            return;

        auto write = writePosition_.load(std::memory_order_relaxed);
        const auto read = readPosition_.load(std::memory_order_acquire);
        auto accepted = 0;

        for (; accepted < frameCount && write - read < capacity; ++accepted, ++write)
        {
            const auto slot = static_cast<std::size_t>(write & (capacity - 1u));
            left_[slot] = left[accepted];
            right_[slot] = right[accepted];
        }

        writePosition_.store(write, std::memory_order_release);
        droppedFrames_.fetch_add(static_cast<uint64_t>(frameCount - accepted), std::memory_order_relaxed);
    }

    int pop(float* left, float* right, int requestedFrames) noexcept
    {
        if (left == nullptr || right == nullptr || requestedFrames <= 0)
            return 0;

        auto read = readPosition_.load(std::memory_order_relaxed);
        const auto write = writePosition_.load(std::memory_order_acquire);
        const auto available = static_cast<int>(std::min<uint64_t>(write - read,
                                            static_cast<uint64_t>(requestedFrames)));

        for (int i = 0; i < available; ++i, ++read)
        {
            const auto slot = static_cast<std::size_t>(read & (capacity - 1u));
            left[i] = left_[slot];
            right[i] = right_[slot];
        }

        std::fill(left + available, left + requestedFrames, 0.0f);
        std::fill(right + available, right + requestedFrames, 0.0f);
        readPosition_.store(read, std::memory_order_release);
        underflowFrames_.fetch_add(static_cast<uint64_t>(requestedFrames - available), std::memory_order_relaxed);
        return available;
    }

    uint64_t getDroppedFrameCount() const noexcept { return droppedFrames_.load(std::memory_order_relaxed); }
    uint64_t getUnderflowFrameCount() const noexcept { return underflowFrames_.load(std::memory_order_relaxed); }

private:
    alignas(64) std::array<float, capacity> left_ {};
    alignas(64) std::array<float, capacity> right_ {};
    alignas(64) std::atomic<uint64_t> readPosition_ { 0 };
    alignas(64) std::atomic<uint64_t> writePosition_ { 0 };
    std::atomic<uint64_t> droppedFrames_ { 0 };
    std::atomic<uint64_t> underflowFrames_ { 0 };
};
