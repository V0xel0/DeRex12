#pragma once

#include <cassert>
#include <cstring>

#include "Utils.hpp"

//TODO: CUSTOM MEMSET
//TODO: Consider removing memsetting or leaving it only for reset 

template <typename T>
[[nodiscard]]
inline T *push_type(auto *allocator, u32 count = 1)
{
	return ( T*)allocate(allocator, sizeof(T) * count, alignof(T));
} 

struct Alloc_Arena_Temp
{
	u64 curr_offset;
	u64 prev_offset;
};

struct Alloc_Arena
{
	u64 max_size;
	byte *base;

	u64 curr_offset;
	u64 prev_offset;
	Alloc_Arena_Temp temp_start;
};

[[nodiscard]]
inline Alloc_Arena arena_from_allocator(auto* allocator, const u64 max_size)
{
	return { max_size, (byte *)allocate(allocator, max_size) };
}

[[nodiscard]]
inline void *allocate(Alloc_Arena *arena, const u64 size_bytes, const u64 alignment = alignof(u64))
{
	arena->curr_offset = AlignAddressPow2((u64)arena->base + arena->curr_offset, alignment);
	arena->curr_offset -= (u64)arena->base;
	assert( ( (arena->curr_offset + size_bytes) <= arena->max_size) && "No more memory!" );

	void *out = (void*) ((byte*)arena->base + arena->curr_offset);
	arena->prev_offset = arena->curr_offset;
	arena->curr_offset += size_bytes;

	return out;
}

[[nodiscard]]
inline void *arena_resize_last(Alloc_Arena *arena, void *old_memory, const u64 size_bytes)
{
	assert((byte *)old_memory == arena->base + arena->prev_offset && "This is not last allocated memory");
	assert( ( (arena->curr_offset + size_bytes) <= arena->max_size) && "No more memory!" );

	auto new_size = arena->prev_offset + size_bytes;
	auto old_size = arena->curr_offset - arena->prev_offset;

	if(new_size < old_size)
		memset(arena->base + new_size, 0, old_size - new_size);
	arena->curr_offset = new_size;
	
	return old_memory;
}

//?(doc) Null terminated ASCII string assumed
[[nodiscard]]
inline auto arena_push_string(Alloc_Arena *arena, const char *string)
{
	assert(arena);
	struct Output {
		char *p;
		s32 size;
	};

	s32 size = 1;
	for(const char *at = string; *at; at++)
		size++;

	char *src = (char *)allocate(arena, size);

	for(s32 i = 0; i < size; i++)
		src[i] = string[i];

	return Output{src, size};
}

inline void arena_start_temp(Alloc_Arena *arena)
{
	assert(arena);
	arena->temp_start.curr_offset = arena->curr_offset;
	arena->temp_start.prev_offset = arena->prev_offset;
}

inline void arena_end_temp(Alloc_Arena *arena)
{
	assert(arena);
	memset(arena->base + arena->prev_offset, 0, arena->curr_offset - arena->prev_offset);
	arena->curr_offset = arena->temp_start.curr_offset;
	arena->prev_offset = arena->temp_start.prev_offset;
}

inline void arena_reset(Alloc_Arena *arena)
{
	assert(arena);
	memset(arena->base, 0, arena->max_size);
	arena->curr_offset = 0;
	arena->prev_offset = 0;
}

struct Alloc_Stack_Header
{
	u64 prev_offset;
	u64 prev_header;
};

struct Alloc_Stack
{
	u64 max_size;
	byte *base;

	u64 curr_offset;
	u64 last_header_offset;
};

[[nodiscard]]
inline Alloc_Stack stack_from_allocator(auto* allocator, const u64 max_size)
{
	return { max_size, (byte *)allocate(allocator, max_size) };
}

[[nodiscard]]
inline void *allocate(Alloc_Stack *stack, const u64 size_bytes, const u64 alignment = alignof(u64))
{
	auto header_start = AlignAddressPow2((u64)stack->base + stack->curr_offset, alignof(Alloc_Stack_Header));
	header_start -= (u64)stack->base;
	Alloc_Stack_Header *new_header = (Alloc_Stack_Header *)((byte *)stack->base + header_start);
	new_header->prev_offset = stack->curr_offset;
	new_header->prev_header = stack->last_header_offset;
	stack->last_header_offset = header_start;

	auto data_offset =  AlignAddressPow2((u64)stack->base + header_start + sizeof(Alloc_Stack_Header), alignment);
	data_offset -= (u64)stack->base;
	assert( ( (data_offset + size_bytes) <= stack->max_size) && "No more memory!" );

	void *out = (void*) ((byte*)stack->base + data_offset);
	stack->curr_offset = size_bytes + data_offset;

	return out;
}

inline void stack_pop(Alloc_Stack *stack)
{
	assert(stack->curr_offset > 0 && "Stack is empty");
	Alloc_Stack_Header *currHeader = (Alloc_Stack_Header *)((byte *)stack->base + stack->last_header_offset);

	stack->curr_offset = currHeader->prev_offset;
	stack->last_header_offset = currHeader->prev_header;
}

inline void stack_reset(Alloc_Stack *stack)
{
	assert(stack);
	memset(stack->base, 0, stack->max_size);
	stack->curr_offset = 0;
	stack->last_header_offset = 0;
}

struct Pool_Free_Node
{
	u64 next;
};

struct Alloc_Pool
{
	u64 max_size;
	byte *base;

	u64 block_size;
	u64 head_block;
};

[[nodiscard]]
inline Pool_Free_Node *get_node(Alloc_Pool *pool, const u64 offset)
{
	assert(offset != pool->max_size / pool->block_size && "No more free blocks!");
	return (Pool_Free_Node *)(pool->base + offset * pool->block_size);
}
	
inline void reset_list(Alloc_Pool *pool)
{
	u64 block_count = pool->max_size / pool->block_size;

	for (u64 i = 0; i < block_count; i++)
	{
		Pool_Free_Node *node = get_node(pool, i);
		node->next = pool->head_block;
		pool->head_block = i;
	}
}

[[nodiscard]]
inline Alloc_Pool create_pool(byte *const mem_buffer, const u64 max_size, const u64 block_size, const u64 alignment)
{
	assert(block_size <= max_size && "Block is bigger than max size!");
	assert(block_size >= sizeof(Pool_Free_Node) && "Block size is too small - minimum size is 8 bytes!");
	assert(alignment % 2 == 0 && "Alignment is not power of 2 base!");

	byte *aligned_mem = (byte *)(AlignAddressPow2((u64)mem_buffer, alignment));
	u64 aligned_size = max_size - (u64)(aligned_mem - mem_buffer);
	u64 aligned_block = AlignAddressPow2(block_size, alignment); //Align address is fine also, changed to it to prevent compiler warning
	u64 head = aligned_size / aligned_block; // block_count that means end of list

	Alloc_Pool out = {aligned_size, aligned_mem, aligned_block, head};
	reset_list(&out);

	return out;
}

[[nodiscard]]
inline Alloc_Pool pool_from_allocator(auto* allocator, const u64 max_size_bytes, const u64 block_size, const u64 alignment)
{
	return create_pool((byte *)allocate(allocator, max_size_bytes), max_size_bytes, block_size, alignment);
}

//? The allocation must fit in a single block size!
//?(note) Already tried to have allocations that spans through multiple blocks - its dumb for this type of allocator!(freeing)
[[nodiscard]]
inline void *allocate(Alloc_Pool *pool, const u64 size_bytes, const u64 alignment = alignof(u64))
{
	assert(size_bytes <= pool->block_size && "Too much elements for a single block!");
	assert(alignment <= pool->block_size && "Alignment is bigger than block size!");

	Pool_Free_Node *head_node = get_node(pool, pool->head_block);
	pool->head_block = head_node->next;
	
	return memset(head_node, 0, pool->block_size);
}
//? Adding to a pool list
inline void free_block(Alloc_Pool *pool, void *ptr)
{
	if (ptr == nullptr)
		return;
	
	byte *end = pool->base + pool->max_size;
	assert(((byte *)ptr < end ) && ((byte *)ptr >= pool->base) && "Provided memory addres is out of bounds!");
	assert((u64)ptr % pool->block_size == 0 && "The address is offsetted - is not a block beginning!");
	
	Pool_Free_Node *head_node = (Pool_Free_Node *)ptr;
	head_node->next = pool->head_block;
	pool->head_block = (u64)(head_node - (Pool_Free_Node *)pool->base);
}