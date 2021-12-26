#ifndef _COMPOSITEPROXY_H_
#define _COMPOSITEPROXY_H_

#include "CompositeProducer.h"


class _EXPORT CompositeProxy final {
private:
	BMessenger fLink;

public:
	CompositeProxy(const BMessenger& link);

	status_t NewSurface(BMessenger& cons, const char* name, const SurfaceUpdate& update);
	status_t DeleteSurface(const BMessenger& cons);
	status_t GetSurface(const BMessenger& cons, SurfaceUpdate& update);
	status_t UpdateSurface(const BMessenger& cons, const SurfaceUpdate& update);
	status_t InvalidateSurface(const BMessenger& cons, const BRegion* dirty);

	status_t Invalidate(const BRect dirty);
	status_t Invalidate(const BRegion& dirty);
};


#endif	// _COMPOSITEPROXY_H_
