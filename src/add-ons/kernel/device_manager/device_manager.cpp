// include for compile check
#include <dm2/device_manager.h>
#include <dm2/bus/PCI.h>
#include <dm2/bus/FDT.h>
#include <dm2/bus/I2C.h>

// TODO: stub


void DeviceNodeQueryTest(DeviceNode* node)
{
	I2cDevice* i2cDev = node->QueryBusInterface<I2cDevice>();
	I2cBus* i2cBus = node->QueryBusInterface<I2cBus>();

	PciDevice* pciDev = node->QueryBusInterface<PciDevice>();
	FdtDevice* fdtDev = node->QueryBusInterface<FdtDevice>();
}
