#ifndef VIRTUSB_USER_IO_H
#define VIRTUSB_USER_IO_H

#include "virtusb.h"

NTSTATUS
VirtUsb_DispatchCreateClose(
	_In_ PDEVICE_OBJECT DeviceObject,
	_In_ PIRP           Irp
	);

NTSTATUS
VirtUsb_DispatchDeviceControl(
	_In_ PDEVICE_OBJECT DeviceObject,
	_In_ PIRP           Irp
	);

VOID
VirtUsb_GotWork(
	_In_ PPDO_DEVICE_DATA PdoData,
	_In_ KIRQL            OldIrql
	);

VOID
VirtUsb_PortStatUpdateToUser(
	_In_ PPDO_DEVICE_DATA PdoData,
	_In_ UCHAR            Index
	);

BOOLEAN
VirtUsb_IsPortBitArraySet(
	_In_ CONST ULONG_PTR *BitArr
	);

VOID
VirtUsb_FailFileIO(
	_Inout_ PFILE_CONTEXT File,
	_In_    NTSTATUS      FailReason
	);

#endif // !VIRTUSB_USER_IO_H
