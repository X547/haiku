#pragma once


class CacheController {
public:
	virtual uint32 CacheBlockSize() = 0;
	virtual void FlushCache(phys_addr_t adr) = 0;

protected:
	~CacheController() = default;
};


extern "C" {
status_t install_cache_controller(CacheController* ctrl);
void uninstall_cache_controller(CacheController* ctrl);
}
