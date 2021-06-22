#include "virtusb.h"
#include "internal_io.h"
#include "proc_urb.h"

NTSTATUS
VirtUsb_DispatchInternalDeviceControl(
	IN PDEVICE_OBJECT DeviceObject,
	IN PIRP           Irp
	)
/*
    IRQL <= DISPATCH_LEVEL
*/
{
	NTSTATUS         status = STATUS_SUCCESS;
	PPDO_DEVICE_DATA pdoData = DeviceObject->DeviceExtension;

	ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);

	VIRTUSB_KDPRINT("in VirtUsb_DispatchInternalDeviceControl\n");

	if(pdoData->IsFDO)
	{
		Irp->IoStatus.Status = status = STATUS_INVALID_DEVICE_REQUEST;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		return status;
	}

	if(!NT_SUCCESS(IoAcquireRemoveLock(&pdoData->RemoveLock, Irp)))
	{
		Irp->IoStatus.Status = status = STATUS_NO_SUCH_DEVICE;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		return status;
	}

	if(pdoData->DevicePnPState & Deleted)
	{
		Irp->IoStatus.Status = status = STATUS_NO_SUCH_DEVICE;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		IoReleaseRemoveLock(&pdoData->RemoveLock, Irp);
		return status;
	}

	switch(IoGetCurrentIrpStackLocation(Irp)->Parameters.DeviceIoControl.IoControlCode)
	{
	case IOCTL_INTERNAL_USB_SUBMIT_URB:
		if(URB_FROM_IRP(Irp))
		{
			return VirtUsb_ProcUrb(pdoData, Irp);
		}
		break;

	default:
		status = STATUS_INVALID_DEVICE_REQUEST;

		// TODO: did we miss something?
		KdBreakPoint();

		break;
	}

	Irp->IoStatus.Status = status;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	IoReleaseRemoveLock(&pdoData->RemoveLock, Irp);
	return status;
}
