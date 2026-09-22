#pragma once

#include "types.hpp"
#include "capture/FrameTypes.h"

#include <atomic>
#include <functional>
#include <memory>
#include <thread>

#include <pipewire/pipewire.h>
#include <spa/param/param.h>

namespace smoothdemon {

    class PipeWireCapture {
    public:
        using DmaBufCallback = std::function<void(std::unique_ptr<DmaBufFrame>)>;
        using MemoryCallback = std::function<void(std::unique_ptr<MemoryFrame>)>;
        using StateCallback  = std::function<void(bool streaming, u32 width, u32 height)>;

        PipeWireCapture();
        ~PipeWireCapture();

        PipeWireCapture(const PipeWireCapture&)            = delete;
        PipeWireCapture& operator=(const PipeWireCapture&) = delete;

        bool Start(int pipewire_fd, u32 node_id);
        void Stop();

        void SetDmaBufCallback(DmaBufCallback cb) { dma_cb_ = std::move(cb); }
        void SetMemoryCallback(MemoryCallback cb) { mem_cb_ = std::move(cb); }
        void SetStateCallback(StateCallback cb)   { state_cb_ = std::move(cb); }

        // Called by the C-API trampoline for every dequeued buffer
        void HandleProcess();

        // Static trampolines — access private members directly.
        static void OnStateChanged(void* userdata,
                                pw_stream_state old_s,
                                pw_stream_state new_s,
                                const char* msg);
        static void OnParamChanged(void* userdata, u32 id, const spa_pod* param);
        static void OnProcess(void* userdata);

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;

        DmaBufCallback dma_cb_;
        MemoryCallback mem_cb_;
        StateCallback  state_cb_;
        std::atomic<bool> streaming_{false};
    };

}
