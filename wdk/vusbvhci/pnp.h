#ifndef VUSBVHCI_PNP_H
#define VUSBVHCI_PNP_H

#include "vusbvhci.h"

NTSTATUS
VUsbVhci_DispatchPnP(
	_In_ PDEVICE_OBJECT DeviceObject,
	_In_ PIRP           Irp
	);

#endif // !VUSBVHCI_PNP_H
