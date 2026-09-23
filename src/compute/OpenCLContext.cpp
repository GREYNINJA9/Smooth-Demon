#include "compute/OpenCLContext.h"

#include <vector>
#include <iostream>
#include <algorithm>
#include <cctype>

namespace
{
    std::string ToLower( std::string s )
    {
        std::transform( s.begin(), s.end(), s.begin(), []( unsigned char c ) { return std::tolower( c ); } );
        return s;
    }

    std::string QueryPlatformString( cl_platform_id platform, cl_platform_info info )
    {
        size_t size = 0;
        if ( clGetPlatformInfo( platform, info, 0, nullptr, &size ) != CL_SUCCESS || size == 0 )
            return {};

        std::string value( size, '\0' );
        clGetPlatformInfo( platform, info, size, value.data(), nullptr );
        if ( !value.empty() && value.back() == '\0' )
            value.pop_back();
        return value;
    }

    std::string QueryDeviceString( cl_device_id device, cl_device_info info )
    {
        size_t size = 0;
        if ( clGetDeviceInfo( device, info, 0, nullptr, &size ) != CL_SUCCESS || size == 0 )
            return {};

        std::string value( size, '\0' );
        clGetDeviceInfo( device, info, size, value.data(), nullptr );
        if ( !value.empty() && value.back() == '\0' )
            value.pop_back();
        return value;
    }

    const char* ClErrorString( cl_int err )
    {
        switch ( err )
        {
            case CL_SUCCESS: return "CL_SUCCESS";
            case CL_DEVICE_NOT_FOUND: return "CL_DEVICE_NOT_FOUND";
            case CL_DEVICE_NOT_AVAILABLE: return "CL_DEVICE_NOT_AVAILABLE";
            case CL_OUT_OF_HOST_MEMORY: return "CL_OUT_OF_HOST_MEMORY";
            case CL_OUT_OF_RESOURCES: return "CL_OUT_OF_RESOURCES";
            case CL_INVALID_PLATFORM: return "CL_INVALID_PLATFORM";
            case CL_INVALID_DEVICE: return "CL_INVALID_DEVICE";
            case CL_INVALID_VALUE: return "CL_INVALID_VALUE";
            default: return "UNKNOWN_CL_ERROR";
        }
    }
}

OpenCLContext::~OpenCLContext()
{
    Shutdown();
}

bool OpenCLContext::FindIntelGpuDevice()
{
    cl_uint numPlatforms = 0;
    if ( clGetPlatformIDs( 0, nullptr, &numPlatforms ) != CL_SUCCESS || numPlatforms == 0 )
    {
        std::cerr << "[OpenCLContext] No OpenCL platforms found. Is an ICD installed?" << std::endl;
        return false;
    }

    std::vector<cl_platform_id> platforms( numPlatforms );
    clGetPlatformIDs( numPlatforms, platforms.data(), nullptr );

    cl_platform_id fallbackPlatform = nullptr;
    cl_device_id   fallbackDevice = nullptr;

    for ( cl_platform_id platform : platforms )
    {
        const std::string name   = QueryPlatformString( platform, CL_PLATFORM_NAME );
        const std::string vendor = QueryPlatformString( platform, CL_PLATFORM_VENDOR );

        cl_uint numDevices = 0;
        if ( clGetDeviceIDs( platform, CL_DEVICE_TYPE_GPU, 0, nullptr, &numDevices ) != CL_SUCCESS || numDevices == 0 )
            continue;

        std::vector<cl_device_id> devices( numDevices );
        clGetDeviceIDs( platform, CL_DEVICE_TYPE_GPU, numDevices, devices.data(), nullptr );

        const bool isIntel = ToLower( name ).find( "intel" ) != std::string::npos ||
                              ToLower( vendor ).find( "intel" ) != std::string::npos;

        if ( isIntel )
        {
            m_platform = platform;
            m_device = devices[ 0 ];
            m_platformName = name;
            return true;
        }

        if ( !fallbackPlatform )
        {
            fallbackPlatform = platform;
            fallbackDevice = devices[ 0 ];
        }
    }

    // No Intel platform found: fall back to whatever GPU we did find, if any,
    // rather than failing outright on non-Intel dev machines.
    if ( fallbackPlatform )
    {
        std::cerr << "[OpenCLContext] No Intel OpenCL platform found; falling back to first available GPU platform." << std::endl;
        m_platform = fallbackPlatform;
        m_device = fallbackDevice;
        m_platformName = QueryPlatformString( fallbackPlatform, CL_PLATFORM_NAME );
        return true;
    }

    std::cerr << "[OpenCLContext] No GPU OpenCL device found on any platform." << std::endl;
    std::cerr << "  If this is an Intel iGPU system, install the Intel Compute Runtime, e.g.:" << std::endl;
    std::cerr << "    sudo pacman -S opencl-headers ocl-icd intel-compute-runtime clinfo" << std::endl;
    return false;
}

void OpenCLContext::QueryDeviceInfo()
{
    m_deviceName = QueryDeviceString( m_device, CL_DEVICE_NAME );

    clGetDeviceInfo( m_device, CL_DEVICE_MAX_COMPUTE_UNITS, sizeof( m_computeUnits ), &m_computeUnits, nullptr );
    clGetDeviceInfo( m_device, CL_DEVICE_MAX_WORK_GROUP_SIZE, sizeof( m_maxWorkGroupSize ), &m_maxWorkGroupSize, nullptr );
    clGetDeviceInfo( m_device, CL_DEVICE_GLOBAL_MEM_SIZE, sizeof( m_globalMemSize ), &m_globalMemSize, nullptr );
}

bool OpenCLContext::Initialize()
{
    if ( m_initialized )
        return true;

    if ( !FindIntelGpuDevice() )
        return false;

    QueryDeviceInfo();

    cl_int err = CL_SUCCESS;
    m_context = clCreateContext( nullptr, 1, &m_device, nullptr, nullptr, &err );
    if ( err != CL_SUCCESS || !m_context )
    {
        std::cerr << "[OpenCLContext] clCreateContext failed: " << ClErrorString( err ) << std::endl;
        return false;
    }

    cl_queue_properties props[] = { CL_QUEUE_PROPERTIES, CL_QUEUE_PROFILING_ENABLE, 0 };
    m_queue = clCreateCommandQueueWithProperties( m_context, m_device, props, &err );
    if ( err != CL_SUCCESS || !m_queue )
    {
        std::cerr << "[OpenCLContext] clCreateCommandQueueWithProperties failed: " << ClErrorString( err ) << std::endl;
        clReleaseContext( m_context );
        m_context = nullptr;
        return false;
    }

    m_initialized = true;
    return true;
}

void OpenCLContext::Shutdown()
{
    if ( m_queue )
    {
        clFinish( m_queue );
        clReleaseCommandQueue( m_queue );
        m_queue = nullptr;
    }

    if ( m_context )
    {
        clReleaseContext( m_context );
        m_context = nullptr;
    }

    // Platform/device IDs are not owned handles in OpenCL, nothing to release.
    m_platform = nullptr;
    m_device = nullptr;
    m_initialized = false;
}
