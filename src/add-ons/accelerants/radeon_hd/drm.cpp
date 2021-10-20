#include "accelerant_protos.h"
#include <stdio.h>
#include <OS.h>


int radeon_drm_ioctl(int fd, unsigned long request, void* arg)
{
	printf("radeon_drm_ioctl()\n");
	return -1;
}

void* radeon_drm_map(void* addr, size_t length, int prot, int flags, int fd, off_t offset)
{
	printf("radeon_drm_map(%d, %#" B_PRIx64 ", %#" B_PRIxSIZE ")\n", fd, offset, length);
	void *resAddr = addr;
	area_id area = create_area("GPU maping", &resAddr, B_ANY_ADDRESS, length, B_NO_LOCK, B_READ_AREA | B_WRITE_AREA);
	if (area < B_OK)
		return NULL;
	return resAddr;
}

int radeon_drm_unmap(void* addr, size_t length)
{
	printf("radeon_drm_unmap(%#" B_PRIx64 ", %#" B_PRIxSIZE ")\n", (addr_t)addr, length);
	area_id area = area_for(addr);
	if (area < B_OK)
		return area;
	delete_area(area);
	return 0;
}
