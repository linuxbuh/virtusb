#ifndef VUSBVHCI_WMI_H
#define VUSBVHCI_WMI_H

#include "vusbvhci.h"

NTSTATUS
VUsbVhci_DispatchSystemControl(
	_In_ PDEVICE_OBJECT DeviceObject,
	_In_ PIRP           Irp
	);

NTSTATUS
VUsbVhci_WmiRegistration(
	_In_ PFDO_DEVICE_DATA FdoData
	);

NTSTATUS
VUsbVhci_WmiDeRegistration (
	_In_ PFDO_DEVICE_DATA FdoData
	);

#endif // !VUSBVHCI_WMI_H
