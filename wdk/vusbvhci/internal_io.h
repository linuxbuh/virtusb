#ifndef VUSBVHCI_INTERNAL_IO_H
#define VUSBVHCI_INTERNAL_IO_H

#include "vusbvhci.h"

NTSTATUS
VUsbVhci_DispatchInternalDeviceControl(
	_In_ PDEVICE_OBJECT DeviceObject,
	_In_ PIRP           Irp
	);

NTSTATUS
VUsbVhci_ProcUrb(
	_In_ PFDO_DEVICE_DATA FdoData,
	_In_ PPDO_DEVICE_DATA PdoData,
	_In_ PIRP             Irp
	);

NTSTATUS
VUsbVhci_NormalizeUrb(
	_Inout_ PURB        Urb,
	_Out_   USBD_STATUS *UsbStatus
	);

#endif // !VUSBVHCI_INTERNAL_IO_H
