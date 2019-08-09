//============================================================================
// Name        : WAPSpoof.cpp
// Author      : Josh Stephenson
// Version     :
// Copyright   : Your copyright notice
// Description : Hello World in C++, Ansi-style
//============================================================================

#include <iostream>
#include <stdlib.h>
#include <string.h>
#include <atomic>
#include <pthread.h>
#include <unistd.h>

#define PAGE 4096

using byte = unsigned char;

class Spinlock
{
public:
	Spinlock() : locked(false)
	{

	}

	inline void Lock()
	{
		if(locked)
		{
			this->Spin();
		}
		else
		{
			locked = true;
			thread_id = (unsigned int)pthread_self();
		}
	}

	inline bool Release()
	{
		if((unsigned int)pthread_self() != thread_id)
		{
			return false;
		}

		locked = false;
		return true;
	}

	~Spinlock()
	{
		locked = false;
	}

private:
	bool locked;
	unsigned int thread_id;

	inline void Spin()
	{
		while(locked);
		this->Lock();
	}
};

template<typename T>
struct Stack
{
	T* Top;
	T* Base;
};

struct PDE
{
	unsigned int Present : 1;
	unsigned int ReadWrite : 1;
	unsigned int KeUsrMode : 1;
	unsigned int PageWriteThrough : 1;
	unsigned int PageCacheDisable : 1;
	unsigned int Accessed : 1;
	unsigned int Reserved1 : 1;
	unsigned int PageSizeExt : 1;
	unsigned int Reserved : 4;
	unsigned int PageTableBaseAddr : 20;
};

struct PD
{
	PDE entries[1024];
};

struct PTE
{
	bool Present()
	{
		uint32_t a = 0x12345679;
		return (bool)(a & 1);
	}

	unsigned int Present : 1;
	unsigned int ReadWrite : 1;
	unsigned int KeUsrMode : 1;
	unsigned int PageWriteThrough : 1;
	unsigned int PageCacheDisable : 1;
	unsigned int Accessed : 1;
	unsigned int Dirty : 1;
	unsigned int PAT : 1;
	unsigned int Global : 1;
	unsigned int Reserved : 3;
	unsigned int PageBaseAddr : 20;
};

struct PT
{
	PTE entries[1024];
};

void* pHeapMemory = calloc(((256 * 1024) * 1024) + 4096, 1);

void SetMaximumPhysicalMemory(unsigned int bytes)
{
	*((unsigned int*)pHeapMemory) = bytes;
}

unsigned int GetMaximumPhysicalMemory()
{
	 return *((unsigned int*)pHeapMemory);
}

bool InsertPDEntry(PD* pdp, unsigned int pdEntryIdx, PDE entry)
{
	if(pdEntryIdx > 1023 || !pdp) { return false; }
        pdp->entries[pdEntryIdx] = entry;
	return true;
}

bool InsertPTEntry(PD* pdp, unsigned int pdEntryIdx, unsigned int ptEntryIdx, PTE entry)
{
	if(ptEntryIdx > 1023 || !pdp) { return false; }
	PT* pPageTable = nullptr;

	if(pdp->entries[pdEntryIdx].Present)
	{
		unsigned int entryValue = *reinterpret_cast<unsigned int*>(&pdp->entries[pdEntryIdx]);
		pPageTable = (PT*)(pdp->entries[pdEntryIdx].PageTableBaseAddr << 12);
		std::cout << "Page Directory Entry -> " << pdEntryIdx << " (PT ptr addr: " << pPageTable << ") ";

	} else { return false; }

	pPageTable->entries[ptEntryIdx] = entry;
	std::cout << ": Page Table Entry -> " << ptEntryIdx << ": mapped to physical address: " << (void*)((int)pPageTable->entries[ptEntryIdx].PageBaseAddr << 12);
	return true;
}

int main(int argc, const char** argv)
{
	//Places a uint32 at beginning of heap memory, so we know how much memory is avaliable to be "mapped".
	SetMaximumPhysicalMemory(((256 * 1024) * 1024) - 4);

	//Page directory pointer... (this will be in CR3 register (PDBR) on x86 CPUs).
	PD* pdp = reinterpret_cast<PD*>(pHeapMemory); // pHeapMemory - This is our fake physical memory to map out.
	pdp = reinterpret_cast<PD*>(((unsigned int*)pdp + 1));

	//If the memory address held in the page directory pointer (pdp)
	//is not PAGE (4KB) aligned, then we align the address manually.
	if(((int)pdp) % PAGE != 0)
	{
		pdp = reinterpret_cast<PD*>(((int)pdp >> 12) << 12);
		pdp = reinterpret_cast<PD*>((int)pdp + 0x1000);
	}

	unsigned int maxPhysAddrSpace =  *(unsigned int*)pHeapMemory;
	std::cout << "Base Address: " << (void*)pHeapMemory << "\n";

	//Create a reversed stack for tracking Kernel physical memory allocation.
	//This allows for the kernel memory space to grow without corrupting any critical data structures...
	Stack<byte> KePhysAddrSpace = { 0 };
	KePhysAddrSpace.Base = (byte*)(&pdp[64 + 1]); //Adjust kernel's memory allocation stack base pointer for page mapping structures (Page Tables & Directory)...
	//Adjusted for 1 page directory and 64 page tables.
	//Usable address space = (Max Physical Address Space - (sizeof(Page Directory) + sizeof(Page Table))).
	KePhysAddrSpace.Top = KePhysAddrSpace.Base;

	//Page directory entry...
	PDE pde = { 0 };
	pde.Present = 1;
	pde.ReadWrite = 1;
	pde.KeUsrMode = 0;
	pde.PageWriteThrough = 1;
	pde.PageCacheDisable = 0;
	pde.Accessed = 0;
	pde.PageSizeExt = 0;

	//Keep only 20 most significant bits...
	pde.PageTableBaseAddr = ((unsigned int)&pdp[1].entries[0]) >> 12;

	//&pdp[0] = Page Directory address.
	//&pdp[1+] = Page Table address. (We can do this because,
	// the page table and page directory have the same underlying type. They just have different bit patterns)

	//Inserts the page directory entry into the page directory using the index specified in 2nd param.
	InsertPDEntry(pdp, 512, pde);

	//Page table entry...
	PTE pte = { 0 };
	pte.Present = 1;
	pte.ReadWrite = 1;
	pte.KeUsrMode = 0;
	pte.PageWriteThrough = 1;
	pte.PageCacheDisable = 0;
	pte.Accessed = 0;
	pte.Dirty = 0;
	pte.PAT = 0;
	pte.Global = 0;
	pte.PageBaseAddr = ((unsigned int)KePhysAddrSpace.Base) >> 12;

	//Unit Test: Maps out all pages for 64 page tables. 
	for(int y = 1; y <= 64; ++y)
	{
		pde.PageTableBaseAddr = ((unsigned int)&pdp[y].entries[0] >> 12);
		InsertPDEntry(pdp, y, pde);

		for(int i = 0; i < 1024; ++i)
		{
			pte.PageBaseAddr++;
			KePhysAddrSpace.Top += PAGE;
			InsertPTEntry(pdp, y, i, pte);
			std::cout << " [Allocated pages: " << (KePhysAddrSpace.Top - KePhysAddrSpace.Base) / PAGE << " (" << (KePhysAddrSpace.Top - KePhysAddrSpace.Base) << " bytes)]\n";
		}
	}

	getchar();
	return 0;
}
