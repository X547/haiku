#pragma once


class DeviceNode {
public:
	virtual void* QueryBusInterface(const char* ifaceName) = 0;
	virtual void* QueryDriverInterface(const char* ifaceName) = 0;

	template<typename Iface>
	inline Iface* QueryBusInterface();

protected:
	~DeviceNode() = default;
};


template<typename Iface>
inline Iface*
DeviceNode::QueryBusInterface()
{
	return (Iface*)QueryBusInterface(Iface::ifaceName);
}
