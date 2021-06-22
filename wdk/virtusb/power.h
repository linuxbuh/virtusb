#ifndef VIRTUSB_POWER_H
#define VIRTUSB_POWER_H

#include "virtusb.h"

NTSTATUS
VirtUsb_DispatchPower(
	_In_ PDEVICE_OBJECT DeviceObject,
	_In_ PIRP           Irp
	);

#endif // !VIRTUSB_POWER_H
