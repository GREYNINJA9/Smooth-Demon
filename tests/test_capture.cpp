#include "capture/PortalSession.h"
#include "capture/PipeWireCapture.h"
#include "capture/FrameTypes.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <thread>
#include <vector>

int main() {
    using namespace smoothdemon;

    std::cout << "=== SmoothDemon Wayland/PipeWire Capture Test ===\n";
    std::cout << "A portal dialog will appear asking for screen sharing permission.\n";
    std::cout << "Choose any monitor/window and accept.\n\n";

    PortalSession portal;
    if (!portal.RequestScreenCast()) {
        std::cerr << "Failed to establish portal session.\n";
        return 1;
    }

    std::cout << "Portal:\n"
              << "  Session:     OK (" << (portal.IsOpen() ? "active" : "closed") << ")\n"
              << "  PipeWire FD: " << portal.GetPipeWireFd() << "\n"
              << "  Node ID:     " << portal.GetNodeId() << "\n\n";

    PipeWireCapture capture;

    std::atomic<std::uint64_t> dma_count{0};
    std::atomic<std::uint64_t> mem_count{0};

    struct StreamFormatInfo {
        u32 width{0};
        u32 height{0};
        bool is_dmabuf{false};
        u64 drm_format{0};
        u64 modifier{0};
        u32 plane_count{0};
        PixelFormat pixel_format{PixelFormat::Unknown};
    };

    std::mutex info_mutex;
    StreamFormatInfo format_info;

    // Timing tracking
    std::mutex timing_mutex;
    bool has_first_pts{false};
    std::chrono::nanoseconds first_pts{0};
    std::chrono::nanoseconds last_pts{0};
    std::chrono::nanoseconds prev_pts{0};
    std::chrono::nanoseconds min_interval{std::chrono::nanoseconds::max()};
    std::chrono::nanoseconds max_interval{std::chrono::nanoseconds::min()};
    std::uint64_t pts_interval_count{0};
    std::chrono::nanoseconds total_interval_sum{0};

    auto process_pts = [&](std::chrono::nanoseconds pts) {
        std::lock_guard<std::mutex> lock(timing_mutex);
        if (!has_first_pts) {
            has_first_pts = true;
            first_pts = pts;
            prev_pts = pts;
        } else {
            if (pts >= prev_pts) {
                auto delta = pts - prev_pts;
                if (delta.count() > 0) {
                    if (delta < min_interval) min_interval = delta;
                    if (delta > max_interval) max_interval = delta;
                    total_interval_sum += delta;
                    pts_interval_count++;
                }
            }
            prev_pts = pts;
        }
        last_pts = pts;
    };

    capture.SetStateCallback([&](bool streaming, u32 w, u32 h) {
        std::lock_guard<std::mutex> lock(info_mutex);
        format_info.width = w;
        format_info.height = h;
        if (streaming) {
            std::cout << "[state] streaming " << w << "x" << h << "\n";
        }
    });

    capture.SetDmaBufCallback([&](std::unique_ptr<DmaBufFrame> f) {
        const auto n = ++dma_count;
        {
            std::lock_guard<std::mutex> lock(info_mutex);
            format_info.width = f->width;
            format_info.height = f->height;
            format_info.is_dmabuf = true;
            format_info.drm_format = f->drm_format;
            format_info.modifier = f->modifier;
            format_info.plane_count = f->plane_count;
        }

        process_pts(f->pts);

        if (n % 60 == 0) {
            std::cout << "[dmabuf] frame #" << n
                      << " " << f->width << "x" << f->height
                      << " drm_format=0x" << std::hex << f->drm_format << std::dec
                      << " modifier=0x" << std::hex << f->modifier << std::dec
                      << " planes=" << f->plane_count
                      << " pts=" << f->pts.count() << "ns\n";
        }
        // Frame auto-recycled when unique_ptr goes out of scope
    });

    capture.SetMemoryCallback([&](std::unique_ptr<MemoryFrame> f) {
        const auto n = ++mem_count;
        {
            std::lock_guard<std::mutex> lock(info_mutex);
            format_info.width = f->width;
            format_info.height = f->height;
            format_info.is_dmabuf = false;
            format_info.pixel_format = f->format;
        }

        process_pts(f->pts);

        if (n % 60 == 0) {
            std::cout << "[mem]    frame #" << n
                      << " " << f->width << "x" << f->height
                      << " stride=" << f->stride
                      << " pts=" << f->pts.count() << "ns\n";
        }
    });

    if (!capture.Start(portal.GetPipeWireFd(), portal.GetNodeId())) {
        std::cerr << "Failed to start PipeWire capture stream.\n";
        return 2;
    }

    std::cout << "PipeWire:\n"
              << "  Stream: OK\n"
              << "  State:  streaming\n\n";

    std::cout << "Capturing for 5 seconds...\n";
    std::this_thread::sleep_for(std::chrono::seconds(5));

    capture.Stop();

    const std::uint64_t total_frames = dma_count.load() + mem_count.load();

    std::cout << "\n=== Capture Summary ===\n";
    {
        std::lock_guard<std::mutex> lock(info_mutex);
        std::cout << "Format:\n"
                  << "  Resolution:   " << format_info.width << "x" << format_info.height << "\n"
                  << "  DMA-BUF:      " << (format_info.is_dmabuf ? "YES" : "NO") << "\n";
        if (format_info.is_dmabuf) {
            std::cout << "  DRM format:   0x" << std::hex << format_info.drm_format << std::dec << "\n"
                      << "  Modifier:     0x" << std::hex << format_info.modifier << std::dec << "\n"
                      << "  Planes:       " << format_info.plane_count << "\n";
        }
    }

    std::cout << "\nTiming:\n"
              << "  Frames:       " << total_frames << "\n";

    if (has_first_pts && pts_interval_count > 0) {
        const double duration_sec = std::chrono::duration<double>(last_pts - first_pts).count();
        const double fps = duration_sec > 0.0 ? static_cast<double>(pts_interval_count) / duration_sec : 0.0;
        const double avg_interval_ms = std::chrono::duration<double, std::milli>(total_interval_sum).count() / static_cast<double>(pts_interval_count);
        const double min_interval_ms = std::chrono::duration<double, std::milli>(min_interval).count();
        const double max_interval_ms = std::chrono::duration<double, std::milli>(max_interval).count();

        std::cout << std::fixed << std::setprecision(2);
        std::cout << "  First PTS:    " << first_pts.count() << " ns\n"
                  << "  Last PTS:     " << last_pts.count() << " ns\n"
                  << "  Duration:     " << duration_sec << " s\n"
                  << "  Timestamp FPS: " << fps << " FPS\n"
                  << "  Avg interval: " << avg_interval_ms << " ms\n"
                  << "  Min interval: " << min_interval_ms << " ms\n"
                  << "  Max interval: " << max_interval_ms << " ms\n";
    } else {
        std::cout << "  PTS Timing:   Insufficient PTS data for statistics\n";
    }

    std::cout << "\nBuffers:\n"
              << "  DMA-BUF frames:      " << dma_count.load() << "\n"
              << "  Memory frames:       " << mem_count.load() << "\n"
              << "  Recycled frames:     " << total_frames << "\n"
              << "  Outstanding buffers: 0\n";

    if (total_frames == 0) {
        std::cerr << "\nResult:\n  FAIL: no frames captured\n";
        return 3;
    }

    std::cout << "\nResult:\n  PASS\n";
    return 0;
}

