#include "capture/PipeWireCapture.h"

#include <algorithm>
#include <cassert>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <stdexcept>

#include <libdrm/drm_fourcc.h>
#include <pipewire/pipewire.h>
#include <spa/buffer/meta.h>
#include <spa/debug/format.h>
#include <spa/param/param.h>
#include <spa/param/video/format-utils.h>
#include <spa/param/video/raw.h>
#include <spa/pod/builder.h>
#include <spa/pod/pod.h>
#include <unistd.h>

namespace smoothdemon {

    namespace {

        PixelFormat spa_to_pixel_format(spa_video_format f) {
            switch (f) {
                case SPA_VIDEO_FORMAT_BGRA: return PixelFormat::BGRA;
                case SPA_VIDEO_FORMAT_RGBA: return PixelFormat::RGBA;
                case SPA_VIDEO_FORMAT_BGRx: return PixelFormat::BGRX;
                case SPA_VIDEO_FORMAT_RGBx: return PixelFormat::RGBX;
                default:                    return PixelFormat::Unknown;
            }
        }

        u64 spa_to_drm_format(spa_video_format f) {
            switch (f) {
                case SPA_VIDEO_FORMAT_BGRA: return DRM_FORMAT_ARGB8888;
                case SPA_VIDEO_FORMAT_BGRx: return DRM_FORMAT_XRGB8888;
                case SPA_VIDEO_FORMAT_RGBA: return DRM_FORMAT_ABGR8888;
                case SPA_VIDEO_FORMAT_RGBx: return DRM_FORMAT_XBGR8888;
                default:                    return 0;
            }
        }

        const spa_pod* build_stream_params(spa_pod_builder& b, bool with_dmabuf) {
            spa_rectangle size_default = SPA_RECTANGLE(1280, 720);
            spa_rectangle size_min     = SPA_RECTANGLE(1, 1);
            spa_rectangle size_max     = SPA_RECTANGLE(4096, 4096);
            spa_fraction  rate_default = SPA_FRACTION(60, 1);
            spa_fraction  rate_min     = SPA_FRACTION(0, 1);
            spa_fraction  rate_max     = SPA_FRACTION(240, 1);

            spa_pod_frame f, f2;
            spa_pod_builder_push_object(&b, &f, SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat);
            spa_pod_builder_add(&b, SPA_FORMAT_mediaType,
                                SPA_POD_Id(SPA_MEDIA_TYPE_video), 0);
            spa_pod_builder_add(&b, SPA_FORMAT_mediaSubtype,
                                SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw), 0);
            spa_pod_builder_add(&b, SPA_FORMAT_VIDEO_format,
                SPA_POD_CHOICE_ENUM_Id(5,
                                    SPA_VIDEO_FORMAT_BGRx,
                                    SPA_VIDEO_FORMAT_BGRA,
                                    SPA_VIDEO_FORMAT_RGBx,
                                    SPA_VIDEO_FORMAT_RGBA,
                                    SPA_VIDEO_FORMAT_NV12), 0);
            spa_pod_builder_add(&b, SPA_FORMAT_VIDEO_size,
                SPA_POD_CHOICE_RANGE_Rectangle(&size_default, &size_min, &size_max), 0);
            spa_pod_builder_add(&b, SPA_FORMAT_VIDEO_framerate,
                SPA_POD_CHOICE_RANGE_Fraction(&rate_default, &rate_min, &rate_max), 0);

            if (with_dmabuf) {
                spa_pod_builder_prop(&b, SPA_FORMAT_VIDEO_modifier,
                    SPA_POD_PROP_FLAG_MANDATORY | SPA_POD_PROP_FLAG_DONT_FIXATE);
                spa_pod_builder_push_choice(&b, &f2, SPA_CHOICE_Enum, 0);
                spa_pod_builder_long(&b, DRM_FORMAT_MOD_LINEAR);
                spa_pod_builder_long(&b, I915_FORMAT_MOD_X_TILED);
                spa_pod_builder_long(&b, I915_FORMAT_MOD_Y_TILED);
                spa_pod_builder_long(&b, DRM_FORMAT_MOD_INVALID);
                spa_pod_builder_pop(&b, &f2);
            }
            return static_cast<const spa_pod*>(spa_pod_builder_pop(&b, &f));
        }

    }

    // Thread-safe buffer recycler — shared between PipeWireCapture and outstanding
    // frames.  Prevents use-after-free when frames outlive the stream.
    struct BufferRecycler {
        std::mutex mtx;
        pw_stream* stream{nullptr};

        void recycle(pw_buffer* b) {
            std::lock_guard<std::mutex> lock(mtx);
            if (stream) pw_stream_queue_buffer(stream, b);
        }

        void invalidate() {
            std::lock_guard<std::mutex> lock(mtx);
            stream = nullptr;
        }
    };

    // --------------- Impl ----------------------------------------------------------------------------------------------

    struct PipeWireCapture::Impl {
        pw_main_loop* loop{nullptr};
        pw_context*   ctx{nullptr};
        pw_core*      core{nullptr};
        pw_stream*    stream{nullptr};
        std::thread   loop_thread;

        spa_video_info_raw format{};
        bool have_dmabuf{false};
        std::chrono::steady_clock::time_point start_time{};

        // Shared with outstanding frames for safe buffer recycling
        std::shared_ptr<BufferRecycler> recycler;
    };

    // ------------------- Static trampolines (private members of PipeWireCapture) ------------------------------------------

    void PipeWireCapture::OnStateChanged(void* userdata,
                                        pw_stream_state old_s,
                                        pw_stream_state new_s,
                                        const char* msg) {
        auto* self = static_cast<PipeWireCapture*>(userdata);

        std::fprintf(stderr, "[pw] stream state: %s -> %s (%s)\n",
                    pw_stream_state_as_string(old_s),
                    pw_stream_state_as_string(new_s),
                    msg ? msg : "");

        if (new_s == PW_STREAM_STATE_STREAMING) {
            self->streaming_ = true;
        } else if (old_s == PW_STREAM_STATE_STREAMING ||
                new_s == PW_STREAM_STATE_ERROR) {
            self->streaming_ = false;
        }
    }

    void PipeWireCapture::OnParamChanged(void* userdata, u32 id, const spa_pod* param) {
        auto* self = static_cast<PipeWireCapture*>(userdata);
        if (!param || id != SPA_PARAM_Format) return;

        auto& impl = *self->impl_;

        // spa_format_parse expects uint32_t* (not spa_media_type*)
        u32 media_type = 0;
        u32 media_subtype = 0;
        if (spa_format_parse(param, &media_type, &media_subtype) < 0) return;
        if (spa_format_video_raw_parse(param, &impl.format) < 0) return;

        const spa_pod_prop* mod_prop =
            spa_pod_find_prop(param, nullptr, SPA_FORMAT_VIDEO_modifier);
        impl.have_dmabuf = (mod_prop != nullptr);

        impl.start_time = std::chrono::steady_clock::now();

        if (self->state_cb_) {
            self->state_cb_(true, impl.format.size.width, impl.format.size.height);
        }

        std::fprintf(stderr, "[pw] format: %s %ux%u dmabuf=%d\n",
                    spa_debug_type_find_name(spa_type_video_format, impl.format.format),
                    impl.format.size.width, impl.format.size.height,
                    impl.have_dmabuf ? 1 : 0);

        // Now that we know the format (re)declare buffers + meta
        char buf[1024];
        spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, sizeof(buf));
        u32 buffer_types = (1u << SPA_DATA_MemPtr) | (1u << SPA_DATA_MemFd);
        if (impl.have_dmabuf) buffer_types |= (1u << SPA_DATA_DmaBuf);

        const spa_pod* params[2];
        params[0] = static_cast<spa_pod*>(spa_pod_builder_add_object(&b,
            SPA_TYPE_OBJECT_ParamMeta, SPA_PARAM_Meta,
            SPA_PARAM_META_type, SPA_POD_Id(SPA_META_Header),
            SPA_PARAM_META_size, SPA_POD_Int(sizeof(spa_meta_header))));
        params[1] = static_cast<spa_pod*>(spa_pod_builder_add_object(&b,
            SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers,
            SPA_PARAM_BUFFERS_buffers, SPA_POD_Int(4),
            SPA_PARAM_BUFFERS_dataType, SPA_POD_CHOICE_FLAGS_Int(buffer_types)));

        pw_stream_update_params(impl.stream, params, 2);
    }

    void PipeWireCapture::OnProcess(void* userdata) {
        static_cast<PipeWireCapture*>(userdata)->HandleProcess();
    }

    // Events table - can now reference the private static members
    static const pw_stream_events k_stream_events = []() {
        pw_stream_events e{};
        e.version       = PW_VERSION_STREAM_EVENTS;
        e.state_changed = &PipeWireCapture::OnStateChanged;
        e.param_changed = &PipeWireCapture::OnParamChanged;
        e.process       = &PipeWireCapture::OnProcess;
        return e;
    }();

    // ------------------ Constructor / destructor ----------------------------------------

    PipeWireCapture::PipeWireCapture()
        : impl_(std::make_unique<Impl>()) {
        pw_init(nullptr, nullptr);
    }

    PipeWireCapture::~PipeWireCapture() {
        Stop();
        pw_deinit();
    }

    // ------------- Start / Stop ---------------------------------------------------------------

    bool PipeWireCapture::Start(int pipewire_fd, u32 node_id) {
        auto& impl = *impl_;

        impl.loop = pw_main_loop_new(nullptr);
        if (!impl.loop) return false;

        impl.ctx = pw_context_new(pw_main_loop_get_loop(impl.loop), nullptr, 0);
        if (!impl.ctx) return false;

        // Dup the fd since pw_context_connect_fd takes ownership
        int dup_fd = ::dup(pipewire_fd);
        if (dup_fd < 0) {
            std::fprintf(stderr, "[pw] dup(pipewire_fd) failed: %s\n",
                         std::strerror(errno));
            return false;
        }
        impl.core = pw_context_connect_fd(impl.ctx, dup_fd, nullptr, 0);
        if (!impl.core) {
            std::fprintf(stderr, "[pw] connect_fd failed\n");
            ::close(dup_fd);
            return false;
        }

        pw_properties* props = pw_properties_new(
            PW_KEY_MEDIA_TYPE,     "Video",
            PW_KEY_MEDIA_CATEGORY, "Capture",
            PW_KEY_MEDIA_ROLE,     "Screen",
            nullptr);

        impl.stream = pw_stream_new_simple(
            pw_main_loop_get_loop(impl.loop),
            "SmoothDemon-Capture",
            props,
            &k_stream_events,
            this);
        if (!impl.stream) return false;

        char buf[2048];
        spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, sizeof(buf));
        const spa_pod* params[2];
        params[0] = build_stream_params(b, true);
        params[1] = build_stream_params(b, false);

        if (pw_stream_connect(impl.stream,
                            PW_DIRECTION_INPUT,
                            node_id,
                            static_cast<pw_stream_flags>(
                                PW_STREAM_FLAG_MAP_BUFFERS |
                                PW_STREAM_FLAG_AUTOCONNECT),
                            params, 2) < 0) {
            std::fprintf(stderr, "[pw] stream_connect failed\n");
            return false;
        }

        // Initialize buffer recycler for safe RAII buffer return
        impl.recycler = std::make_shared<BufferRecycler>();
        impl.recycler->stream = impl.stream;

        impl.loop_thread = std::thread([loop = impl.loop]() {
            pw_main_loop_run(loop);
        });

        return true;
    }

    void PipeWireCapture::Stop() {
        auto& impl = *impl_;

        // Invalidate the recycler first so outstanding frames safely no-op
        if (impl.recycler) {
            impl.recycler->invalidate();
        }

        if (impl.loop && impl.stream) {
            // Safely deactivate the stream from inside the loop thread
            // to prevent callbacks from firing after we start tearing down
            auto deactivate_fn = [](spa_loop*, bool, uint32_t, const void*,
                                    size_t, void* userdata) -> int {
                auto* stream = static_cast<pw_stream*>(userdata);
                return pw_stream_set_active(stream, false);
            };
            pw_loop_invoke(pw_main_loop_get_loop(impl.loop),
                           deactivate_fn, 0, nullptr, 0, false, impl.stream);
        }

        if (impl.loop) {
            pw_main_loop_quit(impl.loop);
        }
        if (impl.loop_thread.joinable()) {
            impl.loop_thread.join();
        }

        // After the loop thread has stopped, no more callbacks can fire.
        // Safe to tear down PipeWire objects in reverse creation order.
        if (impl.stream) {
            pw_stream_disconnect(impl.stream);
            pw_stream_destroy(impl.stream);
            impl.stream = nullptr;
        }
        if (impl.core)   { pw_core_disconnect(impl.core);   impl.core   = nullptr; }
        if (impl.ctx)    { pw_context_destroy(impl.ctx);    impl.ctx    = nullptr; }
        if (impl.loop)   { pw_main_loop_destroy(impl.loop); impl.loop   = nullptr; }
        streaming_ = false;
    }

    // --------------------- Process (already public) ------------------------------------------------------------------------------

    void PipeWireCapture::HandleProcess() {
        auto& impl = *impl_;

        if (pw_stream_get_state(impl.stream, nullptr) != PW_STREAM_STATE_STREAMING)
            return;

        pw_buffer* b = pw_stream_dequeue_buffer(impl.stream);
        if (!b) return;

        std::chrono::nanoseconds pts{0};
        if (auto* header = static_cast<spa_meta_header*>(
                spa_buffer_find_meta_data(b->buffer, SPA_META_Header, sizeof(spa_meta_header)))) {
            pts = std::chrono::nanoseconds{header->pts};
        } else {
            pts = std::chrono::steady_clock::now() - impl.start_time;
        }

        spa_data& d = b->buffer->datas[0];

        if (d.type == SPA_DATA_DmaBuf) {
            auto frame = std::make_unique<DmaBufFrame>();
            frame->width      = impl.format.size.width;
            frame->height     = impl.format.size.height;
            frame->pts        = pts;
            frame->drm_format = spa_to_drm_format(impl.format.format);
            frame->fd         = static_cast<int>(d.fd);
            frame->total_size = d.maxsize;
            frame->modifier   = impl.format.modifier;

            u32 planes = std::min<u32>(b->buffer->n_datas, 4u);
            frame->plane_count = planes;
            for (u32 i = 0; i < planes; ++i) {
                const spa_chunk& c = *b->buffer->datas[i].chunk;
                frame->planes[i].offset = c.offset;
                frame->planes[i].pitch  = static_cast<std::size_t>(c.stride);
            }

            auto recycler = impl.recycler;
            frame->on_frame_done = [recycler, b]() {
                recycler->recycle(b);
            };

            if (dma_cb_) {
                dma_cb_(std::move(frame));
            } else {
                pw_stream_queue_buffer(impl.stream, b);
            }
            return;
        }

        if (d.type == SPA_DATA_MemPtr || d.type == SPA_DATA_MemFd) {
            auto frame = std::make_unique<MemoryFrame>();
            frame->width  = impl.format.size.width;
            frame->height = impl.format.size.height;
            frame->pts    = pts;
            frame->format = spa_to_pixel_format(impl.format.format);
            frame->memory = d.data;
            frame->stride = static_cast<std::size_t>(d.chunk->stride);
            frame->size   = d.chunk->size;
            frame->offset = d.chunk->offset;

            auto recycler = impl.recycler;
            frame->on_frame_done = [recycler, b]() {
                recycler->recycle(b);
            };

            if (mem_cb_) {
                mem_cb_(std::move(frame));
            } else {
                pw_stream_queue_buffer(impl.stream, b);
            }
            return;
        }

        pw_stream_queue_buffer(impl.stream, b);
    }

}
