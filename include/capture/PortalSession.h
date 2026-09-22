#pragma once

#include "types.hpp"

#include <sdbus-c++/sdbus-c++.h>

#include <memory>
#include <string>
#include <map>

namespace smoothdemon {

    // XDG Desktop Portal ScreenCast client
    // Negotiates user permission via a system dialog the FIRST time
    class PortalSession {
    public:
        PortalSession();
        ~PortalSession();

        PortalSession(const PortalSession&)            = delete;
        PortalSession& operator=(const PortalSession&) = delete;

        // Runs the CreateSession -> SelectSources -> Start flow
        // Blocks until the user responds to the portal dialog
        bool RequestScreenCast();

        int         GetPipeWireFd() const noexcept { return pw_fd_; }
        u32         GetNodeId()     const noexcept { return node_id_; }
        bool        IsOpen()        const noexcept { return !session_handle_.empty(); }

    private:
        std::unique_ptr<sdbus::IConnection> conn_;
        std::string session_handle_;
        int pw_fd_{-1};
        u32 node_id_{0};

        // Send Request and wait for the Response signal
        // results_out receives the response body on success
        bool AwaitPortalResponse(const std::string& request_path,
                                std::map<std::string, sdbus::Variant>& results_out);
    };

}
