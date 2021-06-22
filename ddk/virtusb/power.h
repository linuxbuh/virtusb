#ifndef VIRTUSB_POWER_H
#define VIRTUSB_POWER_H

#include "virtusb.h"

NTSTATUS
VirtUsb_DispatchPower(
	IN PDEVICE_OBJECT DeviceObject,
	IN PIRP           Irp
	);

#endif // !VIRTUSB_POWER_H
