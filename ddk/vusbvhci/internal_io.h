#ifndef VUSBVHCI_INTERNAL_IO_H
#define VUSBVHCI_INTERNAL_IO_H

#include "vusbvhci.h"

NTSTATUS
VUsbVhci_DispatchInternalDeviceControl(
	IN PDEVICE_OBJECT DeviceObject,
	IN PIRP           Irp
	);

NTSTATUS
VUsbVhci_ProcUrb(
	IN PFDO_DEVICE_DATA FdoData,
	IN PPDO_DEVICE_DATA PdoData,
	IN PIRP             Irp
	);

NTSTATUS
VUsbVhci_NormalizeUrb(
	IN OUT PURB        Urb,
	OUT    USBD_STATUS *UsbStatus
	);

#endif // !VUSBVHCI_INTERNAL_IO_H
