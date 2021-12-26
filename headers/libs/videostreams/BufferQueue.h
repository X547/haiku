#ifndef _BUFFERQUEUE_H_
#define _BUFFERQUEUE_H_

#include <SupportDefs.h>
#include <private/shared/AutoDeleter.h>


class BufferQueue {
private:
	ArrayDeleter<int32> fItems;
	int32 fBeg, fLen, fMaxLen;

public:
	BufferQueue(int32 maxLen = 0);
	void SetMaxLen(int32 maxLen);

	inline int32 Length() {return fLen;}
	bool Add(int32 val);
	int32 Remove();
	int32 Begin();
};


#endif	// _BUFFERQUEUE_H_
