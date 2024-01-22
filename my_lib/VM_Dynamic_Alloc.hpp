//? -----------------------------------------------------------------------------------------------
//? DYNAMIC ALLOCATOR/ARRAY WITH REQUIRED PRE-RESERVATION. IT WILL GROW BY MULTIPLIES OF 4KB (PAGE SIZE)
//? UP TO MAXIMUM SPECIFIED DURING RESERVATION. THE MINIMAL COMMIT (ACTUAL SIZE) WILL BE 4 KB! 
//? THE RESERVE FUNCTION MUST BE CALLED FIRST! THE MINIMAL RESERVATION SHOULD BE MULTIPLE OF 64KB!
//? USE AS LAZY GENERAL ALLOCATON METHOD ONLY IF YOU CAN GUARANTEE 64BIT PLATFORM!
//? -----------------------------------------------------------------------------------------------

//? It does allocations of one type therefore it is not a typeless allocator 'per se' but works more like dynamic array
//? The below design is inspired by stb library "stretchy buffer" idea where data to maintain allocation state is held in the 
//? same place as the actual data. 

//TODO: Rewrite it to match API of other allocators
#pragma once
#include <cassert>
#include "external/Minimal_Windows.hpp"

#include "Utils.hpp"

struct VM_Alloc_Header
{
	u64 size;
	u64 capacity;
};

#define VMAllocGetHeader(a) ((VM_Alloc_Header*)((char*)(a) - sizeof(VM_Alloc_Header)))
#define VMAllocGetSize(a) ((a) ? VMAllocGetHeader(a)->size : 0)
#define VMAllocGetCapacity(a) ((a) ? VMAllocGetHeader(a)->capacity : 0)
#define VMAllocIsFull(a) ((VMAllocGetSize(a))==(VMAllocGetCapacity(a)))
#define VMAllocIsEmpty(a) ((VMAllocGetSize(a))==0)

//? Commit memory and init it to 0, DO NOT CALL THIS FUNCTION DIRECTLY
template<typename T>
inline T* vm_alloc_grow(T* arr, u64 n, u64 initSize = 0)
{
	u64 currentCapacity = 0;
	u64 size = 0;
	u64 growBy = 0;
	VM_Alloc_Header* h;

	if (arr == nullptr)
	{
		size = 0;
		growBy = ( sizeof(T) * n + sizeof(VM_Alloc_Header) + (KiB(4) - 1) ) & -KiB(4);
		currentCapacity = ( growBy - sizeof(VM_Alloc_Header) ) / sizeof(T);
		assert(growBy < initSize && "Reached max reservation size limit");
		arr = (T*)VirtualAlloc(nullptr, initSize, MEM_RESERVE, PAGE_READWRITE);
		h = (VM_Alloc_Header*)arr;
	}
	else
	{
		// Minimum elements required
		currentCapacity = VMAllocGetCapacity(arr) * 2 + n;
		size = VMAllocGetSize(arr);
		h = VMAllocGetHeader(arr);
		// Grow rounded up to next multiple of page size.
		growBy = ( sizeof(T) * currentCapacity + sizeof(VM_Alloc_Header) + (KiB(4) - 1) ) & -KiB(4);
		// Effective capacity after rounding up
		currentCapacity = ( growBy - sizeof(VM_Alloc_Header) ) / sizeof(T);
	}

	arr = (T*)VirtualAlloc(h, growBy, MEM_COMMIT, PAGE_READWRITE);
	assert(arr && "Failed to allocate memory");
	arr = (T*)((char*)arr + sizeof(VM_Alloc_Header));

	h = VMAllocGetHeader(arr);
	h->capacity = currentCapacity;
	h->size = size;

	return arr;
}

//? Push elemens to vrarray, pass by copy!
template<typename T>
inline void vm_alloc_push(T** arr, T el)
{
	assert((*arr) != nullptr && "Tried to push to NULL vrarray");
	if (VMAllocIsFull(*arr))
	{
		(*arr) = vm_alloc_grow((*arr), 0);
	}
	(*arr)[VMAllocGetHeader((*arr))->size++] = el;
}

//? Reserves max address space from windows (max space to which array could grow),
//? and commits capacity of elements rounded up to  next 4KB page boundry (it will always commit atleast 4KB)
template<typename T>
inline bool vm_alloc_reserve(T** arr, u64 maxAdressSpace, u64 elements)
{
	assert(maxAdressSpace > KiB(64) && "Trying to reserve less than granularity");

	if ((*arr) == nullptr)
	{
		(*arr) = vm_alloc_grow((*arr), elements, maxAdressSpace);
		return 0;
	}
	return 1;
}

//? Adds 'n' elements to the array, 0 initialized
template<typename T>
inline void vm_alloc_add(T** arr, u64 n)
{
	assert((*arr) != nullptr && "Tried to add elements to NULL vrarray");
	if ((VMAllocGetSize((*arr)) + n >= VMAllocGetCapacity((*arr))))
	{
		(*arr) = vm_alloc_grow((*arr), n);
	}
	(VMAllocGetHeader((*arr))->size += n);
}

template<typename T>
inline void vm_alloc_pop(T* arr)
{
	assert(arr != nullptr && VMAllocGetSize(arr) != 0 && "Tried to pop NULL or empty vrarray");
	VMAllocGetHeader(arr)->size--;
}

template<typename T>
[[nodiscard]]
inline T* vm_alloc_get_last(T* arr)
{
	if (arr == nullptr || VMAllocGetSize(arr) == 0)
	{
		return nullptr;
	}
	return &arr[VMAllocGetSize(arr) - 1];
}

template<typename T>
[[nodiscard]]
inline T* vm_alloc_get_end(T* arr)
{
	if (arr == nullptr || VMAllocGetCapacity(arr) == 0)
	{
		return nullptr;
	}
	return &arr[VMAllocGetCapacity(arr) - 1];
}

template<typename T>
inline bool vm_alloc_free(T* arr)
{
	return !VirtualFree(VMAllocGetHeader(arr), 0, MEM_RELEASE);
}