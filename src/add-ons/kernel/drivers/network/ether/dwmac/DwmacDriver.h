#pragma once

#include <device_manager.h>
#include <stdlib.h>

#include <kernel.h>
#include <lock.h>
#include <util/AVLTree.h>
#include <util/iovec_support.h>

#include <AutoDeleterOS.h>

#include "DwmacRegs.h"
#include "CppUtils.h"


#define DWMAC_DEVICE_ID_GENERATOR	"dwmac/device_id"


class DwmacNetDevice;


enum {
	dmaMinAlign = 32,
	dwmacMaxPacketSize = ROUNDUP(1568, dmaMinAlign)
};


class DwmacDriver {
public:
	struct IdNodeDef {
		typedef int32 Key;
		typedef DwmacDriver Value;

		inline AVLTreeNode* GetAVLTreeNode(Value* value) const
		{
			return &value->fIdNode;
		}

		inline Value* GetValue(AVLTreeNode* node) const
		{
			return &ContainerOf(*node, &DwmacDriver::fIdNode);
		}

		inline int Compare(const Key& a, const Value* b) const
		{
			if (a < b->fId) return -1;
			if (a > b->fId) return 1;
			return 0;
		}

		inline int Compare(const Value* a, const Value* b) const
		{
			if (a->fId < b->fId) return -1;
			if (a->fId > b->fId) return 1;
			return 0;
		}
	};

	typedef AVLTree<IdNodeDef> IdMap;

	static float SupportsDevice(device_node* parent);
	static status_t RegisterDevice(device_node* parent);
	static status_t InitDriver(device_node* node, DwmacDriver*& driver);
	void UninitDriver();
	status_t RegisterChildDevices();

	DwmacNetDevice* NetDevice() {return fNetDevice;}
	void SetNetDevice(DwmacNetDevice* netDevice) {fNetDevice = netDevice;}

	status_t Start();
	status_t Stop();

	status_t MdioWaitIdle();
	status_t MdioRead(uint32 addr, uint32 reg, uint32& value);
	status_t MdioWrite(uint32 addr, uint32 reg, uint32 value);

	status_t Send(generic_io_vec* vector, size_t vectorCount);
	status_t Recv(generic_io_vec* vector, size_t vectorCount);

private:
	device_node* fNode {};
	int32 fId = -1;
	AVLTreeNode fIdNode;
	DwmacNetDevice* fNetDevice {};
	AreaDeleter fRegsArea;
	DwmacRegs* fRegs {};

	uint32 fClkTx {};
	uint32 fClkRmiiRtx {};

	AreaDeleter fDmaArea;
	void* fDmaAdr {};
	phys_addr_t fDmaPhysAdr {};

	DwmacDesc* fDescs {};
	uint32 fTxDescCnt {};
	uint32 fRxDescCnt {};
	uint32 fTxDescIdx {};
	uint32 fRxDescIdx {};
	void* fBuffers {};

	inline status_t InitDriverInt(device_node* node);

	status_t StartClocks();
	status_t StartResets();

	status_t InitDma();

	phys_addr_t ToPhysDmaAdr(void* adr) {return ((uint8*)adr - (uint8*)fDmaAdr) + fDmaPhysAdr;}
	DwmacDesc* GetTxDesc(uint32 idx) {return &fDescs[idx];}
	DwmacDesc* GetRxDesc(uint32 idx) {return &fDescs[fTxDescCnt + idx];}
	void* GetTxBuffer(uint32 idx) {return (uint8*)fBuffers + dwmacMaxPacketSize*idx;}
	void* GetRxBuffer(uint32 idx) {return (uint8*)fBuffers + dwmacMaxPacketSize*(fTxDescCnt + idx);}
};

class DwmacDevice {
};

class DwmacRoster {
public:
	static DwmacRoster& Instance() {return sInstance;}

	inline recursive_lock& Lock() {return fLock;}

	void Insert(DwmacDriver* driver) {fDrivers.Insert(driver);}
	void Remove(DwmacDriver* driver) {fDrivers.Remove(driver);}
	DwmacDriver* Lookup(int32 id) {return fDrivers.Find(id);}

private:
	static DwmacRoster sInstance;

	recursive_lock fLock = RECURSIVE_LOCK_INITIALIZER("DwmacRoster");
	DwmacDriver::IdMap fDrivers;
};
