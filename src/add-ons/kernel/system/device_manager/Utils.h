#pragma once

#include <AutoDeleter.h>


#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}


template <class T, class M>
static inline constexpr ptrdiff_t OffsetOf(const M T::*member)
{
	return reinterpret_cast<ptrdiff_t>(&(reinterpret_cast<T*>(0)->*member));
}


template <class T, class M>
static inline constexpr T& ContainerOf(M &ptr, const M T::*member)
{
	return *reinterpret_cast<T*>(reinterpret_cast<intptr_t>(&ptr) - OffsetOf(member));
}


template <typename Ptr, typename Deleter>
void FreeObjectPtr(Ptr &ptr, Deleter deleter)
{
	if (ptr != NULL) {
		deleter();
		ptr = NULL;
	}
}


struct BusDriverDeleter : MethodDeleter<BusDriver, void, &BusDriver::Free>
{
	typedef MethodDeleter<BusDriver, void, &BusDriver::Free> Base;

	BusDriverDeleter() : Base() {}
	BusDriverDeleter(BusDriver* object) : Base(object) {}
};


inline void
free_string(char* str)
{
	free(str);
}


typedef CObjectDeleter<char, void, free_string> CStringDeleter;
