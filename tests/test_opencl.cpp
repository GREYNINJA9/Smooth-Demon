#include "compute/OpenCLContext.h"
#include "compute/OpenCLProgram.h"
#include "compute/OpenCLBuffer.h"
#include "compute/OpenCLProfiling.h"

#include <iostream>
#include <vector>

const char* kTestKernelSource = R"(
__kernel void VectorAdd(__global const float* a, __global const float* b, __global float* c, int count) {
    int id = get_global_id(0);
    if (id < count) {
        c[id] = a[id] + b[id];
    }
}
)";

int main()
{
    std::cout << "[SMOOTHBOOST] Testing OpenCL Compute on Intel HD 610..." << std::endl;

    OpenCLContext ocl;
    if ( !ocl.Initialize() )
    {
        std::cerr << "Failed to initialize Intel OpenCL device" << std::endl;
        return 1;
    }

    std::cout << "Platform: " << ocl.GetPlatformName() << std::endl;
    std::cout << "Device: " << ocl.GetDeviceName()
               << " | Compute Units: " << ocl.GetComputeUnits()
               << " | Max Work Group Size: " << ocl.GetMaxWorkGroupSize()
               << " | Global Mem: " << ( ocl.GetGlobalMemSize() / ( 1024 * 1024 ) ) << " MB"
               << std::endl;

    OpenCLProgram prog( ocl.GetContext(), ocl.GetDevice() );
    if ( !prog.BuildFromSource( kTestKernelSource, "-cl-fast-relaxed-math -cl-mad-enable" ) )
    {
        std::cerr << "Kernel build failed" << std::endl;
        return 1;
    }

    cl_kernel kernel = prog.CreateKernel( "VectorAdd" );
    if ( !kernel )
    {
        std::cerr << "Failed to create VectorAdd kernel" << std::endl;
        return 1;
    }

    const int N = 1024 * 1024; // 1M floats
    OpenCLBuffer bufA( ocl.GetContext(), N * sizeof( float ) );
    OpenCLBuffer bufB( ocl.GetContext(), N * sizeof( float ) );
    OpenCLBuffer bufC( ocl.GetContext(), N * sizeof( float ) );

    if ( !bufA.IsValid() || !bufB.IsValid() || !bufC.IsValid() )
    {
        std::cerr << "Failed to allocate one or more OpenCL buffers" << std::endl;
        return 1;
    }

    float* ptrA = static_cast<float*>( bufA.MapWrite( ocl.GetQueue() ) );
    float* ptrB = static_cast<float*>( bufB.MapWrite( ocl.GetQueue() ) );
    if ( !ptrA || !ptrB )
    {
        std::cerr << "Failed to map input buffers for write" << std::endl;
        return 1;
    }

    for ( int i = 0; i < N; ++i )
    {
        ptrA[ i ] = 1.5f;
        ptrB[ i ] = 2.5f;
    }
    bufA.Unmap( ocl.GetQueue(), ptrA );
    bufB.Unmap( ocl.GetQueue(), ptrB );

    cl_mem memA = bufA.GetMem();
    cl_mem memB = bufB.GetMem();
    cl_mem memC = bufC.GetMem();
    clSetKernelArg( kernel, 0, sizeof( cl_mem ), &memA );
    clSetKernelArg( kernel, 1, sizeof( cl_mem ), &memB );
    clSetKernelArg( kernel, 2, sizeof( cl_mem ), &memC );
    clSetKernelArg( kernel, 3, sizeof( int ), &N );

    size_t globalSize = N;
    size_t localSize = prog.ClampLocalWorkSize( kernel, 256 );

    cl_event event;
    cl_int err = clEnqueueNDRangeKernel( ocl.GetQueue(), kernel, 1, nullptr, &globalSize, &localSize, 0, nullptr, &event );
    if ( err != CL_SUCCESS )
    {
        std::cerr << "clEnqueueNDRangeKernel failed, error " << err << std::endl;
        return 1;
    }
    clWaitForEvents( 1, &event );

    double durationMs = OpenCLProfiling::GetEventDurationMs( event );
    std::cout << "1M Vector Addition GPU Execution Time: " << durationMs << " ms" << std::endl;

    // Spot-check the result by mapping the output buffer for read.
    float* ptrC = static_cast<float*>( bufC.MapRead( ocl.GetQueue() ) );
    if ( ptrC )
    {
        std::cout << "Result[0] = " << ptrC[ 0 ] << " (expected 4.0)" << std::endl;
        bufC.Unmap( ocl.GetQueue(), ptrC );
    }

    clReleaseEvent( event );
    std::cout << "OpenCL Compute Test Passed Successfully!" << std::endl;
    return 0;
}
