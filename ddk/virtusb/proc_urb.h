#ifndef VIRTUSB_PROC_URB_H
#define VIRTUSB_PROC_URB_H

#include "virtusb.h"

#define STDREQ_GET_DESCRIPTOR        (0x8000 | USB_REQUEST_GET_DESCRIPTOR)
#define STDREQ_GET_STATUS            (0x8000 | USB_REQUEST_GET_STATUS)

NTSTATUS
VirtUsb_ProcUrb(
	IN PPDO_DEVICE_DATA PdoData,
	IN PIRP             Irp
	);

NTSTATUS
VirtUsb_SubmitUrb(
	IN     PPDO_DEVICE_DATA PdoData,
	IN OUT PIRP             Irp,
	IN OUT PURB             Urb
	);

VOID
VirtUsb_CancelUrbIrp(
	__inout PDEVICE_OBJECT DeviceObject,
	__in    PIRP           Irp
	);

VOID
VirtUsb_FailUrbIO(
	IN OUT PPDO_DEVICE_DATA PdoData,
	IN     NTSTATUS         FailReason,
	IN     BOOLEAN          FreeIrplessCancelWorkUnits
	);

#endif // !VIRTUSB_PROC_URB_H
