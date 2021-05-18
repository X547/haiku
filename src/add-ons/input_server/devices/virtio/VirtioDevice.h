#ifndef _KEYBOARDDEVICE_H_
#define _KEYBOARDDEVICE_H_

#include <add-ons/input_server/InputServerDevice.h>
#include <InterfaceDefs.h>
#include <AutoDeleter.h>
#include <Handler.h>
#include <MessageRunner.h>

struct VirtioInputPacket;

struct KeyboardState {
	bigtime_t when;
	uint8 keys[256/8];
	uint32 modifiers;
};

struct TabletState {
	bigtime_t when;
	float x, y;
	float pressure;
	uint32 buttons;
	int32 clicks;
	int32 wheelX, wheelY;
};

class VirtioDevice: public BInputServerDevice
{
public:
	VirtioDevice();
	~VirtioDevice();

	status_t InitCheck();

	status_t Start(const char* name, void* cookie);
	status_t Stop(const char* name, void* cookie);

	status_t Control(const char* name, void* cookie, uint32 command, BMessage* message);

private:
	static int32 DeviceWatcher(void* arg);

	TabletState fState;

};


class VirtioHandler
{
public:
	VirtioHandler(VirtioDevice* dev, const char* name, input_device_type type);
	virtual ~VirtioHandler();
	inline VirtioDevice* Device() {return fDev;}
	inline input_device_ref* Ref() {return &fRef;}
	void SetFd(int fd);

	status_t Start();
	status_t Stop();

	static status_t Watcher(void* arg);

	virtual void Reset() = 0;
	virtual status_t Control(uint32 command, BMessage* message);
	virtual void PacketReceived(const VirtioInputPacket &pkt) = 0;

private:
	VirtioDevice* fDev;
	input_device_ref fRef;
	FileDescriptorCloser fDeviceFd;
	thread_id fWatcherThread;
	bool fRun;
};

class KeyboardHandler: public VirtioHandler
{
public:
	KeyboardHandler(VirtioDevice* dev, const char* name);
	~KeyboardHandler();
	virtual void Reset();
	virtual status_t Control(uint32 command, BMessage* message);
	virtual void PacketReceived(const VirtioInputPacket &pkt);

private:
	VirtioDevice* fDev;
	KeyboardState fState, fNewState;
	BPrivate::AutoDeleter<key_map, BPrivate::MemoryDelete> fKeyMap;
	BPrivate::AutoDeleter<char, BPrivate::MemoryDelete> fChars;
	
	bigtime_t fRepeatDelay;
	int32 fRepeatRate;
	thread_id fRepeatThread;
	sem_id fRepeatThreadSem;
	BMessage fRepeatMsg;

	void KeyString(uint32 code, char* str, size_t len);
	void StartRepeating(BMessage* msg);
	void StopRepeating();
	static status_t RepeatThread(void* arg);
	void StateChanged();
};

class TabletHandler: public VirtioHandler
{
public:
	TabletHandler(VirtioDevice* dev, const char* name);
	virtual void Reset();
	virtual status_t Control(uint32 command, BMessage* message);
	virtual void PacketReceived(const VirtioInputPacket &pkt);

private:
	VirtioDevice* fDev;
	TabletState fState, fNewState;
	bigtime_t fLastClick;
	int fLastClickBtn;
	
	bigtime_t fClickSpeed;
};


extern "C" _EXPORT BInputServerDevice*	instantiate_input_device();

#endif	// _KEYBOARDDEVICE_H_
