#ifndef VIRTUSB_WMI_H
#define VIRTUSB_WMI_H

#include "virtusb.h"

NTSTATUS
VirtUsb_DispatchSystemControl(
	IN PDEVICE_OBJECT DeviceObject,
	IN PIRP           Irp
	);

NTSTATUS
VirtUsb_WmiRegistration(
	IN PFDO_DEVICE_DATA FdoData
	);

NTSTATUS
VirtUsb_WmiDeRegistration (
	IN PFDO_DEVICE_DATA FdoData
	);

#endif // !VIRTUSB_WMI_H
