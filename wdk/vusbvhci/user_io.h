#ifndef VUSBVHCI_USER_IO_H
#define VUSBVHCI_USER_IO_H

#include "vusbvhci.h"

NTSTATUS
VUsbVhci_DispatchCreateClose(
	_In_ PDEVICE_OBJECT DeviceObject,
	_In_ PIRP           Irp
	);

NTSTATUS
VUsbVhci_DispatchDeviceControl(
	_In_ PDEVICE_OBJECT DeviceObject,
	_In_ PIRP           Irp
	);

#endif // !VUSBVHCI_USER_IO_H
