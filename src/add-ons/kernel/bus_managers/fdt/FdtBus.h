#pragma once

#include <dm2/bus/FDT.h>

#include <AutoDeleter.h>
#include <HashMap.h>
#include <util/Vector.h>


//#define TRACE_FDT
#ifdef TRACE_FDT
#define TRACE(x...) dprintf(x)
#else
#define TRACE(x...)
#endif


#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}


class FdtBusImpl;

class FdtInterruptMapImpl: public FdtInterruptMap {
public:
	FdtInterruptMapImpl(FdtBusImpl* bus): fBus(bus) {}
	virtual ~FdtInterruptMapImpl() = default;

	void Print() final;
	uint32 Lookup(uint32 childAddr, uint32 childIrq) final;

private:
	friend class FdtDeviceImpl;

	struct MapEntry {
		uint32 childAddr;
		uint32 childIrq;
		uint32 parentIrqCtrl;
		const uint32* parentIrqData;
		uint32 parentIrqCells;
	};

	uint32_t fChildAddrMask;
	uint32_t fChildIrqMask;

	FdtBusImpl* fBus;
	Vector<MapEntry> fInterruptMap;
};


class FdtBusImpl: public DeviceDriver, public FdtBus {
public:
	FdtBusImpl(DeviceNode* node): fNode(node) {}
	virtual ~FdtBusImpl() = default;

	DeviceNode* GetNode() {return fNode;}
	const void* GetFDT() {return fFDT.Get();}
	status_t TranslateInterrupt(uint32 intrParent, const uint32* intrData, uint32 intrCells, int32* vector);

	// DeviceDriver
	static status_t Probe(DeviceNode* node, DeviceDriver** driver);
	void Free() final;
	void* QueryInterface(const char* name) final;

	// FdtBus
	DeviceNode* NodeByPhandle(int phandle) final;

private:
	DeviceNode* fNode;
	MemoryDeleter fFDT;
	HashMap<HashKey32<int32>, DeviceNode*> fPhandles;

	status_t Init();
	status_t Traverse(int &node, int &depth, DeviceNode* parentDev);
	status_t RegisterNode(int node, DeviceNode* parentDev, DeviceNode*& curDev);
};


class FdtDeviceImpl: public BusDriver, public FdtDevice {
public:
	FdtDeviceImpl(FdtBusImpl* bus, int fdtNode): fBus(bus), fFdtNode(fdtNode) {}
	virtual ~FdtDeviceImpl() = default;

	// BusDriver
	void Free() final;
	status_t InitDriver(DeviceNode* node) final;
	void* QueryInterface(const char* name) final;

	// FdtDevice
	DeviceNode* GetBus() final;
	const char* GetName() final;
	const void* GetProp(const char* name, int* len) final;
	bool GetReg(uint32 ord, uint64* regs, uint64* len) final;
	status_t GetRegByName(const char* name, uint64* regs, uint64* len) final;
	bool GetInterrupt(uint32 ord, DeviceNode** interruptController, uint64* interrupt) final;
	status_t GetInterruptByName(const char* name, DeviceNode** interruptController, uint64* interrupt) final;
	status_t GetInterruptVector(uint32 ord, int32* vector) final;
	status_t GetInterruptVectorByName(const char* name, int32* vector) final;
	FdtInterruptMap* GetInterruptMap() final;

	status_t GetClock(uint32 ord, ClockDevice** clock) final;
	status_t GetClockByName(const char* name, ClockDevice** clock) final;
	status_t GetReset(uint32 ord, ResetDevice** reset) final;
	status_t GetResetByName(const char* name, ResetDevice** reset) final;

	status_t BuildAttrs(Vector<device_attr>& attrs);

private:
	FdtBusImpl* fBus;
	int fFdtNode = -1;
	DeviceNode* fNode {};

	ObjectDeleter<FdtInterruptMapImpl> fInterruptMap;

	int GetFdtNode();
	bool GetInterruptInt(uint32 index, uint32& intrParent, const uint32*& intrData, uint32& intrCells);
};
