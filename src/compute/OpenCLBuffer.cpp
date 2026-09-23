#include "compute/OpenCLBuffer.h"

#include <iostream>

OpenCLBuffer::OpenCLBuffer( cl_context context, size_t sizeBytes, cl_mem_flags flags )
    : m_sizeBytes( sizeBytes )
{
    cl_int err = CL_SUCCESS;
    m_mem = clCreateBuffer( context, flags, sizeBytes, nullptr, &err );
    if ( err != CL_SUCCESS || !m_mem )
    {
        std::cerr << "[OpenCLBuffer] clCreateBuffer failed (size " << sizeBytes
                   << " bytes), error " << err << std::endl;
        m_mem = nullptr;
    }
}

OpenCLBuffer::~OpenCLBuffer()
{
    if ( m_mem )
    {
        clReleaseMemObject( m_mem );
        m_mem = nullptr;
    }
}

void* OpenCLBuffer::MapInternal( cl_command_queue queue, cl_map_flags mapFlags )
{
    if ( !m_mem )
        return nullptr;

    cl_int err = CL_SUCCESS;
    void* ptr = clEnqueueMapBuffer(
        queue, m_mem, CL_TRUE, mapFlags,
        0, m_sizeBytes,
        0, nullptr, nullptr, &err );

    if ( err != CL_SUCCESS || !ptr )
    {
        std::cerr << "[OpenCLBuffer] clEnqueueMapBuffer failed, error " << err << std::endl;
        return nullptr;
    }

    return ptr;
}

void* OpenCLBuffer::MapWrite( cl_command_queue queue )
{
    return MapInternal( queue, CL_MAP_WRITE );
}

void* OpenCLBuffer::MapRead( cl_command_queue queue )
{
    return MapInternal( queue, CL_MAP_READ );
}

void OpenCLBuffer::Unmap( cl_command_queue queue, void* mappedPtr )
{
    if ( !m_mem || !mappedPtr )
        return;

    cl_int err = clEnqueueUnmapMemObject( queue, m_mem, mappedPtr, 0, nullptr, nullptr );
    if ( err != CL_SUCCESS )
        std::cerr << "[OpenCLBuffer] clEnqueueUnmapMemObject failed, error " << err << std::endl;
}
