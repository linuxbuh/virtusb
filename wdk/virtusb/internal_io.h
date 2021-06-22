#ifndef VIRTUSB_INTERNAL_IO_H
#define VIRTUSB_INTERNAL_IO_H

#include "virtusb.h"

NTSTATUS
VirtUsb_DispatchInternalDeviceControl(
	_In_ PDEVICE_OBJECT DeviceObject,
	_In_ PIRP           Irp
	);

#endif // !VIRTUSB_INTERNAL_IO_H
