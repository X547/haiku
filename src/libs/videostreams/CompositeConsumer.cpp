#include "CompositeConsumer.h"
#include "VideoBufferUtils.h"
#include <stdio.h>


CompositeConsumer::CompositeConsumer(const char* name, CompositeProducer* base, Surface* surface):
	VideoConsumer(name),
	fBase(base),
	fSurface(surface)
{
	printf("+CompositeConsumer: "); WriteMessenger(BMessenger(this)); printf("\n");
}

CompositeConsumer::~CompositeConsumer()
{
	printf("-CompositeConsumer: "); WriteMessenger(BMessenger(this)); printf("\n");
}
