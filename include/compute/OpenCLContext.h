#pragma once

#define CL_TARGET_OPENCL_VERSION 200
#define CL_USE_DEPRECATED_OPENCL_1_2_APIS

#include <CL/cl.h>
#include <string>

// Discovers and initializes an Intel GPU OpenCL device (intended for the
// Intel HD Graphics 610 / Gen9 NEO driver, but works for any Intel GPU
// platform), and owns the resulting cl_context and a profiling-enabled
// cl_command_queue.
//
// Usage:
//   OpenCLContext ocl;
//   if (!ocl.Initialize()) { ... report error, bail ... }
//   // ocl.GetContext() / ocl.GetDevice() / ocl.GetQueue() are now valid.
class OpenCLContext
{
public:
    OpenCLContext() = default;
    ~OpenCLContext();

    OpenCLContext( const OpenCLContext& ) = delete;
    OpenCLContext& operator=( const OpenCLContext& ) = delete;

    // Enumerates platforms, picks the Intel one (falls back to the first
    // platform with any GPU device if no Intel platform is found), picks
    // its first GPU device, and creates a context + profiling command
    // queue. Returns false and logs a diagnostic to stderr on failure.
    bool Initialize();

    // Releases the queue/context and resets this object to an
    // uninitialized state. Called automatically by the destructor.
    void Shutdown();

    bool IsInitialized() const { return m_initialized; }

    cl_platform_id   GetPlatform() const { return m_platform; }
    cl_device_id     GetDevice() const { return m_device; }
    cl_context       GetContext() const { return m_context; }
    cl_command_queue GetQueue() const { return m_queue; }

    const std::string& GetDeviceName() const { return m_deviceName; }
    const std::string& GetPlatformName() const { return m_platformName; }
    cl_uint  GetComputeUnits() const { return m_computeUnits; }
    size_t   GetMaxWorkGroupSize() const { return m_maxWorkGroupSize; }
    cl_ulong GetGlobalMemSize() const { return m_globalMemSize; }

private:
    bool FindIntelGpuDevice();
    void QueryDeviceInfo();

    bool m_initialized = false;

    cl_platform_id   m_platform = nullptr;
    cl_device_id     m_device = nullptr;
    cl_context       m_context = nullptr;
    cl_command_queue m_queue = nullptr;

    std::string m_platformName;
    std::string m_deviceName;
    cl_uint     m_computeUnits = 0;
    size_t      m_maxWorkGroupSize = 0;
    cl_ulong    m_globalMemSize = 0;
};
