#ifndef VIRTUSB_PROC_URB_H
#define VIRTUSB_PROC_URB_H

#include "virtusb.h"

#define STDREQ_GET_DESCRIPTOR        (0x8000 | USB_REQUEST_GET_DESCRIPTOR)
#define STDREQ_GET_STATUS            (0x8000 | USB_REQUEST_GET_STATUS)

NTSTATUS
VirtUsb_ProcUrb(
	_In_ PPDO_DEVICE_DATA PdoData,
	_In_ PIRP             Irp
	);

NTSTATUS
VirtUsb_SubmitUrb(
	_In_    PPDO_DEVICE_DATA PdoData,
	_Inout_ PIRP             Irp,
	_Inout_ PURB             Urb
	);

VOID
VirtUsb_CancelUrbIrp(
	_Inout_ PDEVICE_OBJECT DeviceObject,
	_In_    PIRP           Irp
	);

VOID
VirtUsb_FailUrbIO(
	_Inout_ PPDO_DEVICE_DATA PdoData,
	_In_    NTSTATUS         FailReason,
	_In_    BOOLEAN          FreeIrplessCancelWorkUnits
	);

#endif // !VIRTUSB_PROC_URB_H
