#pragma once

#include "types.hpp"

#include <string>

namespace smoothdemon {

    struct SystemMetadata {
        std::string os_name;
        std::string kernel_version;
        std::string architecture;
        std::string cpu_model;
        u32 cpu_cores_physical{0};
        u32 cpu_cores_logical{0};
        std::string gpu_model;
        std::string gpu_pci_id;
        std::string driver_name;
        std::string display_protocol;
        std::string compositor;
    };

    class SystemInfoCollector {
    public:
        static SystemMetadata collect();

        static u64 get_uptime_seconds();
        static std::string get_formatted_uptime();

    private:
        static std::string parse_os_name();
        static std::string parse_cpu_model();
        static u32 parse_cpu_physical_cores();
        static void parse_gpu_info(std::string& out_model, std::string& out_pci_id);
        static void detect_display_session(std::string& out_protocol,
                                        std::string& out_compositor);
    };

}
