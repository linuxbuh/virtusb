#ifndef VIRTUSB_WMI_H
#define VIRTUSB_WMI_H

#include "virtusb.h"

NTSTATUS
VirtUsb_DispatchSystemControl(
	_In_ PDEVICE_OBJECT DeviceObject,
	_In_ PIRP           Irp
	);

NTSTATUS
VirtUsb_WmiRegistration(
	_In_ PFDO_DEVICE_DATA FdoData
	);

NTSTATUS
VirtUsb_WmiDeRegistration (
	_In_ PFDO_DEVICE_DATA FdoData
	);

#endif // !VIRTUSB_WMI_H
