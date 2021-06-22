#include "virtusb.h"
#include "proc_urb.h"
#include "roothub.h"
#include "user_io.h"
#include "work_unit.h"
#include "usbdev.h"

NTSTATUS
VirtUsb_AllocateSelectConfigurationWorkUnit(
	OUT    PWORK            *WorkUnit,
	IN     PPDO_DEVICE_DATA PdoData,
	IN OUT PIRP             Irp,
	IN     PPIPE_CONTEXT    Pipe
	);

LONG
VirtUsb_SelectConfigurationWorkUnitSubUrbCompletionHandler(
	IN OUT PWORK        WorkUnit,
	IN OUT PWORK_SUBURB SubUrb,
	IN     USBD_STATUS  UsbdStatus
	);

NTSTATUS
VirtUsb_SelectConfigurationWorkUnitCompletionHandler(
	IN OUT PWORK    WorkUnit,
	IN     NTSTATUS Status,
	IN     BOOLEAN  MustCancel
	);

NTSTATUS
VirtUsb_AllocateSelectInterfaceWorkUnit(
	OUT    PWORK            *WorkUnit,
	IN     PPDO_DEVICE_DATA PdoData,
	IN OUT PIRP             Irp,
	IN     PPIPE_CONTEXT    Pipe
	);

LONG
VirtUsb_SelectInterfaceWorkUnitSubUrbCompletionHandler(
	IN OUT PWORK        WorkUnit,
	IN OUT PWORK_SUBURB SubUrb,
	IN     USBD_STATUS  UsbdStatus
	);

NTSTATUS
VirtUsb_SelectInterfaceWorkUnitCompletionHandler(
	IN OUT PWORK    WorkUnit,
	IN     NTSTATUS Status,
	IN     BOOLEAN  MustCancel
	);

NTSTATUS
VirtUsb_ProcUrb(
	IN PPDO_DEVICE_DATA PdoData,
	IN PIRP             Irp
	)
/*
    IRQL <= DISPATCH_LEVEL
*/
{
	KIRQL           irql, cancelIrql;
	NTSTATUS        ntStatus = STATUS_SUCCESS;
	USBD_STATUS     usbStatus = USBD_STATUS_SUCCESS;
	PURB            urb;
	PWORK           wu;
	PUSBDEV_CONTEXT dev = NULL;
	PUSBHUB_CONTEXT rhub;
	PPIPE_CONTEXT   ep;
	UCHAR           urbType = VIRTUSB_URB_TYPE_BULK;
	BOOLEAN         isIn = FALSE;

	ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);

	VIRTUSB_KDPRINT("in VirtUsb_ProcUrb\n");

	ASSERT(PdoData);
	ASSERT(Irp);
	urb = URB_FROM_IRP(Irp);
	ASSERT(urb);

	if(urb->UrbHeader.Length < sizeof(struct _URB_HEADER))
	{
		VIRTUSB_KDPRINT("'very' invalid URB\n");
		ntStatus = STATUS_INVALID_PARAMETER;
		Irp->IoStatus.Status = ntStatus;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		return ntStatus;
	}

	rhub = PdoData->RootHub;
	if(!rhub)
	{
		VIRTUSB_KDPRINT("!rhub\n");
		usbStatus = USBD_STATUS_DEV_NOT_RESPONDING;
		ntStatus = STATUS_NO_SUCH_DEVICE;
		goto End;
	}

	// VUsbVhci should set it, if it is not
	if(!urb->UrbHeader.UsbdDeviceHandle)
	{
		VIRTUSB_KDPRINT("!urb->UrbHeader.UsbdDeviceHandle\n");
		usbStatus = USBD_STATUS_INVALID_PARAMETER;
		ntStatus = STATUS_INVALID_PARAMETER;
		goto End;
	}

	dev = VirtUsb_ReferenceUsbDeviceByHandle(PdoData, urb->UrbHeader.UsbdDeviceHandle, Irp);
	if(!dev)
	{
		VIRTUSB_KDPRINT("invalid urb->UrbHeader.UsbdDeviceHandle\n");
		usbStatus = USBD_STATUS_DEVICE_GONE;
		ntStatus = STATUS_DEVICE_NOT_CONNECTED;
		goto End;
	}

	if(!dev->Ready)
	{
		VIRTUSB_KDPRINT("device not ready\n");
		VirtUsb_DereferenceUsbDevice(dev, Irp, FALSE);
		usbStatus = USBD_STATUS_ERROR_BUSY;
		ntStatus = STATUS_DEVICE_NOT_READY;
		goto End;
	}

	// check if it is for the root-hub
	if(urb->UrbHeader.UsbdDeviceHandle == rhub)
	{
		VIRTUSB_KDPRINT("root-hub got an URB\n");
		return VirtUsb_ProcRootHubUrb(PdoData, Irp);
	}

	switch(urb->UrbHeader.Function)
	{
	case URB_FUNCTION_CONTROL_TRANSFER:
	case URB_FUNCTION_ISOCH_TRANSFER:
	case URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER:
		if(!VirtUsb_IsValidPipeHandle(dev, urb->UrbControlTransfer.PipeHandle))
		{
			VIRTUSB_KDPRINT1("pipe handle is invalid: 0x%p\n",
				urb->UrbControlTransfer.PipeHandle);
			usbStatus = USBD_STATUS_INVALID_PARAMETER;
			ntStatus = STATUS_INVALID_PARAMETER;
			goto End;
		}
		ep = (PPIPE_CONTEXT)urb->UrbControlTransfer.PipeHandle;

		switch(urb->UrbHeader.Function)
		{
		case URB_FUNCTION_CONTROL_TRANSFER:
			urbType = VIRTUSB_URB_TYPE_CONTROL;
			if(!ep->IsEP0 && !ep->Descriptor)
				goto NoDescriptor;
			isIn = !!(urb->UrbControlTransfer.TransferFlags & USBD_TRANSFER_DIRECTION_IN);
			break;
		case URB_FUNCTION_ISOCH_TRANSFER:
			if(!ep->Descriptor)
				goto NoDescriptor;
			urbType = VIRTUSB_URB_TYPE_ISO;
			isIn = !!(ep->Descriptor->bEndpointAddress & 0x80);
			if(isIn)
				urb->UrbIsochronousTransfer.TransferFlags |= USBD_TRANSFER_DIRECTION_IN;
			else
				urb->UrbIsochronousTransfer.TransferFlags &= ~USBD_TRANSFER_DIRECTION_IN;
			break;
		case URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER:
			if(!ep->Descriptor)
				goto NoDescriptor;
			if((ep->Descriptor->bmAttributes & 0x03) == USB_ENDPOINT_TYPE_INTERRUPT)
			{
				urbType = VIRTUSB_URB_TYPE_INT;
			}
			isIn = !!(ep->Descriptor->bEndpointAddress & 0x80);
			if(isIn)
				urb->UrbBulkOrInterruptTransfer.TransferFlags |= USBD_TRANSFER_DIRECTION_IN;
			else
				urb->UrbBulkOrInterruptTransfer.TransferFlags &= ~USBD_TRANSFER_DIRECTION_IN;
			break;
NoDescriptor:
			VIRTUSB_KDPRINT("no ep descriptor\n");
			usbStatus = USBD_STATUS_INVALID_PIPE_HANDLE;
			ntStatus = STATUS_INVALID_PARAMETER;
			goto End;
		}

		if(urbType == VIRTUSB_URB_TYPE_CONTROL)
		{
			PVIRTUSB_SETUP_PACKET sp = (PVIRTUSB_SETUP_PACKET)urb->UrbControlTransfer.SetupPacket;
			if(sp->wLength > urb->UrbControlTransfer.TransferBufferLength)
			{
				VIRTUSB_KDPRINT("sp->wLength > urb->UrbControlTransfer.TransferBufferLength\n");
				usbStatus = USBD_STATUS_INVALID_PARAMETER;
				ntStatus = STATUS_INVALID_PARAMETER;
				goto End;
			}
			if(!!(sp->bmRequestType & 0x80) != isIn)
			{
				VIRTUSB_KDPRINT("!!(sp->bRequestType & 0x80) != isIn\n");
				usbStatus = USBD_STATUS_INVALID_PARAMETER;
				ntStatus = STATUS_INVALID_PARAMETER;
				goto End;
			}
			if(isIn)
			{
				if(!sp->wLength || !(urb->UrbControlTransfer.TransferBuffer || urb->UrbControlTransfer.TransferBufferMDL))
				{
					VIRTUSB_KDPRINT("!sp->wLength || !(urb->UrbControlTransfer.TransferBuffer || urb->UrbControlTransfer.TransferBufferMDL)\n");
					usbStatus = USBD_STATUS_INVALID_PARAMETER;
					ntStatus = STATUS_INVALID_PARAMETER;
					goto End;
				}
			}
			else
			{
				if(sp->wLength && !(urb->UrbControlTransfer.TransferBuffer || urb->UrbControlTransfer.TransferBufferMDL))
				{
					VIRTUSB_KDPRINT("sp->wLength && !(urb->UrbControlTransfer.TransferBuffer || urb->UrbControlTransfer.TransferBufferMDL)\n");
					usbStatus = USBD_STATUS_INVALID_PARAMETER;
					ntStatus = STATUS_INVALID_PARAMETER;
					goto End;
				}
			}
		}
		else
		{
			if(isIn)
			{
				if(!urb->UrbControlTransfer.TransferBufferLength || !(urb->UrbControlTransfer.TransferBuffer || urb->UrbControlTransfer.TransferBufferMDL))
				{
					VIRTUSB_KDPRINT("!urb->UrbControlTransfer.TransferBufferLength || !(urb->UrbControlTransfer.TransferBuffer || urb->UrbControlTransfer.TransferBufferMDL)\n");
					usbStatus = USBD_STATUS_INVALID_PARAMETER;
					ntStatus = STATUS_INVALID_PARAMETER;
					goto End;
				}
			}
			else
			{
				if(urb->UrbControlTransfer.TransferBufferLength && !(urb->UrbControlTransfer.TransferBuffer || urb->UrbControlTransfer.TransferBufferMDL))
				{
					VIRTUSB_KDPRINT("urb->UrbControlTransfer.TransferBufferLength && !(urb->UrbControlTransfer.TransferBuffer || urb->UrbControlTransfer.TransferBufferMDL)\n");
					usbStatus = USBD_STATUS_INVALID_PARAMETER;
					ntStatus = STATUS_INVALID_PARAMETER;
					goto End;
				}
			}
		}
		break;

	case URB_FUNCTION_SELECT_CONFIGURATION:
		ntStatus = VirtUsb_AllocateSelectConfigurationWorkUnit(&wu, PdoData, Irp, &dev->EndpointZero);
		if(!NT_SUCCESS(ntStatus))
		{
			usbStatus = urb->UrbHeader.Status;
			goto End;
		}
		goto WithOwnWorkUnit;

	case URB_FUNCTION_SELECT_INTERFACE:
		ntStatus = VirtUsb_AllocateSelectInterfaceWorkUnit(&wu, PdoData, Irp, &dev->EndpointZero);
		if(!NT_SUCCESS(ntStatus))
		{
			usbStatus = urb->UrbHeader.Status;
			goto End;
		}
		goto WithOwnWorkUnit;

	case URB_FUNCTION_ABORT_PIPE:
#ifdef TARGETING_Win2K
	case URB_FUNCTION_RESET_PIPE:
#else
	case URB_FUNCTION_SYNC_RESET_PIPE_AND_CLEAR_STALL:
	case URB_FUNCTION_SYNC_RESET_PIPE:
	case URB_FUNCTION_SYNC_CLEAR_STALL:
#endif
	case URB_FUNCTION_GET_CURRENT_FRAME_NUMBER:
		// TODO: implement

	default:
		VIRTUSB_KDPRINT1("unknown urb function: %hu\n", urb->UrbHeader.Function);
		usbStatus = USBD_STATUS_INVALID_URB_FUNCTION;
		ntStatus = STATUS_INVALID_PARAMETER;
		goto End;
	}

	ASSERT(ep->Device == dev);
	wu = VirtUsb_AllocateSimpleUrbWorkUnit(PdoData, Irp, ep);
	if(!wu)
	{
		usbStatus = USBD_STATUS_INSUFFICIENT_RESOURCES;
		ntStatus = STATUS_INSUFFICIENT_RESOURCES;
		goto End;
	}

WithOwnWorkUnit:
	ASSERT(wu);
	KeAcquireSpinLock(&PdoData->Lock, &irql);
	IoMarkIrpPending(Irp);
	IoAcquireCancelSpinLock(&cancelIrql);
	IoSetCancelRoutine(Irp, &VirtUsb_CancelUrbIrp);
	if(Irp->Cancel)
	{
		PDRIVER_CANCEL cancelCallback = IoSetCancelRoutine(Irp, NULL);
		IoReleaseCancelSpinLock(cancelIrql);
		InitializeListHead(&wu->ListEntry); // (to 'no-op-ify' RemoveEntryList(&wu->ListEntry) in cancel routine)
		KeReleaseSpinLock(&PdoData->Lock, irql);
		if(cancelCallback)
		{
			VirtUsb_CompleteWorkUnitCurrentUrb(wu, USBD_STATUS_CANCELED);
			ntStatus = VirtUsb_CompleteWorkUnit(wu, STATUS_CANCELLED);
			if(dev) VirtUsb_DereferenceUsbDevice(dev, Irp, FALSE);
			VirtUsb_FreeWorkUnit(wu);
			IoCompleteRequest(Irp, IO_NO_INCREMENT);
			IoReleaseRemoveLock(&PdoData->RemoveLock, Irp);
			return ntStatus;
		}
		// cancel routine was already called, so we have to return STATUS_PENDING
	}
	else
	{
		IoReleaseCancelSpinLock(cancelIrql);
		InsertTailList(&PdoData->PendingWorkList, &wu->ListEntry);
		KeReleaseSpinLock(&PdoData->Lock, irql);
	}
	urb->UrbHeader.Status = USBD_STATUS_PENDING;
	return STATUS_PENDING;

End:
	if(dev) VirtUsb_DereferenceUsbDevice(dev, Irp, FALSE);
	urb->UrbHeader.Status = usbStatus;
	Irp->IoStatus.Status = ntStatus;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	IoReleaseRemoveLock(&PdoData->RemoveLock, Irp);
	return ntStatus;
}

VOID
VirtUsb_CancelUrbIrp(
	__inout PDEVICE_OBJECT DeviceObject,
	__in    PIRP           Irp
	)
{
	KIRQL            irql;
	PPDO_DEVICE_DATA pdoData;
	PWORK            wu;
	PPIPE_CONTEXT    pipe;
	PLIST_ENTRY      entry, nextEntry;
	KEVENT           event;
	LARGE_INTEGER    timeout;

	ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);

	IoReleaseCancelSpinLock(DISPATCH_LEVEL);

	VIRTUSB_KDPRINT("in VirtUsb_CancelUrbIrp\n");

	ASSERT(DeviceObject);
	ASSERT(Irp);
	wu = WORK_FROM_IRP(Irp);
	ASSERT(wu);
	ASSERT(wu->Irp == Irp);
	pipe = wu->Pipe;
	ASSERT(pipe);
	pdoData = DeviceObject->DeviceExtension;
	ASSERT(pdoData);
	ASSERT(!pdoData->IsFDO);
	ASSERT(pdoData->Self == DeviceObject);
	ASSERT(pdoData == wu->PdoData);

	KeAcquireSpinLockAtDpcLevel(&pdoData->Lock);

	// check if someone nearly completed the irp between our
	// IoReleaseCancelSpinLock and KeAcquireSpinLockAtDpcLevel
	if(wu->CancelRace)
	{
		KeReleaseSpinLockFromDpcLevel(&pdoData->Lock);
		VirtUsb_DereferenceUsbDevice(pipe->Device, Irp, FALSE);
		KeLowerIrql(Irp->CancelIrql);
#if DBG
		wu->Completed = TRUE;
#endif
		VirtUsb_FreeWorkUnit(wu);
		// we just have to complete the irp; status is already set by
		// whom who set wu->CancelRace.
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		IoReleaseRemoveLock(&pdoData->RemoveLock, Irp);
		return;
	}

	for(entry = wu->ListEntry.Flink, nextEntry = entry->Flink;
	    entry != &wu->ListEntry;
	    entry = nextEntry, nextEntry = entry->Flink)
	{
		// if already fetched, then we have to wait until user-space has canceled the urb
		if(entry == &pdoData->FetchedWorkList)
			goto DoWait;
	}
	RemoveEntryList(&wu->ListEntry);
	KeReleaseSpinLockFromDpcLevel(&pdoData->Lock);
	KeLowerIrql(Irp->CancelIrql);
	VirtUsb_CompleteWorkUnitCurrentUrb(wu, USBD_STATUS_CANCELED);
	VirtUsb_CompleteWorkUnit(wu, STATUS_CANCELLED);
	VirtUsb_FreeWorkUnit(wu);
	VirtUsb_DereferenceUsbDevice(pipe->Device, Irp, FALSE);
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	IoReleaseRemoveLock(&pdoData->RemoveLock, Irp);
	return;

DoWait:
	RemoveEntryList(&wu->ListEntry);
	InsertTailList(&pdoData->CancelWorkList, &wu->ListEntry);
	KeInitializeEvent(&event, NotificationEvent, FALSE);
	ASSERT(!wu->CancelEvent);
	wu->CancelEvent = &event;
	VirtUsb_GotWork(pdoData, Irp->CancelIrql); // releases spin lock

	timeout.QuadPart = -100000000; // 10 seconds

	// If the irp is canceled at dispatch level, then we have to cancel it directly
	// without telling user-mode first.
	ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);
	if(KeGetCurrentIrql() == DISPATCH_LEVEL)
		timeout.QuadPart = 0;

	if(KeWaitForSingleObject(&event,
	                         Executive,
	                         KernelMode,
	                         FALSE,
	                         &timeout)
		== STATUS_TIMEOUT)
	{
		KeAcquireSpinLock(&pdoData->Lock, &irql);
		// check if event gots set just before we acquired he spin lock
		if(KeSetEvent(&event, IO_NO_INCREMENT, FALSE))
		{
			KeReleaseSpinLock(&pdoData->Lock, irql);
			goto EventSet;
		}
		wu->CancelEvent = NULL;
		VirtUsb_HardCancelWorkUnit(wu);
		KeReleaseSpinLock(&pdoData->Lock, irql);
		((PURB)URB_FROM_IRP(Irp))->UrbHeader.Status = USBD_STATUS_CANCELED;
		Irp->IoStatus.Status = STATUS_CANCELLED;
		Irp->IoStatus.Information = 0;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		IoReleaseRemoveLock(&pdoData->RemoveLock, Irp);
		return;
	}

EventSet:
	// the one who triggers the event sets the status

	VirtUsb_FreeWorkUnit(wu);
	VirtUsb_DereferenceUsbDevice(pipe->Device, Irp, FALSE);
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	IoReleaseRemoveLock(&pdoData->RemoveLock, Irp);
}

NTSTATUS
VirtUsb_SubmitUrb(
	IN     PPDO_DEVICE_DATA PdoData,
	IN OUT PIRP             Irp,
	IN OUT PURB             Urb
	)
/*
    IRQL = PASSIVE_LEVEL
*/
{
	PIO_STACK_LOCATION irpStack;
	NTSTATUS           status;
	KEVENT             event;
	LARGE_INTEGER      timeout;

	ASSERT(KeGetCurrentIrql() < DISPATCH_LEVEL);

	VIRTUSB_KDPRINT("in VirtUsb_SubmitUrb\n");

	ASSERT(PdoData);
	ASSERT(Irp);
	ASSERT(Urb);

	KeInitializeEvent(&event, NotificationEvent, FALSE);
	timeout.QuadPart = -100000000; // 10 seconds

	irpStack = IoGetNextIrpStackLocation(Irp);
	irpStack->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
	irpStack->Parameters.Others.Argument1 = Urb;
	irpStack->Parameters.DeviceIoControl.IoControlCode = IOCTL_INTERNAL_USB_SUBMIT_URB; // (==Others.Argument3)
	Irp->IoStatus.Status = STATUS_UNSUCCESSFUL;
	Irp->IoStatus.Information = 0;

	IoSetCompletionRoutine(Irp,
	                       VirtUsb_CompletionRoutine,
	                       &event,
	                       TRUE,
	                       TRUE,
	                       TRUE);

	status = IoCallDriver(PdoData->Self, Irp);

	if(status == STATUS_PENDING)
	{
		status = KeWaitForSingleObject(&event,
		                               Executive,
		                               KernelMode,
		                               FALSE,
		                               &timeout);
		if(status == STATUS_TIMEOUT)
		{
			if(!IoCancelIrp(Irp))
			{
				KeWaitForSingleObject(&event,
				                      Executive,
				                      KernelMode,
				                      FALSE,
				                      NULL);
			}
		}
		status = Irp->IoStatus.Status;
	}

	return status;
}

NTSTATUS
VirtUsb_AllocateSelectConfigurationWorkUnit(
	OUT    PWORK            *WorkUnit,
	IN     PPDO_DEVICE_DATA PdoData,
	IN OUT PIRP             Irp,
	IN     PPIPE_CONTEXT    Pipe
	)
{
	struct _URB_SELECT_CONFIGURATION *selectConf;
	PUSBD_INTERFACE_INFORMATION      ifcInf;
	PVIRTUSB_SETUP_PACKET            sp;
	PURB                             urb, subUrb;
	PURB                             subUrbArr = NULL;
	PUSBDEV_CONTEXT                  dev;
	PWORK                            wu = NULL;
	ULONG                            i, j;
	ULONG                            numSubUrbs = 1;
	LONG                             confIndex = -1;
	PCONF_CONTEXT                    conf = NULL;
	UCHAR                            confValue = 0;

	ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);

	VIRTUSB_KDPRINT("in VirtUsb_AllocateSelectConfigurationWorkUnit\n");

	ASSERT(WorkUnit);
	ASSERT(PdoData);
	ASSERT(Irp);
	ASSERT(Pipe);
	urb = URB_FROM_IRP(Irp);
	ASSERT(urb);
	dev = Pipe->Device;
	ASSERT(dev);

	VIRTUSB_KDPRINT("URB_FUNCTION_SELECT_CONFIGURATION\n");
	VIRTUSB_KDPRINT1("--> Hdr.Length:              %lu\n", urb->UrbHeader.Length);
	selectConf = &urb->UrbSelectConfiguration;
	VIRTUSB_KDPRINT1("--> ConfigurationDescriptor: 0x%p\n", selectConf->ConfigurationDescriptor);
	VIRTUSB_KDPRINT1("--> ConfigurationHandle:     0x%p\n", selectConf->ConfigurationHandle);
	if(selectConf->ConfigurationDescriptor)
	{
		confValue = selectConf->ConfigurationDescriptor->bConfigurationValue;

		// TODO: Implement a routine for dumping
		//       configuration descriptors and call
		//       it here.
		VIRTUSB_KDPRINT1("--> *** ConfigurationDescriptor.bConfigurationValue: %02hu\n", (USHORT)confValue);

		// Search for the config
		for(confIndex = 0; (ULONG)confIndex < dev->ConfigurationCount; confIndex++)
		{
			conf = &dev->Configuration[(ULONG)confIndex];
			if(conf->Descriptor)
			{
				if(conf->Descriptor->bConfigurationValue == confValue)
				{
					goto ConfigFound;
				}
			}
		}
		VIRTUSB_KDPRINT("*** config not found ***\n");
		goto Stall; // (if not found)
ConfigFound:
		VIRTUSB_KDPRINT2("*** config index is %ld; handle&context are 0x%p ***\n", confIndex, conf);
		selectConf->ConfigurationHandle = conf;

		numSubUrbs += conf->Descriptor->bNumInterfaces;
	}

	subUrbArr = ExAllocatePoolWithTag(NonPagedPool,
	                                  sizeof(URB) * numSubUrbs,
	                                  VIRTUSB_WORK_POOL_TAG);
	if(!subUrbArr)
	{
		urb->UrbHeader.Status = USBD_STATUS_INSUFFICIENT_RESOURCES;
		return STATUS_INSUFFICIENT_RESOURCES;
	}
	RtlZeroMemory(subUrbArr, sizeof(URB) * numSubUrbs);

	wu = VirtUsb_AllocateMultipleUrbsWorkUnit(PdoData, Irp, Pipe, numSubUrbs);
	if(!wu)
	{
		ExFreePoolWithTag(subUrbArr, VIRTUSB_WORK_POOL_TAG);
		urb->UrbHeader.Status = USBD_STATUS_INSUFFICIENT_RESOURCES;
		return STATUS_INSUFFICIENT_RESOURCES;
	}
	wu->CompletionHandler = &VirtUsb_SelectConfigurationWorkUnitCompletionHandler;

	subUrb = subUrbArr;
	sp = (PVIRTUSB_SETUP_PACKET)subUrb->UrbControlTransfer.SetupPacket;
	subUrb->UrbHeader.Length                        = sizeof(URB);
	subUrb->UrbHeader.Function                      = URB_FUNCTION_CONTROL_TRANSFER;
	subUrb->UrbHeader.Status                        = USBD_STATUS_PENDING;
	subUrb->UrbHeader.UsbdDeviceHandle              = dev;
	subUrb->UrbControlTransfer.PipeHandle           = Pipe;
	subUrb->UrbControlTransfer.TransferFlags        = USBD_TRANSFER_DIRECTION_OUT |
	                                                  USBD_SHORT_TRANSFER_OK |
	                                                  USBD_DEFAULT_PIPE_TRANSFER;
	subUrb->UrbControlTransfer.TransferBufferLength = 0;
	subUrb->UrbControlTransfer.TransferBuffer       = NULL;
	sp->bmRequestType                               = 0x00;
	sp->bRequest                                    = USB_REQUEST_SET_CONFIGURATION;
	sp->wValue                                      = confValue;
	sp->wIndex                                      = 0;
	sp->wLength                                     = 0;
	wu->SubUrb[0].Urb               = subUrb;
	wu->SubUrb[0].CompletionHandler = &VirtUsb_SelectConfigurationWorkUnitSubUrbCompletionHandler;
	wu->SubUrb[0].Context[0]        = (PVOID)(ULONG_PTR)(ULONG)confIndex;

	if(selectConf->ConfigurationDescriptor)
	{
		ifcInf = &selectConf->Interface;
		for(i = 0; i < conf->Descriptor->bNumInterfaces; i++)
		{
			UCHAR         ifcValue = ifcInf->InterfaceNumber,
			              altValue = ifcInf->AlternateSetting;
			ULONG         ifcIndex, altIndex;
			PIFC_CONTEXT  ifc;
			PAIFC_CONTEXT alt;
			// Search for the interface
			for(ifcIndex = 0; ifcIndex < conf->InterfaceCount; ifcIndex++)
			{
				ifc = &conf->Interface[ifcIndex];
				if(ifc->Descriptor)
				{
					if(ifc->Descriptor->bInterfaceNumber == ifcValue)
					{
						goto IfcFound;
					}
				}
			}
			VIRTUSB_KDPRINT("*** ifc not found ***\n");
			goto Stall; // (if not found)
IfcFound:
			VIRTUSB_KDPRINT2("*** ifc index is %lu; context is 0x%p ***\n", ifcIndex, ifc);
			// Search for the alternate setting
			for(altIndex = 0; altIndex < ifc->AltSettingCount; altIndex++)
			{
				alt = &ifc->AltSetting[altIndex];
				if(alt->Descriptor)
				{
					if(alt->Descriptor->bAlternateSetting == altValue)
					{
						goto AltFound;
					}
				}
			}
			VIRTUSB_KDPRINT("*** alternate setting not found ***\n");
			goto Stall; // (if not found)
AltFound:
			VIRTUSB_KDPRINT2("*** alternate setting index is %lu; handle&context are 0x%p ***\n", altIndex, alt);
			ifcInf->InterfaceHandle = alt;
			ifcInf->Class = alt->Descriptor->bInterfaceClass;
			ifcInf->SubClass = alt->Descriptor->bInterfaceSubClass;
			ifcInf->Protocol = alt->Descriptor->bInterfaceProtocol;
			ifcInf->Reserved = 0;
			VIRTUSB_KDPRINT2("ifcInf[%lu]->Length:           %hu\n", i, ifcInf->Length);
			VIRTUSB_KDPRINT2("ifcInf[%lu]->InterfaceNumber:  %hu\n", i, (USHORT)ifcValue);
			VIRTUSB_KDPRINT2("ifcInf[%lu]->AlternateSetting: %hu\n", i, (USHORT)altValue);
			VIRTUSB_KDPRINT2("ifcInf[%lu]->Class:            0x%02hx\n", i, (USHORT)ifcInf->Class);
			VIRTUSB_KDPRINT2("ifcInf[%lu]->SubClass:         0x%02hx\n", i, (USHORT)ifcInf->SubClass);
			VIRTUSB_KDPRINT2("ifcInf[%lu]->Protocol:         0x%02hx\n", i, (USHORT)ifcInf->Protocol);
			VIRTUSB_KDPRINT2("ifcInf[%lu]->Reserved:         0x%02hx\n", i, (USHORT)ifcInf->Reserved);
			VIRTUSB_KDPRINT2("ifcInf[%lu]->InterfaceHandle:  0x%p\n", i, ifcInf->InterfaceHandle);
			VIRTUSB_KDPRINT2("ifcInf[%lu]->NumberOfPipes:    %lu\n", i, ifcInf->NumberOfPipes);
			if(ifcInf->NumberOfPipes != alt->EndpointCount)
			{
				VIRTUSB_KDPRINT1("*** endpoint count mismatch: i have %lu ***\n", alt->EndpointCount);
				goto Inval;
			}
			for(j = 0; j < alt->EndpointCount; j++)
			{
				PUSBD_PIPE_INFORMATION epInf = &ifcInf->Pipes[j];
				UCHAR                  epAdr = epInf->EndpointAddress;
				PPIPE_CONTEXT          ep = &alt->Endpoint[j];
				if(!ep->Descriptor)
				{
					VIRTUSB_KDPRINT("*** descriptor is missing ***\n");
					goto Inval;
				}
				VIRTUSB_KDPRINT2("*** endpoint index is %lu; handle&context are 0x%p ***\n", j, ep);
				epInf->PipeHandle = ep;
				if(!(epInf->PipeFlags & USBD_PF_CHANGE_MAX_PACKET))
				{
					epInf->MaximumPacketSize = ep->Descriptor->wMaxPacketSize;
				} // else { TODO: should we store it somewhere? }
				epInf->EndpointAddress = ep->Descriptor->bEndpointAddress;
				epInf->Interval = ep->Descriptor->bInterval;
				epInf->PipeType = ep->Descriptor->bmAttributes & USB_ENDPOINT_TYPE_MASK; // (they match)
				if(!epInf->MaximumTransferSize)
				{
					epInf->MaximumTransferSize = 4096;
				}
				VIRTUSB_KDPRINT3("ifcInf[%lu]->epInf[%lu]->MaximumPacketSize: %hu\n", i, j, epInf->MaximumPacketSize);
				VIRTUSB_KDPRINT3("ifcInf[%lu]->epInf[%lu]->EndpointAddress: 0x%02hx\n", i, j, (USHORT)epAdr);
				VIRTUSB_KDPRINT3("ifcInf[%lu]->epInf[%lu]->Interval: %hu\n", i, j, (USHORT)epInf->Interval);
				VIRTUSB_KDPRINT3("ifcInf[%lu]->epInf[%lu]->PipeType: %lu\n", i, j, (ULONG)epInf->PipeType);
				VIRTUSB_KDPRINT3("ifcInf[%lu]->epInf[%lu]->PipeHandle: 0x%p\n", i, j, epInf->PipeHandle);
				VIRTUSB_KDPRINT3("ifcInf[%lu]->epInf[%lu]->MaximumTransferSize: %lu\n", i, j, epInf->MaximumTransferSize);
				VIRTUSB_KDPRINT3("ifcInf[%lu]->epInf[%lu]->PipeFlags: 0x%08lx\n", i, j, epInf->PipeFlags);
			}

			subUrb++;
			sp = (PVIRTUSB_SETUP_PACKET)subUrb->UrbControlTransfer.SetupPacket;
			subUrb->UrbHeader.Length                        = sizeof(URB);
			subUrb->UrbHeader.Function                      = URB_FUNCTION_CONTROL_TRANSFER;
			subUrb->UrbHeader.Status                        = USBD_STATUS_PENDING;
			subUrb->UrbHeader.UsbdDeviceHandle              = dev;
			subUrb->UrbControlTransfer.PipeHandle           = Pipe;
			subUrb->UrbControlTransfer.TransferFlags        = USBD_TRANSFER_DIRECTION_OUT |
			                                                  USBD_SHORT_TRANSFER_OK |
			                                                  USBD_DEFAULT_PIPE_TRANSFER;
			subUrb->UrbControlTransfer.TransferBufferLength = 0;
			subUrb->UrbControlTransfer.TransferBuffer       = NULL;
			sp->bmRequestType                               = 0x01;
			sp->bRequest                                    = USB_REQUEST_SET_INTERFACE;
			sp->wValue                                      = altValue;
			sp->wIndex                                      = ifcValue;
			sp->wLength                                     = 0;
			wu->SubUrb[i + 1].Urb               = subUrb;
			wu->SubUrb[i + 1].CompletionHandler = &VirtUsb_SelectConfigurationWorkUnitSubUrbCompletionHandler;
			wu->SubUrb[i + 1].Context[0]        = (PVOID)(ULONG_PTR)altIndex;
			wu->SubUrb[i + 1].Context[1]        = ifc;

			ifcInf = (PUSBD_INTERFACE_INFORMATION)((PUCHAR)ifcInf + ifcInf->Length);
		}
	}

	ASSERT(subUrb == &subUrbArr[numSubUrbs - 1]);

	*WorkUnit = wu;
	return STATUS_SUCCESS;

Stall:
	if(wu)
	{
#if DBG
		wu->Completed = TRUE;
#endif
		VirtUsb_FreeWorkUnit(wu);
	}
	ExFreePoolWithTag(subUrbArr, VIRTUSB_WORK_POOL_TAG);
	urb->UrbHeader.Status = USBD_STATUS_STALL_PID;
	return STATUS_UNSUCCESSFUL;

Inval:
	if(wu)
	{
#if DBG
		wu->Completed = TRUE;
#endif
		VirtUsb_FreeWorkUnit(wu);
	}
	ExFreePoolWithTag(subUrbArr, VIRTUSB_WORK_POOL_TAG);
	urb->UrbHeader.Status = USBD_STATUS_INVALID_PARAMETER;
	return STATUS_INVALID_PARAMETER;
}

LONG
VirtUsb_SelectConfigurationWorkUnitSubUrbCompletionHandler(
	IN OUT PWORK        WorkUnit,
	IN OUT PWORK_SUBURB SubUrb,
	IN     USBD_STATUS  UsbdStatus
	)
{
	LONG next;
	SubUrb->Urb->UrbHeader.Status = UsbdStatus;
	if(WorkUnit->CurrentSubUrb == 0) // SET_CONFIGURATION
	{
		PUSBDEV_CONTEXT dev = WorkUnit->Pipe->Device;
		ULONG confIndex     = (ULONG)(ULONG_PTR)SubUrb->Context[0];
		if(!USBD_SUCCESS(UsbdStatus))
			return -1;
		ASSERT(dev);
		ASSERT(confIndex < dev->ConfigurationCount || confIndex == MAXULONG);
		if(confIndex != MAXULONG)
		{
			// configure device
			dev->ActiveConfiguration = confIndex;
			KeMemoryBarrier(); // enforce setting index first
			dev->State = Configured;
		}
		else
		{
			// unconfigure the device
			dev->State = Addressed;
			KeMemoryBarrier(); // enforce setting state first
			dev->ActiveConfiguration = MAXULONG;
		}
	}
	else // SET_INTERFACE
	{
		ULONG        altIndex = (ULONG)(ULONG_PTR)SubUrb->Context[0];
		PIFC_CONTEXT ifc      = SubUrb->Context[1];
		ASSERT(ifc);
		ASSERT(altIndex < ifc->AltSettingCount);
		if(!USBD_SUCCESS(UsbdStatus))
		{
			if(ifc->AltSettingCount == 1 && UsbdStatus == USBD_STATUS_STALL_PID)
			{
				// usb spec 2.0 section 9.4.10:
				//   If a device only supports a default setting for the
				//   specified interface, then a STALL may be returned in
				//   the Status stage of the request.
				SubUrb->Urb->UrbHeader.Status = USBD_STATUS_SUCCESS;
			}
			else
			{
				return -1;
			}
		}
		// select alternate setting
		ifc->ActiveAltSetting = altIndex;
	}
	next = WorkUnit->CurrentSubUrb + 1;
	if((ULONG)next >= WorkUnit->SubUrbCount) next = -1;
	return next;
}

NTSTATUS
VirtUsb_SelectConfigurationWorkUnitCompletionHandler(
	IN OUT PWORK    WorkUnit,
	IN     NTSTATUS Status,
	IN     BOOLEAN  MustCancel
	)
{
	PURB urb = URB_FROM_IRP(WorkUnit->Irp);
	ExFreePoolWithTag(WorkUnit->SubUrb[0].Urb, VIRTUSB_WORK_POOL_TAG);
	if(MustCancel)
	{
		urb->UrbHeader.Status = USBD_STATUS_CANCELED;
		return Status;
	}
	if(urb->UrbHeader.Status == USBD_STATUS_PENDING)
	{
		urb->UrbHeader.Status = USBD_STATUS_INTERNAL_HC_ERROR;
		if(NT_SUCCESS(Status)) Status = STATUS_UNSUCCESSFUL;
	}
	return Status;
}

NTSTATUS
VirtUsb_AllocateSelectInterfaceWorkUnit(
	OUT    PWORK            *WorkUnit,
	IN     PPDO_DEVICE_DATA PdoData,
	IN OUT PIRP             Irp,
	IN     PPIPE_CONTEXT    Pipe
	)
{
	struct _URB_SELECT_INTERFACE *selectIfc;
	PUSBD_INTERFACE_INFORMATION  ifcInf;
	PVIRTUSB_SETUP_PACKET        sp;
	PURB                         urb, subUrb;
	PURB                         subUrbArr = NULL;
	PUSBDEV_CONTEXT              dev;
	PWORK                        wu = NULL;
	ULONG                        i;
	ULONG                        confIndex = MAXULONG;
	UCHAR                        ifcValue, altValue;
	ULONG                        ifcIndex, altIndex;
	PCONF_CONTEXT                conf = NULL;
	PIFC_CONTEXT                 ifc;
	PAIFC_CONTEXT                alt;

	ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);

	VIRTUSB_KDPRINT("in VirtUsb_AllocateSelectInterfaceWorkUnit\n");

	ASSERT(WorkUnit);
	ASSERT(PdoData);
	ASSERT(Irp);
	ASSERT(Pipe);
	urb = URB_FROM_IRP(Irp);
	ASSERT(urb);
	dev = Pipe->Device;
	ASSERT(dev);

	VIRTUSB_KDPRINT("URB_FUNCTION_SELECT_INTERFACE\n");
	VIRTUSB_KDPRINT1("--> Hdr.Length:              %lu\n", urb->UrbHeader.Length);
	selectIfc = &urb->UrbSelectInterface;
	VIRTUSB_KDPRINT1("--> ConfigurationHandle:     0x%p\n", selectIfc->ConfigurationHandle);

	if(dev->State >= Configured)
	{
		confIndex = dev->ActiveConfiguration;
		conf = &dev->Configuration[confIndex];
	}

	if(!conf)
	{
		VIRTUSB_KDPRINT("Device is not configured\n");
		// TODO: find out if this is the correct behaviour
		goto Stall;
	}

	if(conf != selectIfc->ConfigurationHandle)
	{
		VIRTUSB_KDPRINT("ConfigurationHandle doesn't match the currently active configuration of the device\n");
		VIRTUSB_KDPRINT("--> not sure how to proceed in this case...\n");
		// TODO: find out if this is the correct behaviour
		goto Stall;
	}

	subUrbArr = ExAllocatePoolWithTag(NonPagedPool,
	                                  sizeof(URB),
	                                  VIRTUSB_WORK_POOL_TAG);
	if(!subUrbArr)
	{
		urb->UrbHeader.Status = USBD_STATUS_INSUFFICIENT_RESOURCES;
		return STATUS_INSUFFICIENT_RESOURCES;
	}
	RtlZeroMemory(subUrbArr, sizeof(URB));

	wu = VirtUsb_AllocateMultipleUrbsWorkUnit(PdoData, Irp, Pipe, 1);
	if(!wu)
	{
		ExFreePoolWithTag(subUrbArr, VIRTUSB_WORK_POOL_TAG);
		urb->UrbHeader.Status = USBD_STATUS_INSUFFICIENT_RESOURCES;
		return STATUS_INSUFFICIENT_RESOURCES;
	}
	wu->CompletionHandler = &VirtUsb_SelectInterfaceWorkUnitCompletionHandler;

	ifcInf = &selectIfc->Interface;
	ifcValue = ifcInf->InterfaceNumber;
	altValue = ifcInf->AlternateSetting;
	// Search for the interface
	for(ifcIndex = 0; ifcIndex < conf->InterfaceCount; ifcIndex++)
	{
		ifc = &conf->Interface[ifcIndex];
		if(ifc->Descriptor)
		{
			if(ifc->Descriptor->bInterfaceNumber == ifcValue)
			{
				goto IfcFound;
			}
		}
	}
	VIRTUSB_KDPRINT("*** ifc not found ***\n");
	goto Stall; // (if not found)
IfcFound:
	VIRTUSB_KDPRINT2("*** ifc index is %lu; context is 0x%p ***\n", ifcIndex, ifc);
	// Search for the alternate setting
	for(altIndex = 0; altIndex < ifc->AltSettingCount; altIndex++)
	{
		alt = &ifc->AltSetting[altIndex];
		if(alt->Descriptor)
		{
			if(alt->Descriptor->bAlternateSetting == altValue)
			{
				goto AltFound;
			}
		}
	}
	VIRTUSB_KDPRINT("*** alternate setting not found ***\n");
	goto Stall; // (if not found)
AltFound:
	VIRTUSB_KDPRINT2("*** alternate setting index is %lu; handle&context are 0x%p ***\n", altIndex, alt);
	ifcInf->InterfaceHandle = alt;
	// TODO: are we supposed to set these fields here? we already did it for the SELECT_CONFIGURATION.
	ifcInf->Class = alt->Descriptor->bInterfaceClass;
	ifcInf->SubClass = alt->Descriptor->bInterfaceSubClass;
	ifcInf->Protocol = alt->Descriptor->bInterfaceProtocol;
	ifcInf->Reserved = 0;
	VIRTUSB_KDPRINT1("Length:           %hu\n", ifcInf->Length);
	VIRTUSB_KDPRINT1("InterfaceNumber:  %hu\n", (USHORT)ifcValue);
	VIRTUSB_KDPRINT1("AlternateSetting: %hu\n", (USHORT)altValue);
	VIRTUSB_KDPRINT1("Class:            0x%02hx\n", (USHORT)ifcInf->Class);
	VIRTUSB_KDPRINT1("SubClass:         0x%02hx\n", (USHORT)ifcInf->SubClass);
	VIRTUSB_KDPRINT1("Protocol:         0x%02hx\n", (USHORT)ifcInf->Protocol);
	VIRTUSB_KDPRINT1("Reserved:         0x%02hx\n", (USHORT)ifcInf->Reserved);
	VIRTUSB_KDPRINT1("InterfaceHandle:  0x%p\n", ifcInf->InterfaceHandle);
	VIRTUSB_KDPRINT1("NumberOfPipes:    %lu\n", ifcInf->NumberOfPipes);
	if(ifcInf->NumberOfPipes != alt->EndpointCount)
	{
		VIRTUSB_KDPRINT1("*** endpoint count mismatch: i have %lu ***\n", alt->EndpointCount);
		goto Inval;
	}
	for(i = 0; i < alt->EndpointCount; i++)
	{
		PUSBD_PIPE_INFORMATION epInf = &ifcInf->Pipes[i];
		UCHAR                  epAdr = epInf->EndpointAddress;
		PPIPE_CONTEXT          ep = &alt->Endpoint[i];
		if(!ep->Descriptor)
		{
			VIRTUSB_KDPRINT("*** descriptor is missing ***\n");
			goto Inval;
		}
		VIRTUSB_KDPRINT2("*** endpoint index is %lu; handle&context are 0x%p ***\n", i, ep);
		epInf->PipeHandle = ep;
		if(!(epInf->PipeFlags & USBD_PF_CHANGE_MAX_PACKET))
		{
			epInf->MaximumPacketSize = ep->Descriptor->wMaxPacketSize;
		} // else { TODO: should we store it somewhere? }
		epInf->EndpointAddress = ep->Descriptor->bEndpointAddress;
		epInf->Interval = ep->Descriptor->bInterval;
		epInf->PipeType = ep->Descriptor->bmAttributes & USB_ENDPOINT_TYPE_MASK; // (they match)
		if(!epInf->MaximumTransferSize)
		{
			epInf->MaximumTransferSize = 4096;
		}
		VIRTUSB_KDPRINT2("epInf[%lu]->MaximumPacketSize: %hu\n", i, epInf->MaximumPacketSize);
		VIRTUSB_KDPRINT2("epInf[%lu]->EndpointAddress: 0x%02hx\n", i, (USHORT)epAdr);
		VIRTUSB_KDPRINT2("epInf[%lu]->Interval: %hu\n", i, (USHORT)epInf->Interval);
		VIRTUSB_KDPRINT2("epInf[%lu]->PipeType: %lu\n", i, (ULONG)epInf->PipeType);
		VIRTUSB_KDPRINT2("epInf[%lu]->PipeHandle: 0x%p\n", i, epInf->PipeHandle);
		VIRTUSB_KDPRINT2("epInf[%lu]->MaximumTransferSize: %lu\n", i, epInf->MaximumTransferSize);
		VIRTUSB_KDPRINT2("epInf[%lu]->PipeFlags: 0x%08lx\n", i, epInf->PipeFlags);
	}

	subUrb = subUrbArr;
	sp = (PVIRTUSB_SETUP_PACKET)subUrb->UrbControlTransfer.SetupPacket;
	subUrb->UrbHeader.Length                        = sizeof(URB);
	subUrb->UrbHeader.Function                      = URB_FUNCTION_CONTROL_TRANSFER;
	subUrb->UrbHeader.Status                        = USBD_STATUS_PENDING;
	subUrb->UrbHeader.UsbdDeviceHandle              = dev;
	subUrb->UrbControlTransfer.PipeHandle           = Pipe;
	subUrb->UrbControlTransfer.TransferFlags        = USBD_TRANSFER_DIRECTION_OUT |
	                                                  USBD_SHORT_TRANSFER_OK |
	                                                  USBD_DEFAULT_PIPE_TRANSFER;
	subUrb->UrbControlTransfer.TransferBufferLength = 0;
	subUrb->UrbControlTransfer.TransferBuffer       = NULL;
	sp->bmRequestType                               = 0x01;
	sp->bRequest                                    = USB_REQUEST_SET_INTERFACE;
	sp->wValue                                      = altValue;
	sp->wIndex                                      = ifcValue;
	sp->wLength                                     = 0;
	wu->SubUrb[0].Urb               = subUrb;
	wu->SubUrb[0].CompletionHandler = &VirtUsb_SelectInterfaceWorkUnitSubUrbCompletionHandler;
	wu->SubUrb[0].Context[0]        = (PVOID)(ULONG_PTR)altIndex;
	wu->SubUrb[0].Context[1]        = ifc;

	*WorkUnit = wu;
	return STATUS_SUCCESS;

Stall:
	if(wu)
	{
#if DBG
		wu->Completed = TRUE;
#endif
		VirtUsb_FreeWorkUnit(wu);
	}
	ExFreePoolWithTag(subUrbArr, VIRTUSB_WORK_POOL_TAG);
	urb->UrbHeader.Status = USBD_STATUS_STALL_PID;
	return STATUS_UNSUCCESSFUL;

Inval:
	if(wu)
	{
#if DBG
		wu->Completed = TRUE;
#endif
		VirtUsb_FreeWorkUnit(wu);
	}
	ExFreePoolWithTag(subUrbArr, VIRTUSB_WORK_POOL_TAG);
	urb->UrbHeader.Status = USBD_STATUS_INVALID_PARAMETER;
	return STATUS_INVALID_PARAMETER;
}

LONG
VirtUsb_SelectInterfaceWorkUnitSubUrbCompletionHandler(
	IN OUT PWORK        WorkUnit,
	IN OUT PWORK_SUBURB SubUrb,
	IN     USBD_STATUS  UsbdStatus
	)
{
	LONG         next;
	ULONG        altIndex = (ULONG)(ULONG_PTR)SubUrb->Context[0];
	PIFC_CONTEXT ifc      = SubUrb->Context[1];
	SubUrb->Urb->UrbHeader.Status = UsbdStatus;

	ASSERT(ifc);
	ASSERT(altIndex < ifc->AltSettingCount);
	if(!USBD_SUCCESS(UsbdStatus))
	{
		if(ifc->AltSettingCount == 1 && UsbdStatus == USBD_STATUS_STALL_PID)
		{
			// usb spec 2.0 section 9.4.10:
			//   If a device only supports a default setting for the
			//   specified interface, then a STALL may be returned in
			//   the Status stage of the request.
			SubUrb->Urb->UrbHeader.Status = USBD_STATUS_SUCCESS;
		}
		else
		{
			return -1;
		}
	}
	// select alternate setting
	ifc->ActiveAltSetting = altIndex;

	next = WorkUnit->CurrentSubUrb + 1;
	if((ULONG)next >= WorkUnit->SubUrbCount) next = -1;
	return next;
}

NTSTATUS
VirtUsb_SelectInterfaceWorkUnitCompletionHandler(
	IN OUT PWORK    WorkUnit,
	IN     NTSTATUS Status,
	IN     BOOLEAN  MustCancel
	)
{
	PURB urb = URB_FROM_IRP(WorkUnit->Irp);
	ExFreePoolWithTag(WorkUnit->SubUrb[0].Urb, VIRTUSB_WORK_POOL_TAG);
	if(MustCancel)
	{
		urb->UrbHeader.Status = USBD_STATUS_CANCELED;
		return Status;
	}
	if(urb->UrbHeader.Status == USBD_STATUS_PENDING)
	{
		urb->UrbHeader.Status = USBD_STATUS_INTERNAL_HC_ERROR;
		if(NT_SUCCESS(Status)) Status = STATUS_UNSUCCESSFUL;
	}
	return Status;
}

VOID
VirtUsb_FailUrbIO(
	IN OUT PPDO_DEVICE_DATA PdoData,
	IN     NTSTATUS         FailReason,
	IN     BOOLEAN          FreeIrplessCancelWorkUnits
	)
/*
    Fails all pending irps for <Pdo> with status code <FailReason>.

    IRQL = PASSIVE_LEVEL
*/
{
	KIRQL          irql, cancelIrql;
	PIRP           irp;
	PLIST_ENTRY    entry, nextEntry;
	LIST_ENTRY     failUrbList, failIrpList;
	PDRIVER_CANCEL cancelRoutine;
	PWORK          wu;

	ASSERT(KeGetCurrentIrql() < DISPATCH_LEVEL);

	VIRTUSB_KDPRINT("in VirtUsb_FailUrbIO\n");

	ASSERT(PdoData);
	ASSERT(!NT_SUCCESS(FailReason));

	InitializeListHead(&failUrbList);
	InitializeListHead(&failIrpList);

	KeAcquireSpinLock(&PdoData->Lock, &irql);

	VIRTUSB_KDPRINT1("search pend list 0x%p\n", &PdoData->PendingWorkList);
	while(!IsListEmpty(&PdoData->PendingWorkList))
	{
		entry = RemoveHeadList(&PdoData->PendingWorkList);
		wu = CONTAINING_RECORD(entry, WORK, ListEntry);
		irp = wu->Irp;
		ASSERT(irp);
		IoAcquireCancelSpinLock(&cancelIrql);
		cancelRoutine = IoSetCancelRoutine(irp, NULL);
		if(irp->Cancel && !cancelRoutine)
		{
			IoReleaseCancelSpinLock(cancelIrql);
			InitializeListHead(entry);
			VirtUsb_CompleteWorkUnit(wu, FailReason);
			wu->CancelRace = TRUE;
			// Cancel routine will complete irp and free work unit
		}
		else
		{
			IoReleaseCancelSpinLock(cancelIrql);
			VirtUsb_CompleteWorkUnit(wu, FailReason);
			// In pending list, we can trash both, work unit and irp
			InsertTailList(&failUrbList, entry);
			VIRTUSB_KDPRINT1("insert wu 0x%p from pend\n", wu);
		}
	}
	VIRTUSB_KDPRINT1("search cancel list 0x%p\n", &PdoData->CancelWorkList);
	for(entry = PdoData->CancelWorkList.Flink, nextEntry = entry->Flink;
	    entry != &PdoData->CancelWorkList;
	    entry = nextEntry, nextEntry = entry->Flink)
	{
		wu = CONTAINING_RECORD(entry, WORK, ListEntry);
		irp = wu->Irp;
		if(irp)
		{
			IoAcquireCancelSpinLock(&cancelIrql);
			cancelRoutine = IoSetCancelRoutine(irp, NULL);
			if(irp->Cancel && !cancelRoutine)
			{
				IoReleaseCancelSpinLock(cancelIrql);
				if(FreeIrplessCancelWorkUnits)
				{
					RemoveEntryList(entry);
					InitializeListHead(entry);
					VirtUsb_CompleteWorkUnit(wu, FailReason);
					ASSERT(wu->CancelEvent);
					KeSetEvent(wu->CancelEvent, IO_NO_INCREMENT, FALSE);
					// Cancel routine will complete irp and free work unit
				}
				else
				{
					// We can't 'steal' the irp while cancel routine is running
					// and we won't signal the cancel routine, because we won't
					// free the work unit
				}
			}
			else
			{
				IoReleaseCancelSpinLock(cancelIrql);
				if(FreeIrplessCancelWorkUnits)
				{
					VirtUsb_CompleteWorkUnit(wu, FailReason);
					RemoveEntryList(entry);
					InsertTailList(&failUrbList, entry);
					VIRTUSB_KDPRINT1("insert wu 0x%p from cancel\n", wu);
				}
				else
				{
					// Keep the work unit but not the irp
					InsertTailList(&failIrpList, &irp->Tail.Overlay.ListEntry);
					VirtUsb_HardCancelWorkUnit(wu);
					irp->IoStatus.Status = FailReason;
				}
			}
		}
		else if(FreeIrplessCancelWorkUnits)
		{
			RemoveEntryList(entry);
			InsertTailList(&failUrbList, entry);
			VIRTUSB_KDPRINT1("insert wu 0x%p from cancel w/o irp\n", wu);
		}
	}
	VIRTUSB_KDPRINT1("search fetched list 0x%p\n", &PdoData->FetchedWorkList);
	while(!IsListEmpty(&PdoData->FetchedWorkList))
	{
		entry = RemoveHeadList(&PdoData->FetchedWorkList);
		wu = CONTAINING_RECORD(entry, WORK, ListEntry);
		irp = wu->Irp;
		ASSERT(irp);
		IoAcquireCancelSpinLock(&cancelIrql);
		cancelRoutine = IoSetCancelRoutine(irp, NULL);
		if(irp->Cancel && !cancelRoutine)
		{
			IoReleaseCancelSpinLock(cancelIrql);
			InitializeListHead(entry);
			VirtUsb_CompleteWorkUnit(wu, FailReason);
			irp->IoStatus.Information = 0;
			wu->CancelRace = TRUE;
			// Cancel routine will complete irp and free work unit
		}
		else
		{
			IoReleaseCancelSpinLock(cancelIrql);
			if(FreeIrplessCancelWorkUnits)
			{
				VirtUsb_CompleteWorkUnit(wu, FailReason);
				InsertTailList(&failUrbList, entry);
				VIRTUSB_KDPRINT1("insert wu 0x%p from fetching\n", wu);
			}
			else
			{
				// Keep the work unit but not the irp
				InsertTailList(&failIrpList, &irp->Tail.Overlay.ListEntry);
				VirtUsb_HardCancelWorkUnit(wu);
				irp->IoStatus.Status = FailReason;
				InsertTailList(&PdoData->CancelWorkList, entry);
			}
		}
	}
	VIRTUSB_KDPRINT1("search canceling list 0x%p\n", &PdoData->CancelingWorkList);
	for(entry = PdoData->CancelingWorkList.Flink, nextEntry = entry->Flink;
	    entry != &PdoData->CancelingWorkList;
	    entry = nextEntry, nextEntry = entry->Flink)
	{
		wu = CONTAINING_RECORD(entry, WORK, ListEntry);
		irp = wu->Irp;
		if(irp)
		{
			IoAcquireCancelSpinLock(&cancelIrql);
			cancelRoutine = IoSetCancelRoutine(irp, NULL);
			if(irp->Cancel && !cancelRoutine)
			{
				IoReleaseCancelSpinLock(cancelIrql);
				if(FreeIrplessCancelWorkUnits)
				{
					RemoveEntryList(entry);
					InitializeListHead(entry);
					VirtUsb_CompleteWorkUnit(wu, FailReason);
					ASSERT(wu->CancelEvent);
					KeSetEvent(wu->CancelEvent, IO_NO_INCREMENT, FALSE);
					// Cancel routine will complete irp and free work unit
				}
				else
				{
					// We can't 'steal' the irp while cancel routine is running
					// and we won't signal the cancel routine, because we won't
					// free the work unit
				}
			}
			else
			{
				IoReleaseCancelSpinLock(cancelIrql);
				if(FreeIrplessCancelWorkUnits)
				{
					VirtUsb_CompleteWorkUnit(wu, FailReason);
					RemoveEntryList(entry);
					InsertTailList(&failUrbList, entry);
					VIRTUSB_KDPRINT1("insert wu 0x%p from canceling\n", wu);
				}
				else
				{
					// Keep the work unit but not the irp
					InsertTailList(&failIrpList, &irp->Tail.Overlay.ListEntry);
					VirtUsb_HardCancelWorkUnit(wu);
					irp->IoStatus.Status = FailReason;
				}
			}
		}
		else if(FreeIrplessCancelWorkUnits)
		{
			RemoveEntryList(entry);
			InsertTailList(&failUrbList, entry);
			VIRTUSB_KDPRINT1("insert wu 0x%p from canceling w/o irp\n", wu);
		}
	}

	VIRTUSB_KDPRINT1("search root hub list 0x%p\n", &PdoData->RootHubRequests);
	while(!IsListEmpty(&PdoData->RootHubRequests))
	{
		entry = RemoveHeadList(&PdoData->RootHubRequests);
		irp = CONTAINING_RECORD(entry, IRP, Tail.Overlay.ListEntry);
		IoAcquireCancelSpinLock(&cancelIrql);
		cancelRoutine = IoSetCancelRoutine(irp, NULL);
		if(irp->Cancel && !cancelRoutine)
		{
			IoReleaseCancelSpinLock(cancelIrql);
			InitializeListHead(entry);
		}
		else
		{
			ASSERT(((PURB)URB_FROM_IRP(irp))->UrbHeader.UsbdDeviceHandle == PdoData->RootHub);
			VirtUsb_DereferenceUsbDevice((PUSBDEV_CONTEXT)PdoData->RootHub, irp, TRUE);
			IoReleaseCancelSpinLock(cancelIrql);
			InsertTailList(&failIrpList, entry);
		}
	}

	KeReleaseSpinLock(&PdoData->Lock, irql);

	for(entry = failUrbList.Flink, nextEntry = entry->Flink;
	    entry != &failUrbList;
	    entry = nextEntry, nextEntry = entry->Flink)
	{
		wu = CONTAINING_RECORD(entry, WORK, ListEntry);
		irp = wu->Irp;
		VirtUsb_DereferenceUsbDevice(wu->Pipe->Device, irp, FALSE);
		VirtUsb_FreeWorkUnit(wu);
		if(irp)
		{
			IoCompleteRequest(irp, IO_NO_INCREMENT);
			IoReleaseRemoveLock(&PdoData->RemoveLock, irp);
		}
		VIRTUSB_KDPRINT1("wu 0x%p done\n", wu);
	}

	for(entry = failIrpList.Flink, nextEntry = entry->Flink;
	    entry != &failIrpList;
	    entry = nextEntry, nextEntry = entry->Flink)
	{
		irp = CONTAINING_RECORD(entry, IRP, Tail.Overlay.ListEntry);
		irp->IoStatus.Status = FailReason;
		irp->IoStatus.Information = 0;
		IoCompleteRequest(irp, IO_NO_INCREMENT);
		IoReleaseRemoveLock(&PdoData->RemoveLock, irp);
	}
}
