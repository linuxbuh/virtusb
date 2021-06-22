#ifndef VUSBVHCI_USER_IO_H
#define VUSBVHCI_USER_IO_H

#include "vusbvhci.h"

NTSTATUS
VUsbVhci_DispatchCreateClose(
	IN PDEVICE_OBJECT DeviceObject,
	IN PIRP           Irp
	);

NTSTATUS
VUsbVhci_DispatchDeviceControl(
	IN PDEVICE_OBJECT DeviceObject,
	IN PIRP           Irp
	);

#endif // !VUSBVHCI_USER_IO_H
