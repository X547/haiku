#ifndef _COMPOSITECONSUMER_H_
#define _COMPOSITECONSUMER_H_

#include <Region.h>

#include <VideoConsumer.h>

class CompositeProducer;
class Surface;


class _EXPORT CompositeConsumer: public VideoConsumer
{
private:
	CompositeProducer* fBase;
	Surface* fSurface;

public:
	CompositeConsumer(const char* name, CompositeProducer* base, Surface* surface);
	virtual ~CompositeConsumer();

	inline class CompositeProducer* Base() {return fBase;}
	inline class Surface* GetSurface() {return fSurface;}

	virtual void Draw(int32 bufferId, const BRegion& dirty) = 0;
};

#endif	// _COMPOSITECONSUMER_H_
