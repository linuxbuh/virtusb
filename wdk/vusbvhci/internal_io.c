#include "vusbvhci.h"
#include "trace.h"
#include "internal_io.tmh"
#include "internal_io.h"

PCHAR
DbgInternalDeviceControlString(
	_In_ ULONG CtlCode
	);

NTSTATUS
VUsbVhci_DispatchInternalDeviceControl(
	_In_ PDEVICE_OBJECT DeviceObject,
	_In_ PIRP           Irp
	)
/*
    IRQL <= DISPATCH_LEVEL
*/
{
	PIO_STACK_LOCATION  irpStack;
	NTSTATUS            status = STATUS_SUCCESS;
	PCOMMON_DEVICE_DATA data = (PCOMMON_DEVICE_DATA)DeviceObject->DeviceExtension;
	PPDO_DEVICE_DATA    pdoData;
	PFDO_DEVICE_DATA    fdoData;
	PDEVICE_OBJECT      fdoTop;

	ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_INTERNAL_IO, ">%!FUNC!");

	// The hub driver sends us "SubmitUrb"-IRPs to both, FDO and PDO!
	if(data->IsFDO)
	{
		fdoData = (PFDO_DEVICE_DATA)data;

		if(!NT_SUCCESS(IoAcquireRemoveLock(&fdoData->RemoveLock, Irp)))
		{
			Irp->IoStatus.Status = status = STATUS_NO_SUCH_DEVICE;
			IoCompleteRequest(Irp, IO_NO_INCREMENT);
			return status;
		}

		pdoData = PDO_FROM_FDO(fdoData);
		ASSERT(pdoData);

		if(!NT_SUCCESS(IoAcquireRemoveLock(&pdoData->RemoveLock, Irp)))
		{
			Irp->IoStatus.Status = status = STATUS_NO_SUCH_DEVICE;
			IoCompleteRequest(Irp, IO_NO_INCREMENT);
			IoReleaseRemoveLock(&fdoData->RemoveLock, Irp);
			return status;
		}

		ASSERT(DeviceObject == pdoData->ParentFdo);
	}
	else
	{
		pdoData = (PPDO_DEVICE_DATA)data;

		if(!NT_SUCCESS(IoAcquireRemoveLock(&pdoData->RemoveLock, Irp)))
		{
			Irp->IoStatus.Status = status = STATUS_NO_SUCH_DEVICE;
			IoCompleteRequest(Irp, IO_NO_INCREMENT);
			return status;
		}

		fdoData = FDO_FROM_PDO(pdoData);
		ASSERT(fdoData);

		if(!NT_SUCCESS(IoAcquireRemoveLock(&fdoData->RemoveLock, Irp)))
		{
			Irp->IoStatus.Status = status = STATUS_NO_SUCH_DEVICE;
			IoCompleteRequest(Irp, IO_NO_INCREMENT);
			IoReleaseRemoveLock(&pdoData->RemoveLock, Irp);
			return status;
		}

		ASSERT(DeviceObject == fdoData->RHubPdo);
	}

	if((fdoData->DevicePnPState & Deleted) || (pdoData->DevicePnPState & Deleted))
	{
		status = STATUS_NO_SUCH_DEVICE;
		goto End;
	}

	irpStack = IoGetCurrentIrpStackLocation(Irp);

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_INTERNAL_IO, "CtlCode: %s", DbgInternalDeviceControlString(irpStack->Parameters.DeviceIoControl.IoControlCode));

	switch(irpStack->Parameters.DeviceIoControl.IoControlCode)
	{
	case IOCTL_INTERNAL_USB_SUBMIT_URB:
		if(irpStack->Parameters.Others.Argument1)
		{
			status = VUsbVhci_ProcUrb(fdoData, pdoData, Irp);
			return status;
		}
		break;

	case IOCTL_INTERNAL_USB_RESET_PORT:
		status = Irp->IoStatus.Status;
		// TODO: implement
		KdBreakPoint();
		break;

	case IOCTL_INTERNAL_USB_GET_ROOTHUB_PDO:
		if(irpStack->Parameters.Others.Argument1)
		{
			*(PDEVICE_OBJECT *)irpStack->Parameters.Others.Argument1 = DeviceObject;
		}
		if(irpStack->Parameters.Others.Argument2)
		{
			fdoTop = IoGetAttachedDeviceReference(fdoData->Self);
			ObDereferenceObject(fdoTop); // crappy shit
			*(PDEVICE_OBJECT *)irpStack->Parameters.Others.Argument2 = fdoTop;
		}
		break;

	case IOCTL_INTERNAL_USB_GET_PORT_STATUS:
		status = Irp->IoStatus.Status;
		// TODO: implement!                          *****************************************************
		//       gets called when IOCTL_INTERNAL_USB_SUBMIT_URB with a
		//       GET_PORT_STATUS control urb fails a few times
		//KdBreakPoint();
		break;

	case IOCTL_INTERNAL_USB_ENABLE_PORT: /* obsolete: use IOCTL_INTERNAL_USB_RESET_PORT */
		status = Irp->IoStatus.Status;
		// TODO: implement
		KdBreakPoint();
		break;

	case IOCTL_INTERNAL_USB_GET_HUB_COUNT:
		// This one is incremented by every hub on the
		// way down to us. We have to increment it, too.
		if(irpStack->Parameters.Others.Argument1)
		{
			(*(ULONG *)irpStack->Parameters.Others.Argument1)++;
			TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_INTERNAL_IO, "HubCount: %lu", *(ULONG *)irpStack->Parameters.Others.Argument1);
		}
		break;

	case IOCTL_INTERNAL_USB_CYCLE_PORT:
		status = Irp->IoStatus.Status;
		// TODO: implement
		KdBreakPoint();
		break;

	case IOCTL_INTERNAL_USB_GET_HUB_NAME:
		status = VUsbVhci_GetAndStoreRootHubName(pdoData);
		if(!NT_SUCCESS(status))
		{
			break;
		}
		ASSERT(pdoData->InterfaceName.Buffer);
		ASSERT(pdoData->InterfaceName.Length);

		// TODO: copy buffer
		KdBreakPoint();
		status = Irp->IoStatus.Status;
		break;

	case IOCTL_INTERNAL_USB_GET_BUS_INFO: /* obsolete */
	case IOCTL_INTERNAL_USB_GET_CONTROLLER_NAME:
	case IOCTL_INTERNAL_USB_GET_BUSGUID_INFO:
	case IOCTL_INTERNAL_USB_GET_PARENT_HUB_INFO:
		status = Irp->IoStatus.Status;
		// TODO: implement
		KdBreakPoint();
		break;

#ifndef TARGETING_Win2K
	case IOCTL_INTERNAL_USB_SUBMIT_IDLE_NOTIFICATION:
		// TODO: Will we suspend the root-hub sometime?
		//       If yes, then we have to store the callback
		//       function pointer somewhere.
		status = STATUS_SUCCESS;
		break;

	case IOCTL_INTERNAL_USB_GET_DEVICE_HANDLE:
		if(irpStack->Parameters.Others.Argument1)
		{
			if(irpStack->Parameters.Others.Argument2)
			{
				// TODO: is this shit true?
/*
    (INPUT)
    Parameters.Others.Argument1 =
        pointer to device handle
    (OUTPUT)
    Parameters.Others.Argument2 =
        pointer to device address
*/
				TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_INTERNAL_IO, "someone used two arguments on IOCTL_INTERNAL_USB_GET_DEVICE_HANDLE");
				// TODO: implement
				KdBreakPoint();
				status = STATUS_NOT_IMPLEMENTED;
			}
			else
			{
				*(PUSB_DEVICE_HANDLE *)irpStack->Parameters.Others.Argument1 =
					(PUSB_DEVICE_HANDLE)VUsbVhci_GetRootHubContext(fdoData);
				TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_INTERNAL_IO, "RootHub device handle: 0x%p", *(PUSB_DEVICE_HANDLE *)irpStack->Parameters.Others.Argument1);
			}
		}
		break;
#endif

	default:
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_INTERNAL_IO, "invalid internal IOCTL: 0x%08lx", irpStack->Parameters.DeviceIoControl.IoControlCode);
		status = STATUS_INVALID_DEVICE_REQUEST;
		// TODO: did we miss something?
		KdBreakPoint();
		break;
	}

End:
	Irp->IoStatus.Status = status;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	IoReleaseRemoveLock(&pdoData->RemoveLock, Irp);
	IoReleaseRemoveLock(&fdoData->RemoveLock, Irp);
	return status;
}

PCHAR
DbgInternalDeviceControlString(
	_In_ ULONG CtlCode
	)
{
	switch(CtlCode)
	{
	case IOCTL_INTERNAL_USB_SUBMIT_URB:
		return "IOCTL_INTERNAL_USB_SUBMIT_URB";
	case IOCTL_INTERNAL_USB_RESET_PORT:
		return "IOCTL_INTERNAL_USB_RESET_PORT";
	case IOCTL_INTERNAL_USB_GET_ROOTHUB_PDO:
		return "IOCTL_INTERNAL_USB_GET_ROOTHUB_PDO";
	case IOCTL_INTERNAL_USB_GET_PORT_STATUS:
		return "IOCTL_INTERNAL_USB_GET_PORT_STATUS";
	case IOCTL_INTERNAL_USB_ENABLE_PORT: /* obsolete: use IOCTL_INTERNAL_USB_RESET_PORT */
		return "IOCTL_INTERNAL_USB_ENABLE_PORT (***obsolete***)";
	case IOCTL_INTERNAL_USB_GET_HUB_COUNT:
		return "IOCTL_INTERNAL_USB_GET_HUB_COUNT";
	case IOCTL_INTERNAL_USB_CYCLE_PORT:
		return "IOCTL_INTERNAL_USB_CYCLE_PORT";
	case IOCTL_INTERNAL_USB_GET_HUB_NAME:
		return "IOCTL_INTERNAL_USB_GET_HUB_NAME";
	case IOCTL_INTERNAL_USB_GET_BUS_INFO: /* obsolete */
		return "IOCTL_INTERNAL_USB_GET_BUS_INFO (***obsolete***)";
	case IOCTL_INTERNAL_USB_GET_CONTROLLER_NAME:
		return "IOCTL_INTERNAL_USB_GET_CONTROLLER_NAME";
	case IOCTL_INTERNAL_USB_GET_BUSGUID_INFO:
		return "IOCTL_INTERNAL_USB_GET_BUSGUID_INFO";
	case IOCTL_INTERNAL_USB_GET_PARENT_HUB_INFO:
		return "IOCTL_INTERNAL_USB_GET_PARENT_HUB_INFO";
#ifndef TARGETING_Win2K
	case IOCTL_INTERNAL_USB_SUBMIT_IDLE_NOTIFICATION:
		return "IOCTL_INTERNAL_USB_SUBMIT_IDLE_NOTIFICATION";
	case IOCTL_INTERNAL_USB_GET_DEVICE_HANDLE:
		return "IOCTL_INTERNAL_USB_GET_DEVICE_HANDLE";
#endif
	default:
		return "unknown internal ioctl";
	}
}

NTSTATUS
VUsbVhci_ProcUrb(
	_In_ PFDO_DEVICE_DATA FdoData,
	_In_ PPDO_DEVICE_DATA PdoData,
	_In_ PIRP             Irp
	)
/*
    IRQL <= DISPATCH_LEVEL
*/
{
	NTSTATUS        ntStatus = STATUS_SUCCESS;
	USBD_STATUS     usbStatus = USBD_STATUS_SUCCESS;
	PUSBHUB_CONTEXT rhub;
	PURB            urb;

	ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_INTERNAL_IO, ">%!FUNC!");

	ASSERT(FdoData);
	ASSERT(Irp);
	urb = URB_FROM_IRP(Irp);
	ASSERT(urb);

	if(urb->UrbHeader.Length < sizeof(struct _URB_HEADER))
	{
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_INTERNAL_IO, "'very' invalid URB");
		ntStatus = STATUS_INVALID_PARAMETER;
		Irp->IoStatus.Status = ntStatus;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		IoReleaseRemoveLock(&PdoData->RemoveLock, Irp);
		IoReleaseRemoveLock(&FdoData->RemoveLock, Irp);
		return ntStatus;
	}

	rhub = VUsbVhci_GetRootHubContext(FdoData);
	if(!rhub)
	{
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_INTERNAL_IO, "parent interface down");
		usbStatus = USBD_STATUS_DEV_NOT_RESPONDING;
		ntStatus = STATUS_NO_SUCH_DEVICE;
		goto Fail;
	}

	// check if it is for the root-hub
	if(!urb->UrbHeader.UsbdDeviceHandle ||
	   (urb->UrbHeader.UsbdDeviceHandle == rhub))
	{
		TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_INTERNAL_IO, "root-hub got an URB");
		// in case it is zero
		urb->UrbHeader.UsbdDeviceHandle = rhub;
	}

	ntStatus = VUsbVhci_NormalizeUrb(urb, &usbStatus);
	if(!NT_SUCCESS(ntStatus))
	{
		goto Fail;
	}

	// There are no stack locations left so we can't send it to FdoData->NextLowerDriver
	IoSkipCurrentIrpStackLocation(Irp);
	ntStatus = IoCallDriver(FdoData->UnderlyingPDO, Irp);

	IoReleaseRemoveLock(&PdoData->RemoveLock, Irp);
	IoReleaseRemoveLock(&FdoData->RemoveLock, Irp);
	return ntStatus;

Fail:
	urb->UrbHeader.Status = usbStatus;
	Irp->IoStatus.Status = ntStatus;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	IoReleaseRemoveLock(&PdoData->RemoveLock, Irp);
	IoReleaseRemoveLock(&FdoData->RemoveLock, Irp);
	return ntStatus;
}

NTSTATUS
VUsbVhci_NormalizeUrb(
	_Inout_ PURB        Urb,
	_Out_   USBD_STATUS *UsbStatus
	)
/*
    IRQL <= DISPATCH_LEVEL
*/
{
	PUSBDEV_CONTEXT                dev;
	PPIPE_CONTEXT                  ep0;
	PUSB_DEFAULT_PIPE_SETUP_PACKET sp;

	ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_INTERNAL_IO, ">%!FUNC!");

	ASSERT(Urb);
	ASSERT(UsbStatus);
	dev = (PUSBDEV_CONTEXT)Urb->UrbHeader.UsbdDeviceHandle;
	ASSERT(dev);
	ep0 = &dev->EndpointZero;

	switch(Urb->UrbHeader.Function)
	{
	case URB_FUNCTION_CONTROL_TRANSFER:
		if(Urb->UrbHeader.Length < sizeof(struct _URB_CONTROL_TRANSFER))
		{
			*UsbStatus = USBD_STATUS_INVALID_PARAMETER;
			return STATUS_INVALID_PARAMETER;
		}
		if(!Urb->UrbControlTransfer.PipeHandle)
		{
			Urb->UrbControlTransfer.PipeHandle = ep0;
		}
		if(Urb->UrbControlTransfer.PipeHandle == ep0)
		{
			Urb->UrbControlTransfer.TransferFlags |= USBD_DEFAULT_PIPE_TRANSFER;
		}
		else if(Urb->UrbControlTransfer.TransferFlags & USBD_DEFAULT_PIPE_TRANSFER)
		{
			*UsbStatus = USBD_STATUS_INVALID_PARAMETER;
			return STATUS_INVALID_PARAMETER;
		}
		sp = (PUSB_DEFAULT_PIPE_SETUP_PACKET)&Urb->UrbControlTransfer.SetupPacket;
		if(sp->bmRequestType.Dir == BMREQUEST_DEVICE_TO_HOST)
		{
			Urb->UrbControlTransfer.TransferFlags |= USBD_TRANSFER_DIRECTION_IN;
		}
		else if(Urb->UrbControlTransfer.TransferFlags & USBD_TRANSFER_DIRECTION_IN)
		{
			*UsbStatus = USBD_STATUS_INVALID_PARAMETER;
			return STATUS_INVALID_PARAMETER;
		}
		if(Urb->UrbControlTransfer.TransferBufferLength < sp->wLength)
		{
			*UsbStatus = USBD_STATUS_INVALID_PARAMETER;
			return STATUS_INVALID_PARAMETER;
		}
		Urb->UrbControlTransfer.TransferBufferLength = sp->wLength;
		goto CheckBuffer;

	case URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER:
		if(Urb->UrbHeader.Length < sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER))
		{
			*UsbStatus = USBD_STATUS_INVALID_PARAMETER;
			return STATUS_INVALID_PARAMETER;
		}
		if(!Urb->UrbBulkOrInterruptTransfer.PipeHandle ||
		   (Urb->UrbBulkOrInterruptTransfer.TransferFlags & USBD_DEFAULT_PIPE_TRANSFER))
		{
			*UsbStatus = USBD_STATUS_INVALID_PARAMETER;
			return STATUS_INVALID_PARAMETER;
		}
		// USBD_TRANSFER_DIRECTION_IN flag gets set in VirtUsb_ProcUrb if necessary.
		// We don't want to bother with the pipe context here.
		goto CheckBuffer;

	case URB_FUNCTION_ISOCH_TRANSFER:
		if(Urb->UrbHeader.Length < sizeof(struct _URB_ISOCH_TRANSFER))
		{
			*UsbStatus = USBD_STATUS_INVALID_PARAMETER;
			return STATUS_INVALID_PARAMETER;
		}
		if(!Urb->UrbIsochronousTransfer.PipeHandle ||
		   (Urb->UrbIsochronousTransfer.TransferFlags & USBD_DEFAULT_PIPE_TRANSFER))
		{
			*UsbStatus = USBD_STATUS_INVALID_PARAMETER;
			return STATUS_INVALID_PARAMETER;
		}
		// USBD_TRANSFER_DIRECTION_IN flag gets set in VirtUsb_ProcUrb if necessary.
		// We don't want to bother with the pipe context here.
		goto CheckBuffer;

	case URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE:
	case URB_FUNCTION_GET_DESCRIPTOR_FROM_INTERFACE:
	case URB_FUNCTION_GET_DESCRIPTOR_FROM_ENDPOINT:
		if(Urb->UrbHeader.Length < sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST))
		{
			*UsbStatus = USBD_STATUS_INVALID_PARAMETER;
			return STATUS_INVALID_PARAMETER;
		}
		Urb->UrbControlTransfer.PipeHandle = ep0;
		Urb->UrbControlTransfer.TransferFlags =
			USBD_TRANSFER_DIRECTION_IN |
			USBD_SHORT_TRANSFER_OK |
			USBD_DEFAULT_PIPE_TRANSFER;
		sp = (PUSB_DEFAULT_PIPE_SETUP_PACKET)&Urb->UrbControlTransfer.SetupPacket;
		sp->bmRequestType.Dir = BMREQUEST_DEVICE_TO_HOST;
		sp->bmRequestType.Type = BMREQUEST_STANDARD;
		if(Urb->UrbHeader.Function == URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE)
		{
			sp->bmRequestType.Recipient = BMREQUEST_TO_DEVICE;
		}
		else if(Urb->UrbHeader.Function == URB_FUNCTION_GET_DESCRIPTOR_FROM_INTERFACE)
		{
			sp->bmRequestType.Recipient = BMREQUEST_TO_INTERFACE;
		}
		else // if(Urb->UrbHeader.Function == URB_FUNCTION_GET_DESCRIPTOR_FROM_ENDPOINT)
		{
			sp->bmRequestType.Recipient = BMREQUEST_TO_ENDPOINT;
		}
		Urb->UrbHeader.Function = URB_FUNCTION_CONTROL_TRANSFER;
		sp->bmRequestType.Reserved = 0;
		sp->bRequest = USB_REQUEST_GET_DESCRIPTOR;
		sp->wLength = (USHORT)Urb->UrbControlTransfer.TransferBufferLength;
		goto CheckBuffer;

	case URB_FUNCTION_SET_DESCRIPTOR_TO_DEVICE:
	case URB_FUNCTION_SET_DESCRIPTOR_TO_INTERFACE:
	case URB_FUNCTION_SET_DESCRIPTOR_TO_ENDPOINT:
		if(Urb->UrbHeader.Length < sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST))
		{
			*UsbStatus = USBD_STATUS_INVALID_PARAMETER;
			return STATUS_INVALID_PARAMETER;
		}
		Urb->UrbControlTransfer.PipeHandle = ep0;
		Urb->UrbControlTransfer.TransferFlags =
			USBD_TRANSFER_DIRECTION_OUT |
			USBD_SHORT_TRANSFER_OK |
			USBD_DEFAULT_PIPE_TRANSFER;
		sp = (PUSB_DEFAULT_PIPE_SETUP_PACKET)&Urb->UrbControlTransfer.SetupPacket;
		sp->bmRequestType.Dir = BMREQUEST_HOST_TO_DEVICE;
		sp->bmRequestType.Type = BMREQUEST_STANDARD;
		if(Urb->UrbHeader.Function == URB_FUNCTION_SET_DESCRIPTOR_TO_DEVICE)
		{
			sp->bmRequestType.Recipient = BMREQUEST_TO_DEVICE;
		}
		else if(Urb->UrbHeader.Function == URB_FUNCTION_SET_DESCRIPTOR_TO_INTERFACE)
		{
			sp->bmRequestType.Recipient = BMREQUEST_TO_INTERFACE;
		}
		else // if(Urb->UrbHeader.Function == URB_FUNCTION_SET_DESCRIPTOR_TO_ENDPOINT)
		{
			sp->bmRequestType.Recipient = BMREQUEST_TO_ENDPOINT;
		}
		Urb->UrbHeader.Function = URB_FUNCTION_CONTROL_TRANSFER;
		sp->bmRequestType.Reserved = 0;
		sp->bRequest = USB_REQUEST_SET_DESCRIPTOR;
		sp->wLength = (USHORT)Urb->UrbControlTransfer.TransferBufferLength;
		goto CheckBuffer;

	case URB_FUNCTION_SET_FEATURE_TO_DEVICE:
	case URB_FUNCTION_SET_FEATURE_TO_INTERFACE:
	case URB_FUNCTION_SET_FEATURE_TO_ENDPOINT:
	case URB_FUNCTION_SET_FEATURE_TO_OTHER:
		if(Urb->UrbHeader.Length < sizeof(struct _URB_CONTROL_FEATURE_REQUEST))
		{
			*UsbStatus = USBD_STATUS_INVALID_PARAMETER;
			return STATUS_INVALID_PARAMETER;
		}
		Urb->UrbControlTransfer.PipeHandle = ep0;
		Urb->UrbControlTransfer.TransferFlags =
			USBD_TRANSFER_DIRECTION_OUT |
			USBD_SHORT_TRANSFER_OK |
			USBD_DEFAULT_PIPE_TRANSFER;
		sp = (PUSB_DEFAULT_PIPE_SETUP_PACKET)&Urb->UrbControlTransfer.SetupPacket;
		sp->bmRequestType.Dir = BMREQUEST_HOST_TO_DEVICE;
		sp->bmRequestType.Type = BMREQUEST_STANDARD;
		if(Urb->UrbHeader.Function == URB_FUNCTION_SET_FEATURE_TO_DEVICE)
		{
			sp->bmRequestType.Recipient = BMREQUEST_TO_DEVICE;
		}
		else if(Urb->UrbHeader.Function == URB_FUNCTION_SET_FEATURE_TO_INTERFACE)
		{
			sp->bmRequestType.Recipient = BMREQUEST_TO_INTERFACE;
		}
		else if(Urb->UrbHeader.Function == URB_FUNCTION_SET_FEATURE_TO_ENDPOINT)
		{
			sp->bmRequestType.Recipient = BMREQUEST_TO_ENDPOINT;
		}
		else // if(Urb->UrbHeader.Function == URB_FUNCTION_SET_FEATURE_TO_OTHER)
		{
			sp->bmRequestType.Recipient = BMREQUEST_TO_OTHER;
		}
		Urb->UrbHeader.Function = URB_FUNCTION_CONTROL_TRANSFER;
		sp->bmRequestType.Reserved = 0;
		sp->bRequest = USB_REQUEST_SET_FEATURE;
		Urb->UrbControlTransfer.TransferBufferLength = 0;
		sp->wLength = 0;
		break;

	case URB_FUNCTION_CLEAR_FEATURE_TO_DEVICE:
	case URB_FUNCTION_CLEAR_FEATURE_TO_INTERFACE:
	case URB_FUNCTION_CLEAR_FEATURE_TO_ENDPOINT:
	case URB_FUNCTION_CLEAR_FEATURE_TO_OTHER:
		if(Urb->UrbHeader.Length < sizeof(struct _URB_CONTROL_FEATURE_REQUEST))
		{
			*UsbStatus = USBD_STATUS_INVALID_PARAMETER;
			return STATUS_INVALID_PARAMETER;
		}
		Urb->UrbControlTransfer.PipeHandle = ep0;
		Urb->UrbControlTransfer.TransferFlags =
			USBD_TRANSFER_DIRECTION_OUT |
			USBD_SHORT_TRANSFER_OK |
			USBD_DEFAULT_PIPE_TRANSFER;
		sp = (PUSB_DEFAULT_PIPE_SETUP_PACKET)&Urb->UrbControlTransfer.SetupPacket;
		sp->bmRequestType.Dir = BMREQUEST_HOST_TO_DEVICE;
		sp->bmRequestType.Type = BMREQUEST_STANDARD;
		if(Urb->UrbHeader.Function == URB_FUNCTION_CLEAR_FEATURE_TO_DEVICE)
		{
			sp->bmRequestType.Recipient = BMREQUEST_TO_DEVICE;
		}
		else if(Urb->UrbHeader.Function == URB_FUNCTION_CLEAR_FEATURE_TO_INTERFACE)
		{
			sp->bmRequestType.Recipient = BMREQUEST_TO_INTERFACE;
		}
		else if(Urb->UrbHeader.Function == URB_FUNCTION_CLEAR_FEATURE_TO_ENDPOINT)
		{
			sp->bmRequestType.Recipient = BMREQUEST_TO_ENDPOINT;
		}
		else // if(Urb->UrbHeader.Function == URB_FUNCTION_CLEAR_FEATURE_TO_OTHER)
		{
			sp->bmRequestType.Recipient = BMREQUEST_TO_OTHER;
		}
		Urb->UrbHeader.Function = URB_FUNCTION_CONTROL_TRANSFER;
		sp->bmRequestType.Reserved = 0;
		sp->bRequest = USB_REQUEST_CLEAR_FEATURE;
		Urb->UrbControlTransfer.TransferBufferLength = 0;
		sp->wLength = 0;
		break;

	case URB_FUNCTION_GET_STATUS_FROM_DEVICE:
	case URB_FUNCTION_GET_STATUS_FROM_INTERFACE:
	case URB_FUNCTION_GET_STATUS_FROM_ENDPOINT:
	case URB_FUNCTION_GET_STATUS_FROM_OTHER:
		if(Urb->UrbHeader.Length < sizeof(struct _URB_CONTROL_GET_STATUS_REQUEST))
		{
			*UsbStatus = USBD_STATUS_INVALID_PARAMETER;
			return STATUS_INVALID_PARAMETER;
		}
		Urb->UrbControlTransfer.PipeHandle = ep0;
		Urb->UrbControlTransfer.TransferFlags =
			USBD_TRANSFER_DIRECTION_IN |
			USBD_SHORT_TRANSFER_OK |
			USBD_DEFAULT_PIPE_TRANSFER;
		sp = (PUSB_DEFAULT_PIPE_SETUP_PACKET)&Urb->UrbControlTransfer.SetupPacket;
		sp->bmRequestType.Dir = BMREQUEST_DEVICE_TO_HOST;
		sp->bmRequestType.Type = BMREQUEST_STANDARD;
		if(Urb->UrbHeader.Function == URB_FUNCTION_GET_STATUS_FROM_DEVICE)
		{
			sp->bmRequestType.Recipient = BMREQUEST_TO_DEVICE;
		}
		else if(Urb->UrbHeader.Function == URB_FUNCTION_GET_STATUS_FROM_INTERFACE)
		{
			sp->bmRequestType.Recipient = BMREQUEST_TO_INTERFACE;
		}
		else if(Urb->UrbHeader.Function == URB_FUNCTION_GET_STATUS_FROM_ENDPOINT)
		{
			sp->bmRequestType.Recipient = BMREQUEST_TO_ENDPOINT;
		}
		else // if(Urb->UrbHeader.Function == URB_FUNCTION_GET_STATUS_FROM_OTHER)
		{
			sp->bmRequestType.Recipient = BMREQUEST_TO_OTHER;
		}
		Urb->UrbHeader.Function = URB_FUNCTION_CONTROL_TRANSFER;
		sp->bmRequestType.Reserved = 0;
		sp->bRequest = USB_REQUEST_GET_STATUS;
		sp->wValue.W = 0;
		sp->wLength = (USHORT)Urb->UrbControlTransfer.TransferBufferLength;
		goto CheckBuffer;

	case URB_FUNCTION_CLASS_DEVICE:
	case URB_FUNCTION_CLASS_INTERFACE:
	case URB_FUNCTION_CLASS_ENDPOINT:
	case URB_FUNCTION_CLASS_OTHER:
		if(Urb->UrbHeader.Length < sizeof(struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST))
		{
			*UsbStatus = USBD_STATUS_INVALID_PARAMETER;
			return STATUS_INVALID_PARAMETER;
		}
		Urb->UrbControlTransfer.PipeHandle = ep0;
		Urb->UrbControlTransfer.TransferFlags |=
			USBD_SHORT_TRANSFER_OK |
			USBD_DEFAULT_PIPE_TRANSFER;
		sp = (PUSB_DEFAULT_PIPE_SETUP_PACKET)&Urb->UrbControlTransfer.SetupPacket;
		if(Urb->UrbControlTransfer.TransferFlags & USBD_TRANSFER_DIRECTION_IN)
		{
			sp->bmRequestType.Dir = BMREQUEST_DEVICE_TO_HOST;
		}
		else
		{
			sp->bmRequestType.Dir = BMREQUEST_HOST_TO_DEVICE;
		}
		sp->bmRequestType.Type = BMREQUEST_CLASS;
		if(Urb->UrbHeader.Function == URB_FUNCTION_CLASS_DEVICE)
		{
			sp->bmRequestType.Recipient = BMREQUEST_TO_DEVICE;
		}
		else if(Urb->UrbHeader.Function == URB_FUNCTION_CLASS_INTERFACE)
		{
			sp->bmRequestType.Recipient = BMREQUEST_TO_INTERFACE;
		}
		else if(Urb->UrbHeader.Function == URB_FUNCTION_CLASS_ENDPOINT)
		{
			sp->bmRequestType.Recipient = BMREQUEST_TO_ENDPOINT;
		}
		else // if(Urb->UrbHeader.Function == URB_FUNCTION_CLASS_OTHER)
		{
			sp->bmRequestType.Recipient = BMREQUEST_TO_OTHER;
		}
		Urb->UrbHeader.Function = URB_FUNCTION_CONTROL_TRANSFER;
		sp->bmRequestType.Reserved = 0;
		sp->wLength = (USHORT)Urb->UrbControlTransfer.TransferBufferLength;
		goto CheckBuffer;

	case URB_FUNCTION_VENDOR_DEVICE:
	case URB_FUNCTION_VENDOR_INTERFACE:
	case URB_FUNCTION_VENDOR_ENDPOINT:
	case URB_FUNCTION_VENDOR_OTHER:
		if(Urb->UrbHeader.Length < sizeof(struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST))
		{
			*UsbStatus = USBD_STATUS_INVALID_PARAMETER;
			return STATUS_INVALID_PARAMETER;
		}
		Urb->UrbControlTransfer.PipeHandle = ep0;
		Urb->UrbControlTransfer.TransferFlags |=
			USBD_SHORT_TRANSFER_OK |
			USBD_DEFAULT_PIPE_TRANSFER;
		sp = (PUSB_DEFAULT_PIPE_SETUP_PACKET)&Urb->UrbControlTransfer.SetupPacket;
		if(Urb->UrbControlTransfer.TransferFlags & USBD_TRANSFER_DIRECTION_IN)
		{
			sp->bmRequestType.Dir = BMREQUEST_DEVICE_TO_HOST;
		}
		else
		{
			sp->bmRequestType.Dir = BMREQUEST_HOST_TO_DEVICE;
		}
		sp->bmRequestType.Type = BMREQUEST_VENDOR;
		if(Urb->UrbHeader.Function == URB_FUNCTION_VENDOR_DEVICE)
		{
			sp->bmRequestType.Recipient = BMREQUEST_TO_DEVICE;
		}
		else if(Urb->UrbHeader.Function == URB_FUNCTION_VENDOR_INTERFACE)
		{
			sp->bmRequestType.Recipient = BMREQUEST_TO_INTERFACE;
		}
		else if(Urb->UrbHeader.Function == URB_FUNCTION_VENDOR_ENDPOINT)
		{
			sp->bmRequestType.Recipient = BMREQUEST_TO_ENDPOINT;
		}
		else // if(Urb->UrbHeader.Function == URB_FUNCTION_VENDOR_OTHER)
		{
			sp->bmRequestType.Recipient = BMREQUEST_TO_OTHER;
		}
		Urb->UrbHeader.Function = URB_FUNCTION_CONTROL_TRANSFER;
		sp->bmRequestType.Reserved = 0;
		sp->wLength = (USHORT)Urb->UrbControlTransfer.TransferBufferLength;
		goto CheckBuffer;

	case URB_FUNCTION_GET_CONFIGURATION:
		if(Urb->UrbHeader.Length < sizeof(struct _URB_CONTROL_GET_CONFIGURATION_REQUEST))
		{
			*UsbStatus = USBD_STATUS_INVALID_PARAMETER;
			return STATUS_INVALID_PARAMETER;
		}
		Urb->UrbControlTransfer.PipeHandle = ep0;
		Urb->UrbControlTransfer.TransferFlags =
			USBD_TRANSFER_DIRECTION_IN |
			USBD_SHORT_TRANSFER_OK |
			USBD_DEFAULT_PIPE_TRANSFER;
		sp = (PUSB_DEFAULT_PIPE_SETUP_PACKET)&Urb->UrbControlTransfer.SetupPacket;
		sp->bmRequestType.Dir = BMREQUEST_DEVICE_TO_HOST;
		sp->bmRequestType.Type = BMREQUEST_STANDARD;
		sp->bmRequestType.Recipient = BMREQUEST_TO_DEVICE;
		Urb->UrbHeader.Function = URB_FUNCTION_CONTROL_TRANSFER;
		sp->bmRequestType.Reserved = 0;
		sp->bRequest = USB_REQUEST_GET_CONFIGURATION;
		sp->wValue.W = 0;
		sp->wIndex.W = 0;
		sp->wLength = (USHORT)Urb->UrbControlTransfer.TransferBufferLength;
		goto CheckBuffer;

	case URB_FUNCTION_GET_INTERFACE:
		if(Urb->UrbHeader.Length < sizeof(struct _URB_CONTROL_GET_CONFIGURATION_REQUEST))
		{
			*UsbStatus = USBD_STATUS_INVALID_PARAMETER;
			return STATUS_INVALID_PARAMETER;
		}
		Urb->UrbControlTransfer.PipeHandle = ep0;
		Urb->UrbControlTransfer.TransferFlags =
			USBD_TRANSFER_DIRECTION_IN |
			USBD_SHORT_TRANSFER_OK |
			USBD_DEFAULT_PIPE_TRANSFER;
		sp = (PUSB_DEFAULT_PIPE_SETUP_PACKET)&Urb->UrbControlTransfer.SetupPacket;
		sp->bmRequestType.Dir = BMREQUEST_DEVICE_TO_HOST;
		sp->bmRequestType.Type = BMREQUEST_STANDARD;
		sp->bmRequestType.Recipient = BMREQUEST_TO_INTERFACE;
		Urb->UrbHeader.Function = URB_FUNCTION_CONTROL_TRANSFER;
		sp->bmRequestType.Reserved = 0;
		sp->bRequest = USB_REQUEST_GET_INTERFACE;
		sp->wValue.W = 0;
		sp->wLength = (USHORT)Urb->UrbControlTransfer.TransferBufferLength;
		goto CheckBuffer;

	case URB_FUNCTION_ABORT_PIPE:
#ifdef TARGETING_Win2K
	case URB_FUNCTION_RESET_PIPE:
#else
	case URB_FUNCTION_SYNC_RESET_PIPE_AND_CLEAR_STALL:
	case URB_FUNCTION_SYNC_RESET_PIPE:
	case URB_FUNCTION_SYNC_CLEAR_STALL:
#endif
		if(Urb->UrbHeader.Length < sizeof(struct _URB_PIPE_REQUEST))
		{
			*UsbStatus = USBD_STATUS_INVALID_PARAMETER;
			return STATUS_INVALID_PARAMETER;
		}
		if(!Urb->UrbPipeRequest.PipeHandle)
		{
			Urb->UrbPipeRequest.PipeHandle = ep0;
		}
		break;

#ifndef TARGETING_Win2K
	case URB_FUNCTION_GET_MS_FEATURE_DESCRIPTOR:
		if(Urb->UrbHeader.Length < sizeof(struct _URB_OS_FEATURE_DESCRIPTOR_REQUEST))
		{
			*UsbStatus = USBD_STATUS_INVALID_PARAMETER;
			return STATUS_INVALID_PARAMETER;
		}
		Urb->UrbControlTransfer.PipeHandle = ep0;
		Urb->UrbControlTransfer.TransferFlags =
			USBD_TRANSFER_DIRECTION_IN |
			USBD_SHORT_TRANSFER_OK |
			USBD_DEFAULT_PIPE_TRANSFER;
		sp = (PUSB_DEFAULT_PIPE_SETUP_PACKET)&Urb->UrbControlTransfer.SetupPacket;
		sp->bmRequestType.Dir = BMREQUEST_DEVICE_TO_HOST;
		sp->bmRequestType.Type = BMREQUEST_VENDOR;
		Urb->UrbHeader.Function = URB_FUNCTION_CONTROL_TRANSFER;
		// TODO: is this correct?
		sp->bRequest = USB_REQUEST_GET_DESCRIPTOR;
		sp->wLength = (USHORT)Urb->UrbControlTransfer.TransferBufferLength;
		goto CheckBuffer;
#endif // !TARGETING_Win2K

CheckBuffer:
		if(!Urb->UrbControlTransfer.TransferBuffer &&
		   !Urb->UrbControlTransfer.TransferBufferMDL &&
		   Urb->UrbControlTransfer.TransferBufferLength)
		{
			*UsbStatus = USBD_STATUS_INVALID_PARAMETER;
			return STATUS_INVALID_PARAMETER;
		}
	case URB_FUNCTION_SELECT_CONFIGURATION:
	case URB_FUNCTION_SELECT_INTERFACE:
	case URB_FUNCTION_GET_CURRENT_FRAME_NUMBER:
		// nothing to do
		break;

	case URB_FUNCTION_TAKE_FRAME_LENGTH_CONTROL:
	case URB_FUNCTION_RELEASE_FRAME_LENGTH_CONTROL:
	case URB_FUNCTION_GET_FRAME_LENGTH:
	case URB_FUNCTION_SET_FRAME_LENGTH:
		// obsolete in Windows 2000 and later
	default:
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_INTERNAL_IO, "unknown urb function: %hu", Urb->UrbHeader.Function);
		*UsbStatus = USBD_STATUS_INVALID_URB_FUNCTION;
		return STATUS_INVALID_PARAMETER;
	}
	return STATUS_SUCCESS;
}
