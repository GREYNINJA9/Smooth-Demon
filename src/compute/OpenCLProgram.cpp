#include "compute/OpenCLProgram.h"

#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>

OpenCLProgram::OpenCLProgram( cl_context context, cl_device_id device )
    : m_context( context )
    , m_device( device )
{
}

OpenCLProgram::~OpenCLProgram()
{
    for ( cl_kernel kernel : m_kernels )
    {
        if ( kernel )
            clReleaseKernel( kernel );
    }
    m_kernels.clear();

    ReleaseProgram();
}

void OpenCLProgram::ReleaseProgram()
{
    if ( m_program )
    {
        clReleaseProgram( m_program );
        m_program = nullptr;
    }
}

bool OpenCLProgram::BuildFromSource( const std::string& sourceCode, const std::string& buildOptions )
{
    ReleaseProgram();

    const char* srcStr = sourceCode.c_str();
    const size_t srcLen = sourceCode.size();

    cl_int err = CL_SUCCESS;
    m_program = clCreateProgramWithSource( m_context, 1, &srcStr, &srcLen, &err );
    if ( err != CL_SUCCESS || !m_program )
    {
        std::cerr << "[OpenCLProgram] clCreateProgramWithSource failed, error " << err << std::endl;
        m_program = nullptr;
        return false;
    }

    err = clBuildProgram( m_program, 1, &m_device, buildOptions.c_str(), nullptr, nullptr );
    if ( err != CL_SUCCESS )
    {
        size_t logSize = 0;
        clGetProgramBuildInfo( m_program, m_device, CL_PROGRAM_BUILD_LOG, 0, nullptr, &logSize );

        std::vector<char> log( logSize > 0 ? logSize : 1, '\0' );
        if ( logSize > 0 )
            clGetProgramBuildInfo( m_program, m_device, CL_PROGRAM_BUILD_LOG, logSize, log.data(), nullptr );

        std::cerr << "[OpenCLProgram] Build failed (error " << err << "):\n"
                   << ( logSize > 0 ? log.data() : "(no build log available)" ) << std::endl;

        ReleaseProgram();
        return false;
    }

    return true;
}

bool OpenCLProgram::BuildFromFile( const std::string& filePath, const std::string& buildOptions )
{
    std::ifstream file( filePath );
    if ( !file.is_open() )
    {
        std::cerr << "[OpenCLProgram] Failed to open kernel source file: " << filePath << std::endl;
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return BuildFromSource( buffer.str(), buildOptions );
}

cl_kernel OpenCLProgram::CreateKernel( const std::string& kernelName )
{
    if ( !m_program )
    {
        std::cerr << "[OpenCLProgram] CreateKernel(\"" << kernelName << "\") called before a successful build." << std::endl;
        return nullptr;
    }

    cl_int err = CL_SUCCESS;
    cl_kernel kernel = clCreateKernel( m_program, kernelName.c_str(), &err );
    if ( err != CL_SUCCESS || !kernel )
    {
        std::cerr << "[OpenCLProgram] clCreateKernel(\"" << kernelName << "\") failed, error " << err << std::endl;
        return nullptr;
    }

    m_kernels.push_back( kernel );
    return kernel;
}

size_t OpenCLProgram::ClampLocalWorkSize( cl_kernel kernel, size_t desiredLocalSize ) const
{
    size_t kernelMaxWorkGroupSize = desiredLocalSize;
    cl_int err = clGetKernelWorkGroupInfo(
        kernel, m_device, CL_KERNEL_WORK_GROUP_SIZE,
        sizeof( kernelMaxWorkGroupSize ), &kernelMaxWorkGroupSize, nullptr );

    if ( err != CL_SUCCESS )
    {
        // Query failed; be conservative and just hand back what was asked for.
        return desiredLocalSize;
    }

    return std::min( desiredLocalSize, kernelMaxWorkGroupSize );
}
