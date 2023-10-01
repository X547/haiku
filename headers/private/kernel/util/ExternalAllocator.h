/*
 * Copyright 2023, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _KERNEL_UTIL_EXTERNAL_ALLOCATOR_H
#define _KERNEL_UTIL_EXTERNAL_ALLOCATOR_H

#include <ContainerOf.h>
#include <util/AVLTree.h>


class ExternalAllocator {
public:
	~ExternalAllocator();

	status_t Register(uint64 adr, uint64 size);
	void Unregister(uint64 adr, uint64 size);

	[[nodiscard]] status_t Alloc(uint64& adr, uint64 size);
	[[nodiscard]] status_t AllocAligned(uint64& adr, uint64 size, uint64 align);
	[[nodiscard]] status_t AllocAt(uint64 adr, uint64 size);
	status_t Free(uint64 adr);
	status_t Free(uint64 adr, uint64 size);

	inline uint64 TotalSize() {return fTotalSize;}
	inline uint64 AllocSize() {return fAllocSize;}

private:
	class Block {
	public:
		struct AdrNodeDef {
			typedef uint64 Key;
			typedef Block Value;

			inline AVLTreeNode* GetAVLTreeNode(Value* value) const
			{
				return &value->fAdrNode;
			}

			inline Value* GetValue(AVLTreeNode* node) const
			{
				return &ContainerOf(*node, &Block::fAdrNode);
			}

			inline int Compare(const Key& a, const Value* b) const
			{
				if (a < b->fAdr) return -1;
				if (a > b->fAdr) return 1;
				return 0;
			}

			inline int Compare(const Value* a, const Value* b) const
			{
				if (a->fAdr < b->fAdr) return -1;
				if (a->fAdr > b->fAdr) return 1;
				return 0;
			}
		};

		struct SizeNodeDef {
			typedef uint64 Key;
			typedef Block Value;

			inline AVLTreeNode* GetAVLTreeNode(Value* value) const
			{
				return &value->fSizeNode;
			}

			inline Value* GetValue(AVLTreeNode* node) const
			{
				return &ContainerOf(*node, &Block::fSizeNode);
			}

			inline int Compare(const Key& a, const Value* b) const
			{
				if (a < b->fSize) return -1;
				if (a > b->fSize) return 1;
				return 0;
			}

			inline int Compare(const Value* a, const Value* b) const
			{
				if (a->fSize < b->fSize) return -1;
				if (a->fSize > b->fSize) return 1;
				if (a->fAdr < b->fAdr) return -1;
				if (a->fAdr > b->fAdr) return 1;
				return 0;
			}
		};

		uint64 fAdr;
		uint64 fSize;
		AVLTreeNode fAdrNode;
		AVLTreeNode fSizeNode;
		bool fAllocated = false;

		Block(uint64 adr, uint64 size): fAdr(adr), fSize(size) {}
	};

	AVLTree<Block::AdrNodeDef> fAdrMap;
	AVLTree<Block::SizeNodeDef> fSizeMap;
	uint64 fTotalSize {};
	uint64 fAllocSize {};
};

#endif	// _KERNEL_UTIL_EXTERNAL_ALLOCATOR_H
