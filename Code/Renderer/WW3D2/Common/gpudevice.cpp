/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// gpudevice.cpp -- the device-neutral parts of gpudevice.h.

#include "gpudevice.h"

#include <new>

namespace {

/// A blob in main memory: header and bytes in one allocation.
class Memory_Blob final : public GpuBlob
{
public:
	explicit Memory_Blob(SIZE_T size) : m_size(size), m_refs(1) {}
	ULONG AddRef() override { return ++m_refs; }
	ULONG Release() override
	{
		const ULONG refs = --m_refs;
		if (refs == 0) {
			this->~Memory_Blob();
			::operator delete(this);
		}
		return refs;
	}
	void* Native() override { return nullptr; }
	void* GetBufferPointer() override { return this + 1; }
	SIZE_T GetBufferSize() override { return m_size; }
private:
	~Memory_Blob() override {}
	SIZE_T	m_size;
	ULONG	m_refs;
};

}  // namespace

GpuBlob* Gpu_Create_Blob(SIZE_T size)
{
	void* memory = ::operator new(sizeof(Memory_Blob) + size, std::nothrow);
	return memory ? new (memory) Memory_Blob(size) : nullptr;
}
