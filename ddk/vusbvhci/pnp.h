#ifndef VUSBVHCI_PNP_H
#define VUSBVHCI_PNP_H

#include "vusbvhci.h"

NTSTATUS
VUsbVhci_DispatchPnP(
	IN PDEVICE_OBJECT DeviceObject,
	IN PIRP           Irp
	);

#endif // !VUSBVHCI_PNP_H
