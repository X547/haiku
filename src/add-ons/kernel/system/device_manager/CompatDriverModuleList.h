#pragma once

#include <SupportDefs.h>

#include <dm2/device_manager.h>

#include <util/AVLTree.h>
#include <util/Vector.h>

#include "Utils.h"

class DriverModuleInfo;


class CompatDriverModuleList {
public:
	int32 Count();
	const char* ModuleNameAt(int32 index);

	void Clear();
	void Insert(DriverModuleInfo* module, float score);
	void Remove(DriverModuleInfo* module);

private:
	class CompatInfo {
	private:
		struct NameNodeDef {
			typedef const char* Key;
			typedef CompatInfo Value;

			inline AVLTreeNode* GetAVLTreeNode(Value* value) const
			{
				return &value->fNameNode;
			}

			inline Value* GetValue(AVLTreeNode* node) const
			{
				return &ContainerOf(*node, &CompatInfo::fNameNode);
			}

			inline int Compare(const Key& a, const Value* b) const
			{
				return strcmp(a, b->GetName());
			}

			inline int Compare(const Value* a, const Value* b) const
			{
				return strcmp(a->GetName(), b->GetName());
			}
		};

		struct ScoreNodeDef {
			typedef float Key;
			typedef CompatInfo Value;

			inline AVLTreeNode* GetAVLTreeNode(Value* value) const
			{
				return &value->fScoreNode;
			}

			inline Value* GetValue(AVLTreeNode* node) const
			{
				return &ContainerOf(*node, &CompatInfo::fScoreNode);
			}

			inline int Compare(const Key& a, const Value* b) const
			{
				if (a < b->fScore)
					return 1;

				if (a > b->fScore)
					return -1;

				return 0;
			}

			inline int Compare(const Value* a, const Value* b) const
			{
				if (a->fScore < b->fScore)
					return 1;

				if (a->fScore > b->fScore)
					return -1;

				return strcmp(a->GetName(), b->GetName());
			}
		};

	public:
		typedef AVLTree<NameNodeDef> NameMap;
		typedef AVLTree<ScoreNodeDef> ScoreMap;

		CompatInfo(DriverModuleInfo* module, float score): fModule(module), fScore(score) {}
		const char* GetName() const;
		float GetScore() const {return fScore;}
		void SetScore(float score) {fScore = score;}

	private:
		AVLTreeNode fNameNode;
		AVLTreeNode fScoreNode;
		DriverModuleInfo* fModule;
		float fScore;
	};

private:
	CompatInfo::NameMap fModules;
	CompatInfo::ScoreMap fModuleScores;
};
