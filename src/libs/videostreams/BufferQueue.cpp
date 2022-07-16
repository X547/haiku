#include "BufferQueue.h"
#include <stdlib.h>


BufferQueue::BufferQueue(int32 maxLen):
	fItems((maxLen > 0) ? new int32[maxLen] : NULL),
	fBeg(0), fLen(0), fMaxLen(maxLen)
{}

void BufferQueue::SetMaxLen(int32 maxLen)
{
	fItems.SetTo((maxLen > 0) ? new int32[maxLen] : NULL);
	fMaxLen = maxLen;
	fBeg = 0; fLen = 0; fMaxLen = maxLen;
}


bool BufferQueue::Add(int32 val)
{
	if (!(fLen < fMaxLen))
		return false;
	fItems[(fBeg + fLen)%fMaxLen] = val;
	fLen++;
	return true;
}

int32 BufferQueue::Remove()
{
	if (!(fLen > 0))
		return -1;
	int32 res = fItems[fBeg%fMaxLen];
	fBeg = (fBeg + 1)%fMaxLen;
	fLen--;
	return res;
}

void BufferQueue::RemoveAt(int32 idx)
{
	if (!(idx <= fLen)) {
		abort();
	}
	for (int32 i = idx; i < fLen; i++) {
		fItems[(fBeg + i) % fMaxLen] = fItems[(fBeg + i + 1) % fMaxLen];
	}
	fLen--;
}

int32 BufferQueue::Begin()
{
	if (!(fLen > 0))
		return -1;
	return fItems[fBeg%fMaxLen];
}

int32 BufferQueue::ItemAt(int32 idx)
{
	return fItems[(fBeg + idx) % fMaxLen];
}

int32 BufferQueue::FindItem(int32 val)
{
	for (int32 i = 0; i < Length(); i++) {
		if (ItemAt(i) == val) return i;
	}
	return -1;
}
