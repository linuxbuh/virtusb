#include "vusbvhci.h"
#include "user_io.h"

NTSTATUS
VUsbVhci_ProcUserCtl(
	IN PFDO_DEVICE_DATA FdoData,
	IN PIRP             Irp
	);

#if DBG
PCHAR
DbgUserDeviceControlString(
	IN ULONG CtlCode
	);
#endif // DBG

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, VUsbVhci_DispatchCreateClose)
#pragma alloc_text(PAGE, VUsbVhci_DispatchDeviceControl)
#pragma alloc_text(PAGE, VUsbVhci_ProcUserCtl)
#endif // ALLOC_PRAGMA

NTSTATUS
VUsbVhci_DispatchCreateClose(
	IN PDEVICE_OBJECT DeviceObject,
	IN PIRP           Irp
	)
{
	PIO_STACK_LOCATION  irpStack;
	NTSTATUS            status = STATUS_SUCCESS;
	PFDO_DEVICE_DATA    fdoData;

	PAGED_CODE();

	VUSBVHCI_KDPRINT("in VUsbVhci_DispatchCreateClose\n");

	// We allow create/close requests for the FDO only.
	// That is the host-controller itself.
	fdoData = (PFDO_DEVICE_DATA)DeviceObject->DeviceExtension;
	if(!fdoData->IsFDO)
	{
		Irp->IoStatus.Information = 0;
		Irp->IoStatus.Status = status = STATUS_INVALID_DEVICE_REQUEST;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		return status;
	}

	if(!NT_SUCCESS(IoAcquireRemoveLock(&fdoData->RemoveLock, Irp)))
	{
		Irp->IoStatus.Information = 0;
		Irp->IoStatus.Status = status = STATUS_NO_SUCH_DEVICE;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		return status;
	}

	irpStack = IoGetCurrentIrpStackLocation(Irp);

	switch(irpStack->MajorFunction)
	{
	case IRP_MJ_CREATE:
		VUSBVHCI_KDPRINT("Create\n");
		if(fdoData->DevicePnPState & Deleted)
		{
			Irp->IoStatus.Information = 0;
			Irp->IoStatus.Status = status = STATUS_NO_SUCH_DEVICE;
			IoCompleteRequest(Irp, IO_NO_INCREMENT);
			IoReleaseRemoveLock(&fdoData->RemoveLock, Irp);
			return status;
		}
		break;

	case IRP_MJ_CLOSE:
		VUSBVHCI_KDPRINT("Close\n");
		break;

	default:
		status = STATUS_INVALID_DEVICE_REQUEST;
		break;
	}

	Irp->IoStatus.Information = 0;
	Irp->IoStatus.Status = status;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	IoReleaseRemoveLock(&fdoData->RemoveLock, Irp);
	return status;
}

NTSTATUS
VUsbVhci_DispatchDeviceControl(
	IN PDEVICE_OBJECT DeviceObject,
	IN PIRP           Irp
	)
{
	NTSTATUS           status = STATUS_SUCCESS;
	PFDO_DEVICE_DATA   fdoData = (PFDO_DEVICE_DATA)DeviceObject->DeviceExtension;

	PAGED_CODE();

	VUSBVHCI_KDPRINT("in VUsbVhci_DispatchDeviceControl\n");

	if(!fdoData->IsFDO)
	{
		Irp->IoStatus.Status = status = STATUS_INVALID_DEVICE_REQUEST;
		Irp->IoStatus.Information = 0;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		return status;
	}

	if(!NT_SUCCESS(IoAcquireRemoveLock(&fdoData->RemoveLock, Irp)))
	{
		Irp->IoStatus.Status = status = STATUS_NO_SUCH_DEVICE;
		Irp->IoStatus.Information = 0;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		return status;
	}

	if(fdoData->DevicePnPState & Deleted)
	{
		Irp->IoStatus.Status = status = STATUS_NO_SUCH_DEVICE;
		Irp->IoStatus.Information = 0;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		IoReleaseRemoveLock(&fdoData->RemoveLock, Irp);
		return status;
	}

	status = VUsbVhci_ProcUserCtl(fdoData, Irp);
	return status;
}

#if DBG
PCHAR
DbgUserDeviceControlString(
	IN ULONG CtlCode
	)
{
	switch(CtlCode)
	{
	case IOCTL_USB_HCD_GET_STATS_1:
		return "IOCTL_USB_HCD_GET_STATS_1";
	case IOCTL_USB_HCD_GET_STATS_2:
		return "IOCTL_USB_HCD_GET_STATS_2";
	case IOCTL_USB_HCD_DISABLE_PORT:
		return "IOCTL_USB_HCD_DISABLE_PORT";
	case IOCTL_USB_HCD_ENABLE_PORT:
		return "IOCTL_USB_HCD_ENABLE_PORT";
	case IOCTL_USB_DIAGNOSTIC_MODE_ON:
		return "IOCTL_USB_DIAGNOSTIC_MODE_ON";
	case IOCTL_USB_DIAGNOSTIC_MODE_OFF:
		return "IOCTL_USB_DIAGNOSTIC_MODE_OFF";
	case IOCTL_USB_GET_ROOT_HUB_NAME:
		return "IOCTL_USB_GET_ROOT_HUB_NAME";
	case IOCTL_GET_HCD_DRIVERKEY_NAME:
		return "IOCTL_GET_HCD_DRIVERKEY_NAME";
	case IOCTL_USB_GET_NODE_CONNECTION_INFORMATION:
		return "IOCTL_USB_GET_NODE_CONNECTION_INFORMATION";
	default:
		return "unknown user ioctl";
	}
}
#endif // DBG

NTSTATUS
VUsbVhci_ProcUserCtl(
	IN PFDO_DEVICE_DATA FdoData,
	IN PIRP             Irp
	)
{
	NTSTATUS           status = STATUS_SUCCESS;
	ULONG_PTR          inf = 0;
	ULONG              len, buflen, i;
	PPDO_DEVICE_DATA   pdoData;
	PIO_STACK_LOCATION irpStack;

	PAGED_CODE();

	VUSBVHCI_KDPRINT("in VUsbVhci_ProcUserCtl\n");

	ASSERT(FdoData);
	ASSERT(Irp);
	irpStack = IoGetCurrentIrpStackLocation(Irp);
	ASSERT(irpStack);

	VUSBVHCI_KDPRINT1("CtlCode: %s\n", DbgUserDeviceControlString(irpStack->Parameters.DeviceIoControl.IoControlCode));

	switch(irpStack->Parameters.DeviceIoControl.IoControlCode)
	{
	case IOCTL_USB_HCD_GET_STATS_1:
	case IOCTL_USB_HCD_GET_STATS_2:
	case IOCTL_USB_HCD_DISABLE_PORT:
	case IOCTL_USB_HCD_ENABLE_PORT:
	case IOCTL_USB_DIAGNOSTIC_MODE_ON:
	case IOCTL_USB_DIAGNOSTIC_MODE_OFF:
	case IOCTL_USB_GET_NODE_CONNECTION_INFORMATION:
		status = STATUS_NOT_IMPLEMENTED;
		// TODO: implement
		KdBreakPoint();
		break;

	case IOCTL_USB_GET_ROOT_HUB_NAME:
		if(!FdoData->RHubPdo)
		{
			status = STATUS_UNSUCCESSFUL;
			break;
		}
		pdoData = PDO_FROM_FDO(FdoData);

		status = VUsbVhci_GetAndStoreRootHubName(pdoData);
		if(!NT_SUCCESS(status))
		{
			break;
		}
		ASSERT(pdoData->InterfaceName.Buffer);
		ASSERT(pdoData->InterfaceName.Length / 2);

		if(irpStack->Parameters.DeviceIoControl.OutputBufferLength <
		   sizeof(USB_HCD_DRIVERKEY_NAME))
		{
			status = STATUS_BUFFER_TOO_SMALL;
			break;
		}

		// we want the basename -- search for the start index
		for(i = pdoData->InterfaceName.Length / 2; i; i--)
		{
			if(pdoData->InterfaceName.Buffer[i - 1] == L'\\')
			{
				break;
			}
		}

		buflen = pdoData->InterfaceName.Length / 2 - i + 1;
		len = FIELD_OFFSET(USB_HCD_DRIVERKEY_NAME, DriverKeyName[0]) +
		      buflen * sizeof(WCHAR);
		((PUSB_HCD_DRIVERKEY_NAME)Irp->AssociatedIrp.SystemBuffer)->ActualLength = len;
		if(len > irpStack->Parameters.DeviceIoControl.OutputBufferLength)
		{
			len = irpStack->Parameters.DeviceIoControl.OutputBufferLength;
			buflen = (len - FIELD_OFFSET(USB_HCD_DRIVERKEY_NAME, DriverKeyName[0])) / sizeof(WCHAR);
			ASSERT(buflen); // there is always space for at least one wide-char!
		}

		RtlCopyMemory(((PUSB_HCD_DRIVERKEY_NAME)Irp->AssociatedIrp.SystemBuffer)->DriverKeyName,
		              pdoData->InterfaceName.Buffer + i,
		              buflen * sizeof(WCHAR));
		// make sure the last character is 0
		((PWCHAR)((PUSB_HCD_DRIVERKEY_NAME)Irp->AssociatedIrp.SystemBuffer)->DriverKeyName)[buflen - 1] = 0;
		inf = len;
		VUSBVHCI_KDPRINT1("RootHubSymlink: %ws\n", ((PUSB_HCD_DRIVERKEY_NAME)Irp->AssociatedIrp.SystemBuffer)->DriverKeyName);
		break;

	case IOCTL_GET_HCD_DRIVERKEY_NAME:
		buflen = irpStack->Parameters.DeviceIoControl.OutputBufferLength;
		if(buflen < sizeof(USB_HCD_DRIVERKEY_NAME))
		{
			status = STATUS_BUFFER_TOO_SMALL;
			break;
		}

		status = IoGetDeviceProperty(FdoData->UnderlyingPDO,
		                             DevicePropertyDriverKeyName,
		                             buflen - FIELD_OFFSET(USB_HCD_DRIVERKEY_NAME, DriverKeyName[0]),
		                             ((PUSB_HCD_DRIVERKEY_NAME)Irp->AssociatedIrp.SystemBuffer)->DriverKeyName,
		                             &len);
		if(!NT_SUCCESS(status) &&
		   (status != STATUS_BUFFER_TOO_SMALL))
		{
			VUSBVHCI_KDPRINT1("IoGetDeviceProperty failed (0x%08lx)\n", status);
			break;
		}
		status = STATUS_SUCCESS;

		len += FIELD_OFFSET(USB_HCD_DRIVERKEY_NAME, DriverKeyName[0]);
		((PUSB_HCD_DRIVERKEY_NAME)Irp->AssociatedIrp.SystemBuffer)->ActualLength = len;
		if(len > buflen)
		{
			inf = buflen;
			buflen = (len - FIELD_OFFSET(USB_HCD_DRIVERKEY_NAME, DriverKeyName[0])) / sizeof(WCHAR);
			ASSERT(buflen); // there is always space for at least one wide-char!
			// make sure the last character is 0
			((PWCHAR)((PUSB_HCD_DRIVERKEY_NAME)Irp->AssociatedIrp.SystemBuffer)->DriverKeyName)[buflen - 1] = 0;
		}
		else
		{
			inf = len;
		}
		VUSBVHCI_KDPRINT1("DevicePropertyDriverKeyName: %ws\n", ((PUSB_HCD_DRIVERKEY_NAME)Irp->AssociatedIrp.SystemBuffer)->DriverKeyName);
		break;

	default:
		VUSBVHCI_KDPRINT1("invalid IOCTL: 0x%08lx\n", irpStack->Parameters.DeviceIoControl.IoControlCode);
		status = STATUS_INVALID_DEVICE_REQUEST;
		break;
	}

	Irp->IoStatus.Information = inf;
	Irp->IoStatus.Status = status;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	IoReleaseRemoveLock(&FdoData->RemoveLock, Irp);
	return status;
}
