// include for compile check
#include <dm2/device_manager.h>

#include <dm2/bus/ACPI.h>
#include <dm2/bus/ATA.h>
#include <dm2/bus/FDT.h>
#include <dm2/bus/HID.h>
#include <dm2/bus/I2C.h>
#include <dm2/bus/MMC.h>
#include <dm2/bus/PCI.h>
#include <dm2/bus/PS2.h>
#include <dm2/bus/SCSI.h>
#include <dm2/bus/USB.h>
#include <dm2/bus/Virtio.h>


void DeviceNodeQueryTest(DeviceNode* node)
{
	I2cDevice* i2cDev = node->QueryBusInterface<I2cDevice>();
	I2cBus*    i2cBus = node->QueryDriverInterface<I2cBus>();

	PciDevice* pciDev = node->QueryBusInterface<PciDevice>();
	FdtBus*    fdtBus = node->QueryDriverInterface<FdtBus>();
	FdtDevice* fdtDev = node->QueryBusInterface<FdtDevice>();
}
