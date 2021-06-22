#ifndef VUSBVHCI_WMI_H
#define VUSBVHCI_WMI_H

#include "vusbvhci.h"

NTSTATUS
VUsbVhci_DispatchSystemControl(
	IN PDEVICE_OBJECT DeviceObject,
	IN PIRP           Irp
	);

NTSTATUS
VUsbVhci_WmiRegistration(
	IN PFDO_DEVICE_DATA FdoData
	);

NTSTATUS
VUsbVhci_WmiDeRegistration (
	IN PFDO_DEVICE_DATA FdoData
	);

#endif // !VUSBVHCI_WMI_H
