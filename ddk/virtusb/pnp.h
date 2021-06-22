#ifndef VIRTUSB_PNP_H
#define VIRTUSB_PNP_H

#include "virtusb.h"

NTSTATUS
VirtUsb_DispatchPnP(
	IN PDEVICE_OBJECT DeviceObject,
	IN PIRP           Irp
	);

#endif // !VIRTUSB_PNP_H
