#include "capture/PortalSession.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <future>
#include <map>
#include <sstream>

#include <sdbus-c++/sdbus-c++.h>
#include <unistd.h>

namespace smoothdemon {

    namespace {

        constexpr const char* kPortalService  = "org.freedesktop.portal.Desktop";
        constexpr const char* kPortalPath     = "/org/freedesktop/portal/desktop";
        constexpr const char* kScreenCastIface = "org.freedesktop.portal.ScreenCast";
        constexpr const char* kRequestIface   = "org.freedesktop.portal.Request";
        constexpr const char* kSessionIface   = "org.freedesktop.portal.Session";

        // Portal request paths are: /org/freedesktop/portal/desktop/request/<SENDER>/<TOKEN>
        // SENDER is our unique bus name with ':' replaced by '_' and '.' replaced by '_'
        std::string unique_name_to_sender(const std::string& unique) {
            std::string s = unique;
            if (!s.empty() && s[0] == ':') s.erase(0, 1);
            for (char& c : s) if (c == '.') c = '_';
            return s;
        }

        // A handle token must be alphanumeric + underscore only
        std::string make_token(const char* tag) {
            static int counter = 0;
            std::ostringstream os;
            os << "smoothdemon_" << tag << "_" << ++counter;
            return os.str();
        }

    }

    PortalSession::PortalSession()
        : conn_(sdbus::createSessionBusConnection()) {
        // We need the event loop running so signals are delivered to signal handlers
        conn_->enterEventLoopAsync();
    }

    PortalSession::~PortalSession() {
        if (!session_handle_.empty()) {
            try {
                auto session_proxy = sdbus::createProxy(
                    *conn_,
                    sdbus::ServiceName{kPortalService},
                    sdbus::ObjectPath{session_handle_});
                session_proxy->callMethod("Close")
                            .onInterface(kSessionIface);
            } catch (...) {
                // Best effort - connection close also kills the session
            }
        }
        if (pw_fd_ >= 0) { ::close(pw_fd_); pw_fd_ = -1; }
    }

    bool PortalSession::AwaitPortalResponse(
        const std::string& request_path,
        std::map<std::string, sdbus::Variant>& results_out) {

        auto req_proxy = sdbus::createProxy(
            *conn_,
            sdbus::ServiceName{kPortalService},
            sdbus::ObjectPath{request_path});

        std::promise<std::pair<u32, std::map<std::string, sdbus::Variant>>> p;
        auto fut = p.get_future();

        req_proxy->uponSignal("Response")
            .onInterface(kRequestIface)
            .call([&p](u32 code, std::map<std::string, sdbus::Variant> results) {
                p.set_value({code, std::move(results)});
            });

        auto [code, results] = fut.get();
        if (code != 0) {
            std::fprintf(stderr,
                "[portal] request %s returned code %u (1=cancelled, 2=other)\n",
                request_path.c_str(), code);
            return false;
        }
        results_out = std::move(results);
        return true;
    }

    bool PortalSession::RequestScreenCast() {
        // Predict the request path using our unique bus name + a token
        const std::string unique = conn_->getUniqueName();
        const std::string sender = unique_name_to_sender(unique);

        auto make_request_path = [&](const std::string& token) {
            return std::string("/org/freedesktop/portal/desktop/request/")
                + sender + "/" + token;
        };

        // ----- CreateSession ------------------------------------------------------------------------------
        const std::string create_token    = make_token("create");
        const std::string session_token   = make_token("session");
        const std::string create_path     = make_request_path(create_token);

        std::map<std::string, sdbus::Variant> create_opts;
        create_opts["handle_token"]          = sdbus::Variant{create_token};
        create_opts["session_handle_token"]  = sdbus::Variant{session_token};

        auto portal_proxy = sdbus::createProxy(
            *conn_, sdbus::ServiceName{kPortalService}, sdbus::ObjectPath{kPortalPath});

        try {
            portal_proxy->callMethod("CreateSession")
                .onInterface(kScreenCastIface)
                .withArguments(create_opts);
        } catch (const sdbus::Error& e) {
            std::fprintf(stderr, "[portal] CreateSession call failed: %s\n", e.what());
            return false;
        }

        std::map<std::string, sdbus::Variant> create_results;
        if (!AwaitPortalResponse(create_path, create_results)) return false;

        auto it = create_results.find("session_handle");
        if (it == create_results.end()) {
            std::fprintf(stderr, "[portal] session_handle missing\n");
            return false;
        }
        session_handle_ = it->second.get<std::string>();
        std::fprintf(stderr, "[portal] session: %s\n", session_handle_.c_str());

        // ------- SelectSources --------------------------------------------------------------------------------------
        const std::string sel_token = make_token("select");
        const std::string sel_path  = make_request_path(sel_token);

        std::map<std::string, sdbus::Variant> sel_opts;
        sel_opts["handle_token"] = sdbus::Variant{sel_token};
        sel_opts["types"]        = sdbus::Variant{u32{1u | 2u}};  // 1=MONITOR, 2=WINDOW
        sel_opts["multiple"]     = sdbus::Variant{false};
        sel_opts["cursor_mode"]  = sdbus::Variant{u32{2u}};       // 2=embedded

        try {
            portal_proxy->callMethod("SelectSources")
                .onInterface(kScreenCastIface)
                .withArguments(sdbus::ObjectPath{session_handle_}, sel_opts);
        } catch (const sdbus::Error& e) {
            std::fprintf(stderr, "[portal] SelectSources failed: %s\n", e.what());
            return false;
        }

        std::map<std::string, sdbus::Variant> sel_results;
        if (!AwaitPortalResponse(sel_path, sel_results)) return false;

        // ----- Start ---------------------------------------------------------------------------------
        const std::string start_token = make_token("start");
        const std::string start_path  = make_request_path(start_token);

        std::map<std::string, sdbus::Variant> start_opts;
        start_opts["handle_token"] = sdbus::Variant{start_token};

        try {
            portal_proxy->callMethod("Start")
                .onInterface(kScreenCastIface)
                .withArguments(sdbus::ObjectPath{session_handle_}, std::string{""}, start_opts);
        } catch (const sdbus::Error& e) {
            std::fprintf(stderr, "[portal] Start failed: %s\n", e.what());
            return false;
        }

        std::map<std::string, sdbus::Variant> start_results;
        if (!AwaitPortalResponse(start_path, start_results)) return false;

        // Response contains "streams": a(ua{sv}) — array of (node_id, properties)
        auto streams_it = start_results.find("streams");
        if (streams_it == start_results.end()) {
            std::fprintf(stderr, "[portal] no streams in response\n");
            return false;
        }

        const auto streams = streams_it->second.get<
            std::vector<sdbus::Struct<u32, std::map<std::string, sdbus::Variant>>>>();

        if (streams.empty()) {
            std::fprintf(stderr, "[portal] empty streams array\n");
            return false;
        }

        node_id_ = streams.front().get<0>();
        std::fprintf(stderr, "[portal] node_id=%u\n", node_id_);

        // ---------------OpenPipeWireRemote ---------------------------------------------------------
        // The PipeWire fd is NOT in the streams properties. It is returned
        // by a dedicated method that hands us a UnixFd connected to the
        // portal's PipeWire socket for THIS session
        std::map<std::string, sdbus::Variant> remote_opts;

        sdbus::UnixFd pw_fd_handle;
        try {
            portal_proxy->callMethod("OpenPipeWireRemote")
                .onInterface(kScreenCastIface)
                .withArguments(sdbus::ObjectPath{session_handle_}, remote_opts)
                .storeResultsTo(pw_fd_handle);
        } catch (const sdbus::Error& e) {
            std::fprintf(stderr, "[portal] OpenPipeWireRemote failed: %s\n", e.what());
            return false;
        }

        // UnixFd owns its fd. Duplicate so we can safely close our copy
        pw_fd_ = ::dup(pw_fd_handle.get());
        if (pw_fd_ < 0) {
            std::fprintf(stderr, "[portal] dup(fd) failed: %s\n", std::strerror(errno));
            return false;
        }

        std::fprintf(stderr, "[portal] pipewire_fd=%d\n", pw_fd_);
        return true;
    }

}
