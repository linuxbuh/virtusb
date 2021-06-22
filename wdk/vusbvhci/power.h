#ifndef VUSBVHCI_POWER_H
#define VUSBVHCI_POWER_H

#include "vusbvhci.h"

NTSTATUS
VUsbVhci_DispatchPower(
	_In_ PDEVICE_OBJECT DeviceObject,
	_In_ PIRP           Irp
	);

#endif // !VUSBVHCI_POWER_H
