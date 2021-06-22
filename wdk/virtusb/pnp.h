#ifndef VIRTUSB_PNP_H
#define VIRTUSB_PNP_H

#include "virtusb.h"

NTSTATUS
VirtUsb_DispatchPnP(
	_In_ PDEVICE_OBJECT DeviceObject,
	_In_ PIRP           Irp
	);

#endif // !VIRTUSB_PNP_H
