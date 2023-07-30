#pragma once

#include <dm2/bus/FDT.h>

#include <HashMap.h>
#include <util/Vector.h>


//#define TRACE_FDT
#ifdef TRACE_FDT
#define TRACE(x...) dprintf(x)
#else
#define TRACE(x...)
#endif


#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}


class FdtBusImpl: public DeviceDriver, public FdtBus {
public:
	FdtBusImpl(DeviceNode* node): fNode(node) {}
	virtual ~FdtBusImpl() = default;

	// DeviceDriver
	static status_t Probe(DeviceNode* node, DeviceDriver** driver);
	void Free() final;
	void* QueryInterface(const char* name) final;
	status_t RegisterChildDevices() final;

	// FdtBus
	DeviceNode* NodeByPhandle(int phandle) final;

private:
	DeviceNode* fNode;
	HashMap<HashKey32<int32>, DeviceNode*> fPhandles;

	void Traverse(int &node, int &depth, DeviceNode* parentDev);
	status_t RegisterNode(int node, DeviceNode* parentDev, DeviceNode*& curDev);
};


class FdtDeviceImpl: public BusDriver, public FdtDevice {
public:
	FdtDeviceImpl(DeviceNode* busNode, int fdtNode): fBusNode(busNode), fFdtNode(fdtNode) {}
	virtual ~FdtDeviceImpl() = default;

	// BusDriver
	status_t InitDriver(DeviceNode* node) final;
	void Free() final;
	const device_attr* Attributes() const final;
	void* QueryInterface(const char* name) final;

	// FdtDevice
	DeviceNode* GetBus() final;
	const char* GetName() final;
	const void* GetProp(const char* name, int* len) final;
	bool GetReg(uint32 ord, uint64* regs, uint64* len) final;
	bool GetInterrupt(uint32 ord, DeviceNode** interruptController, uint64* interrupt) final;
	FdtInterruptMap* GetInterruptMap() final;

private:
	DeviceNode* fNode {};
	DeviceNode* fBusNode {};
	int fFdtNode = -1;
	Vector<device_attr> fAttrs;

	int GetFdtNode();
};


class FdtInterruptMapImpl: public FdtInterruptMap {
public:
	virtual ~FdtInterruptMapImpl() = default;

	void Print() final;
	uint32 Lookup(uint32 childAddr, uint32 childIrq) final;

private:
	struct MapEntry {
		uint32_t childAddr;
		uint32_t childIrq;
		uint32_t parentIrqCtrl;
		uint32_t parentIrq;
	};

	uint32_t fChildAddrMask;
	uint32_t fChildIrqMask;

	Vector<MapEntry> fInterruptMap;
};
