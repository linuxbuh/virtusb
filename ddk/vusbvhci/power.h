#ifndef VUSBVHCI_POWER_H
#define VUSBVHCI_POWER_H

#include "vusbvhci.h"

NTSTATUS
VUsbVhci_DispatchPower(
	IN PDEVICE_OBJECT DeviceObject,
	IN PIRP           Irp
	);

#endif // !VUSBVHCI_POWER_H
