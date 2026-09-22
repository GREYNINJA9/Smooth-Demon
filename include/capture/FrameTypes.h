#pragma once

#include "types.hpp"

#include <chrono>
#include <cstddef>
#include <functional>

namespace smoothdemon {

    enum class PixelFormat {
        BGRA,
        RGBA,
        BGRX,
        RGBX,
        Unknown,
    };

    using FrameDoneCallback = std::function<void()>;

    struct DmaBufPlane {
        std::size_t offset{0};
        std::size_t pitch{0};
    };

    struct DmaBufFrame {
        u32 width{0};
        u32 height{0};
        std::chrono::nanoseconds pts{};
        u64 drm_format{0};

        // DRM PRIME object
        int         fd{-1};
        std::size_t total_size{0};
        u64         modifier{0};

        u32 plane_count{0};
        DmaBufPlane planes[4]{};

        FrameDoneCallback on_frame_done;

        DmaBufFrame() = default;
        DmaBufFrame(const DmaBufFrame&)            = delete;
        DmaBufFrame& operator=(const DmaBufFrame&) = delete;
        DmaBufFrame(DmaBufFrame&&) noexcept        = default;
        DmaBufFrame& operator=(DmaBufFrame&&) noexcept = default;

        ~DmaBufFrame() noexcept {
            if (on_frame_done) on_frame_done();
        }
    };

    struct MemoryFrame {
        u32 width{0};
        u32 height{0};
        std::chrono::nanoseconds pts{};
        PixelFormat format{PixelFormat::Unknown};

        void*       memory{nullptr};
        std::size_t stride{0};
        std::size_t size{0};
        std::size_t offset{0};

        FrameDoneCallback on_frame_done;

        MemoryFrame() = default;
        MemoryFrame(const MemoryFrame&)            = delete;
        MemoryFrame& operator=(const MemoryFrame&) = delete;
        MemoryFrame(MemoryFrame&&) noexcept        = default;
        MemoryFrame& operator=(MemoryFrame&&) noexcept = default;

        ~MemoryFrame() noexcept {
            if (on_frame_done) on_frame_done();
        }
    };

}
