#pragma once

#define CL_TARGET_OPENCL_VERSION 200

#include <CL/cl.h>
#include <cstddef>

// Zero-copy pinned host/GPU buffer for Intel integrated GPUs.
//
// Backed by CL_MEM_ALLOC_HOST_PTR, so on unified-memory Intel iGPU systems
// clEnqueueMapBuffer hands back a pointer directly into the same physical
// DDR4 pages the GPU reads/writes -- no PCIe copy, unlike a discrete GPU.
class OpenCLBuffer
{
public:
    // flags defaults to CL_MEM_READ_WRITE | CL_MEM_ALLOC_HOST_PTR, the
    // zero-copy unified-memory pattern this class exists for. Override
    // (e.g. to CL_MEM_READ_ONLY | CL_MEM_ALLOC_HOST_PTR) if a kernel only
    // ever reads or only ever writes this buffer.
    OpenCLBuffer( cl_context context, size_t sizeBytes,
                   cl_mem_flags flags = CL_MEM_READ_WRITE | CL_MEM_ALLOC_HOST_PTR );
    ~OpenCLBuffer();

    OpenCLBuffer( const OpenCLBuffer& ) = delete;
    OpenCLBuffer& operator=( const OpenCLBuffer& ) = delete;

    bool IsValid() const { return m_mem != nullptr; }

    // Maps the buffer for host writes. Blocking (CL_TRUE) since the
    // pattern here is write-then-launch-kernel; there is nothing useful
    // to overlap it with. Returns nullptr on failure.
    void* MapWrite( cl_command_queue queue );

    // Maps the buffer for host reads (e.g. after a kernel has run and
    // you want to read back results). Blocking. Returns nullptr on
    // failure.
    void* MapRead( cl_command_queue queue );

    // Unmaps a pointer previously returned by MapWrite()/MapRead().
    void Unmap( cl_command_queue queue, void* mappedPtr );

    cl_mem GetMem() const { return m_mem; }
    size_t GetSizeBytes() const { return m_sizeBytes; }

private:
    void* MapInternal( cl_command_queue queue, cl_map_flags mapFlags );

    cl_mem m_mem = nullptr;
    size_t m_sizeBytes = 0;
};
