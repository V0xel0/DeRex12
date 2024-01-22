#pragma once
//? Light RAII wrapper for "VM_Alloc" allocator, intended for 64bit OS, POD structures and atleast 64KB reservations and 4KB allocations
#include "VM_Dynamic_Alloc.hpp"
template<typename T>
struct VM_Array
{
	VM_Array(u64 maxAddressSpace, u64 elements )
	{
		vm_alloc_reserve(&origin, maxAddressSpace, elements);
	}

	T* origin = nullptr;

	inline void add_elements_0(u64 n)
	{
		vm_alloc_add(&origin, n);
	}

	inline void push(T el)
	{
		vm_alloc_push(&origin, el);
	}

	inline void pop()
	{
		VMAllocGetHeader(origin)->size--;
	}

	inline void erase_swap(u64 id)
	{
		assert(id < VMAllocGetSize(origin));
		origin[id] = *(vm_alloc_get_last(origin));
		pop();
	}

	inline u64 get_size()
	{
		return VMAllocGetSize(origin);
	}

	inline u64 get_capacity()
	{
		return VMAllocGetCapacity(origin);
	}

	inline T* begin()
	{
		return origin;
	}

	inline const T* begin() const
	{
		return origin;
	}

	inline T* last()
	{
		return vm_alloc_get_last(origin);
	}

	inline const T* last() const
	{
		return vm_alloc_get_last(origin);
	}

	inline T* end()
	{
		return origin + VMAllocGetSize(origin);
	}

	inline const T* end() const
	{
		return origin + VMAllocGetSize(origin);
	}

	inline T& operator[] (u64 i)
	{
		assert(i < VMAllocGetSize(origin)); return origin[i];
	}

	inline const T& operator[] (u64 i) const
	{
		assert(i < VMAllocGetSize(origin)); return origin[i];
	}
	
	~VM_Array()
	{
		for (u64 i = 0; i < VMAllocGetSize(origin); i++)
		{
			origin[i].~T();
		}
		vm_alloc_free(origin);
	}

	VM_Array(const VM_Array&) = delete;
	VM_Array(VM_Array&&) = delete;
	VM_Array& operator=(const VM_Array&) = delete;
	VM_Array& operator=(VM_Array&&) = delete;
};