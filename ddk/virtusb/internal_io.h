#ifndef VIRTUSB_INTERNAL_IO_H
#define VIRTUSB_INTERNAL_IO_H

#include "virtusb.h"

NTSTATUS
VirtUsb_DispatchInternalDeviceControl(
	IN PDEVICE_OBJECT DeviceObject,
	IN PIRP           Irp
	);

#endif // !VIRTUSB_INTERNAL_IO_H
