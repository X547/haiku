#include "BufferQueue.h"


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

int32 BufferQueue::Begin()
{
	if (!(fLen > 0))
		return -1;
	return fItems[fBeg%fMaxLen];
}
