#include "cpu.hpp"

#include <fstream>
#include <string>

#include <unistd.h>

namespace smoothdemon {

    CpuMonitor::CpuMonitor() {
        long n = sysconf(_SC_NPROCESSORS_ONLN);
        if (n < 1) n = 1;

        const std::size_t count = static_cast<std::size_t>(n);
        prev_cores_.resize(count);
        curr_cores_.resize(count);
    }

    std::uint64_t CpuMonitor::get_total_ticks(const CpuSample& s) noexcept {
        return s.user + s.nice + s.system + s.idle +
               s.iowait + s.irq + s.softirq +s.steal;
    }

    CpuSample CpuMonitor::calculate_delta(const CpuSample& prev,
                                          const CpuSample& curr) noexcept {
        CpuSample d{};
        d.user    = curr.user    - prev.user;
        d.nice    = curr.nice    - prev.nice;
        d.system  = curr.system  - prev.system;
        d.idle    = curr.idle    - prev.idle;
        d.iowait  = curr.iowait  - prev.iowait;
        d.irq     = curr.irq     - prev.irq;
        d.softirq = curr.softirq - prev.softirq;
        d.steal   = curr.steal   - prev.steal;
        return d;
    }

    CpuMetrics CpuMonitor::calculate_metrics(const CpuSample& delta) noexcept {
        CpuMetrics m{};

        const std::uint64_t total =get_total_ticks(delta);
        if(total == 0) return m;

        const std::uint64_t idle_time = delta.idle + delta.iowait;
        const double inv = 100.0 / static_cast<double>(total);

        auto clamp_pct = [](double v) {
            if (v < 0.0) return 0.0;
            if(v > 100.0) return 100.0;
            return v;
        };

        m.total_percent = clamp_pct(
            (1.0 - static_cast<double>(idle_time) / static_cast<double>(total)) * 100.0);
            m.user_percent   = clamp_pct(static_cast<double>(delta.user)   * inv);
            m.system_percent = clamp_pct(static_cast<double>(delta.system) * inv);
            m.iowait_percent = clamp_pct(static_cast<double>(delta.iowait) * inv);
            return m;
    }

    // ---------- Allocation-free parse helpers ------------------------------------------
    namespace {
        inline const char* skip_spaces(const char* p) {
            while (*p == ' ' || *p == '\t') ++p;
            return p;
        }

        inline const char* parse_u64(const char* p, std::uint64_t& out) {
            p = skip_spaces(p);
            std::uint64_t v = 0;
            bool any = false;
            while (*p >= '0' && *p <= '9') {
                v = v * 10u + static_cast<std::uint64_t>(*p - '0');
                ++p;
                any = true;
            }
            if (any) out = v;
            return p;
        }

        inline void parse_eight(const char* p, CpuSample& s) {
            p = parse_u64(p, s.user);
            p = parse_u64(p, s.nice);
            p = parse_u64(p, s.system);
            p = parse_u64(p, s.idle);
            p = parse_u64(p, s.iowait);
            p = parse_u64(p, s.irq);
            p = parse_u64(p, s.softirq);
            p = parse_u64(p, s.steal);
        }

    }

    bool CpuMonitor::sample() {
        std::ifstream f("/proc/stat");
        if (!f.is_open()) return false;

        CpuSample aggregate_curr{};
        std::vector<CpuSample> core_curr(prev_cores_.size());

        std::string line;
        line.reserve(256);

        bool aggregate_seen = false;

        while (std::getline(f, line)) {
            if (line.size() < 4 ||
                line[0] != 'c' || line[1] != 'p' || line[2] != 'u') {
                break;
            }

            const char* p = line.c_str() + 3;

            if (*p == ' ') {
                // Aggregate line: "cpu  ..."
                if (!aggregate_seen) {
                    parse_eight(p, aggregate_curr);
                    aggregate_seen = true;
                }
            } else if (*p >= '0' && *p <= '9') {
                // Per-core line: "cpuN ..."
                std::uint64_t idx = 0;
                const char* q = p;
                while (*q >= '0' && *q <= '9') {
                    idx = idx * 10u + static_cast<std::uint64_t>(*q - '0');
                    ++q;
                }
                if (idx < core_curr.size()) {
                    parse_eight(q, core_curr[static_cast<std::size_t>(idx)]);
                }
            }
        }

        if (!aggregate_seen) return false;

        if (first_sample_) {
            prev_aggregate_ = aggregate_curr;
            for (std::size_t i = 0; i < prev_cores_.size(); ++i) {
                prev_cores_[i] = core_curr[i];
            }
            first_sample_ = false;

            curr_aggregate_ = CpuMetrics{};
            for (auto& m : curr_cores_) m = CpuMetrics{};
            return true;
        }

        curr_aggregate_ = calculate_metrics(
            calculate_delta(prev_aggregate_, aggregate_curr));
            
        for (std::size_t i = 0; i < prev_cores_.size(); ++i) {
            curr_cores_[i] = calculate_metrics(
                calculate_delta(prev_cores_[i], core_curr[i]));
        }

        prev_aggregate_ = aggregate_curr;
        for (std::size_t i = 0; i < prev_cores_.size(); ++i) {
            prev_cores_[i] = core_curr[i];
        }

        return true;
    }
}
