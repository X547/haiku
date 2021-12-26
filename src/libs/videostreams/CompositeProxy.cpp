#include "CompositeProxy.h"


//#define CheckRet(err) {status_t _err = (err); if (_err < B_OK) return _err;}
inline void CheckRet(status_t res) {if (res < B_OK) {fprintf(stderr, "Error: %s\n", strerror(res)); abort();}}


CompositeProxy::CompositeProxy(const BMessenger& link): fLink(link)
{}


status_t CompositeProxy::NewSurface(BMessenger& cons, const char* name, const SurfaceUpdate& update)
{
	BMessage msg(compositeProducerNewSurfaceMsg);
	BMessage reply;
	CheckRet(msg.AddString("name", name));
	CheckRet(SetSurfaceUpdate(msg, update));
	CheckRet(fLink.SendMessage(&msg, &reply));
	int32 error;
	if (reply.FindInt32("error", &error) >= B_OK) CheckRet(error);
	CheckRet(reply.FindMessenger("cons", &cons));
	return B_OK;
}

status_t CompositeProxy::DeleteSurface(const BMessenger& cons)
{
	BMessage msg(compositeProducerDeleteSurfaceMsg);
	BMessage reply;
	CheckRet(msg.AddMessenger("cons", cons));
	CheckRet(fLink.SendMessage(&msg, &reply));
	int32 error;
	if (reply.FindInt32("error", &error) >= B_OK) CheckRet(error);
	return B_OK;
}

status_t CompositeProxy::GetSurface(const BMessenger& cons, SurfaceUpdate& update)
{
	BMessage msg(compositeProducerGetSurfaceMsg);
	BMessage reply;
	CheckRet(msg.AddMessenger("cons", cons));
	CheckRet(msg.AddUInt32("valid", update.valid));
	CheckRet(fLink.SendMessage(&msg, &reply));
	int32 error;
	if (reply.FindInt32("error", &error) >= B_OK) CheckRet(error);
	CheckRet(GetSurfaceUpdate(reply, update));
	return B_OK;
}

status_t CompositeProxy::UpdateSurface(const BMessenger& cons, const SurfaceUpdate& update)
{
	BMessage msg(compositeProducerUpdateSurfaceMsg);
	BMessage reply;
	CheckRet(msg.AddMessenger("cons", cons));
	CheckRet(SetSurfaceUpdate(msg, update));
	CheckRet(fLink.SendMessage(&msg, &reply));
	int32 error;
	if (reply.FindInt32("error", &error) >= B_OK) CheckRet(error);
	return B_OK;
}

status_t CompositeProxy::InvalidateSurface(const BMessenger& cons, const BRegion* dirty)
{
	BMessage msg(compositeProducerInvalidateSurfaceMsg);
	BMessage reply;
	CheckRet(msg.AddMessenger("cons", cons));
	CheckRet(SetRegion(msg, "dirty", dirty));
	CheckRet(fLink.SendMessage(&msg, &reply));
	int32 error;
	if (reply.FindInt32("error", &error) >= B_OK) CheckRet(error);
	return B_OK;
}


status_t CompositeProxy::Invalidate(const BRect dirty)
{
	return Invalidate(BRegion(dirty));
}

status_t CompositeProxy::Invalidate(const BRegion& dirty)
{
	BMessage msg(compositeProducerInvalidateMsg);
	BMessage reply;
	CheckRet(SetRegion(msg, "dirty", &dirty));
	CheckRet(fLink.SendMessage(&msg, &reply));
	int32 error;
	if (reply.FindInt32("error", &error) >= B_OK) CheckRet(error);
	return B_OK;
}
