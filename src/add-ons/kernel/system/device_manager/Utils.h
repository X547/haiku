#pragma once

#include <AutoDeleter.h>


#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}


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
