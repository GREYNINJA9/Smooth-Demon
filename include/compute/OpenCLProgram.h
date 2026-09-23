#pragma once

#define CL_TARGET_OPENCL_VERSION 200

#include <CL/cl.h>
#include <string>
#include <vector>

// Compiles OpenCL C source into a cl_program for a given context/device,
// logging the full compiler build log on failure, and vends cl_kernel
// handles created from that program.
class OpenCLProgram
{
public:
    OpenCLProgram( cl_context context, cl_device_id device );
    ~OpenCLProgram();

    OpenCLProgram( const OpenCLProgram& ) = delete;
    OpenCLProgram& operator=( const OpenCLProgram& ) = delete;

    // Compiles `sourceCode` with the given build options (e.g.
    // "-cl-fast-relaxed-math -cl-mad-enable"). On failure, prints the
    // compiler's CL_PROGRAM_BUILD_LOG to stderr and returns false.
    bool BuildFromSource( const std::string& sourceCode, const std::string& buildOptions = "" );

    // Loads a .cl file from disk and compiles it. Convenience wrapper
    // around BuildFromSource().
    bool BuildFromFile( const std::string& filePath, const std::string& buildOptions = "" );

    // Creates a kernel by entry-point name from the currently built
    // program. Returns nullptr on failure. The caller owns the returned
    // kernel and is responsible for clReleaseKernel(), OR may leave it to
    // this object -- every kernel created via this call is also tracked
    // internally and released when this OpenCLProgram is destroyed.
    cl_kernel CreateKernel( const std::string& kernelName );

    // Clamps a desired local work group size down to what the given
    // kernel actually supports on the current device
    // (CL_KERNEL_WORK_GROUP_SIZE), guarding against CL_INVALID_WORK_GROUP_SIZE.
    size_t ClampLocalWorkSize( cl_kernel kernel, size_t desiredLocalSize ) const;

    cl_program GetProgram() const { return m_program; }
    bool IsBuilt() const { return m_program != nullptr; }

private:
    void ReleaseProgram();

    cl_context   m_context;
    cl_device_id m_device;
    cl_program   m_program = nullptr;

    std::vector<cl_kernel> m_kernels;
};
