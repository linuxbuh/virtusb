#ifndef VIRTUSB_USER_IO_H
#define VIRTUSB_USER_IO_H

#include "virtusb.h"

NTSTATUS
VirtUsb_DispatchCreateClose(
	IN PDEVICE_OBJECT DeviceObject,
	IN PIRP           Irp
	);

NTSTATUS
VirtUsb_DispatchDeviceControl(
	IN PDEVICE_OBJECT DeviceObject,
	IN PIRP           Irp
	);

VOID
VirtUsb_GotWork(
	IN PPDO_DEVICE_DATA PdoData,
	IN KIRQL            OldIrql
	);

VOID
VirtUsb_PortStatUpdateToUser(
	IN PPDO_DEVICE_DATA PdoData,
	IN UCHAR            Index
	);

BOOLEAN
VirtUsb_IsPortBitArraySet(
	IN CONST ULONG_PTR *BitArr
	);

VOID
VirtUsb_FailFileIO(
	IN OUT PFILE_CONTEXT File,
	IN     NTSTATUS      FailReason
	);

#endif // !VIRTUSB_USER_IO_H
