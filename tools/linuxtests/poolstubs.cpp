/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/
// poolstubs.cpp -- Ersatzimplementierungen fuer den Linux-Testlauf (tools/linuxtests/README.md).
//
// UnitTests bindet Common/System/AsciiString.cpp ein, und AsciiString holt seine Puffer ueber
// TheDynamicMemoryAllocator aus Common/System/GameMemory.cpp. GameMemory.cpp zieht ueber
// GlobalData.h/ClientRandomValue.h die halbe Engine mit -- genau deshalb steht in
// Tests/UnitTests/UnitTestStubs.cpp schon ein Durchreich-Stub fuer die new/delete-Overloads.
// Hier dasselbe eine Ebene tiefer: der Allokator reicht an malloc/free durch.
//
// Die Klassenlayouts kommen unveraendert aus den echten Headern; hier stehen nur die
// Implementierungen, die sonst GameMemory.cpp liefern wuerde. Fuer die numerischen Tests und
// den WAV-Parser genuegt das -- geprueft wird der Bestandscode, nicht der Speicherpool.

#include "PreRTS.h"
#include "Common/GameMemory.h"
#include "Common/CriticalSection.h"

#include <cstdlib>

static DynamicMemoryAllocator theTestAllocator;

// Live blocks of both allocators; tests compare it before and after an operation to find leaks.
long g_poolStubLiveBlocks = 0;
static MemoryPoolFactory theTestFactory;

// Die Engine haelt ihre Singletons in Engine::EngineContext (Engine/EngineContext.h). Die
// Testziele linken EngineContext.cpp nicht, der Kontext steht hier: als Aggregat mit konstanter
// Initialisierung, damit Allokator und Pool-Fabrik schon vor jeder dynamischen Initialisierung
// gesetzt sind (wie frueher die beiden Zeiger).
// Die Namen sind Makros auf den Kontext; fuer die Feldnamen hier kurz aufgehoben.
#pragma push_macro("TheDynamicMemoryAllocator")
#pragma push_macro("TheMemoryPoolFactory")
#undef TheDynamicMemoryAllocator
#undef TheMemoryPoolFactory
namespace Engine
{
EngineContext g_defaultContext = { .dynamicMemoryAllocator = &theTestAllocator, .memoryPoolFactory = &theTestFactory };
EngineContext * g_currentContext = &g_defaultContext;
}
#pragma pop_macro("TheMemoryPoolFactory")
#pragma pop_macro("TheDynamicMemoryAllocator")

// Die beiden Baeume deklarieren das Ding unterschiedlich -- Generals als Zeiger
// (CriticalSection*), Zero Hour als Objekt (FastCriticalSectionClass). decltype nimmt die
// Deklaration aus dem jeweiligen Header, dann passt dieselbe Zeile fuer beide. Im Namensraum
// wird nullinitialisiert, der Zeigerfall ist also nullptr wie gehabt.
decltype(TheAsciiStringCriticalSection) TheAsciiStringCriticalSection;

DynamicMemoryAllocator::DynamicMemoryAllocator() :
	m_factory(nullptr),
	m_nextDmaInFactory(nullptr),
	m_numPools(0),
	m_usedBlocksInDma(0),
	m_rawBlocks(nullptr)
{
	for (Int i = 0; i < MAX_DYNAMICMEMORYALLOCATOR_SUBPOOLS; ++i) {
		m_pools[i] = nullptr;
	}
}

DynamicMemoryAllocator::~DynamicMemoryAllocator()
{
}

void *DynamicMemoryAllocator::allocateBytesImplementation(Int numBytes DECLARE_LITERALSTRING_ARG2)
{
	void *p = allocateBytesDoNotZeroImplementation(numBytes PASS_LITERALSTRING_ARG2);
	if (p != nullptr) {
		::memset(p, 0, (size_t)numBytes);
	}
	return p;
}

void *DynamicMemoryAllocator::allocateBytesDoNotZeroImplementation(Int numBytes DECLARE_LITERALSTRING_ARG2)
{
	++m_usedBlocksInDma;
	++g_poolStubLiveBlocks;
	return ::malloc((size_t)numBytes);
}

void DynamicMemoryAllocator::freeBytes(void *pMem)
{
	if (pMem != nullptr) {
		--m_usedBlocksInDma;
		--g_poolStubLiveBlocks;
		::free(pMem);
	}
}

Int DynamicMemoryAllocator::getActualAllocationSize(Int numBytes)
{
	return numBytes;
}

// ---------------------------------------------------------------------------------------------
// MemoryPoolObject/Bucket/Overridable in UnitTests.cpp benutzen den Block-Allokator
// (MEMORY_POOL_GLUE). Auch der reicht hier nur durch; eine Blockgroesse verwaltet er nicht,
// weil kein Test sie prueft.
// ---------------------------------------------------------------------------------------------


MemoryPool *MemoryPoolFactory::createMemoryPool(const char *poolName, Int allocationSize,
												Int initialAllocationCount, Int overflowAllocationCount)
{
	MemoryPool *pool = (MemoryPool *)::calloc(1, sizeof(MemoryPool));
	pool->init(this, poolName, allocationSize, initialAllocationCount, overflowAllocationCount);
	return pool;
}

void MemoryPool::init(MemoryPoolFactory *factory, const char *poolName, Int allocationSize,
					  Int initialAllocationCount, Int overflowAllocationCount)
{
	// Nur die Blockgroesse wird gebraucht (allocateBlock); Blobs legt dieser Ersatz nie an.
	m_factory = factory;
	m_nextPoolInFactory = nullptr;
	m_poolName = poolName;
	m_allocationSize = allocationSize;
	m_initialAllocationCount = initialAllocationCount;
	m_overflowAllocationCount = overflowAllocationCount;
	m_usedBlocksInPool = 0;
	m_totalBlocksInPool = 0;
	m_peakUsedBlocksInPool = 0;
	m_firstBlob = nullptr;
	m_lastBlob = nullptr;
	m_firstBlobWithFreeBlocks = nullptr;
}

void *MemoryPool::allocateBlockImplementation(DECLARE_LITERALSTRING_ARG1)
{
	void *p = allocateBlockDoNotZeroImplementation(PASS_LITERALSTRING_ARG1);
	if (p != nullptr) {
		::memset(p, 0, (size_t)m_allocationSize);
	}
	return p;
}

void *MemoryPool::allocateBlockDoNotZeroImplementation(DECLARE_LITERALSTRING_ARG1)
{
	++g_poolStubLiveBlocks;
	return ::malloc((size_t)m_allocationSize);
}

void MemoryPool::freeBlock(void *pBlock)
{
	if (pBlock != nullptr) {
		--g_poolStubLiveBlocks;
	}
	::free(pBlock);
}

MemoryPoolFactory::MemoryPoolFactory() :
	m_firstPoolInFactory(nullptr),
	m_firstDmaInFactory(nullptr)
{
}

MemoryPoolFactory::~MemoryPoolFactory()
{
}
