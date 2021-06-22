#include "vusbvhci.h"
#include "trace.h"
#include "busif.tmh"
#include "busif.h"

#ifndef TARGETING_Win2K

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, VUsbVhci_BUSIF_CreateUsbDevice)
#pragma alloc_text(PAGE, VUsbVhci_BUSIF_InitializeUsbDevice)
#pragma alloc_text(PAGE, VUsbVhci_BUSIF_RemoveUsbDevice)
#pragma alloc_text(PAGE, VUsbVhci_BUSIF_GetUsbDescriptors)
#pragma alloc_text(PAGE, VUsbVhci_BUSIF_RestoreUsbDevice)
#pragma alloc_text(PAGE, VUsbVhci_BUSIF_GetUsbDeviceHackFlags)
#pragma alloc_text(PAGE, VUsbVhci_BUSIF_GetPortHackFlags)
#pragma alloc_text(PAGE, VUsbVhci_BUSIF_QueryDeviceInformation)
#pragma alloc_text(PAGE, VUsbVhci_BUSIF_GetControllerInformation)
#pragma alloc_text(PAGE, VUsbVhci_BUSIF_ControllerSelectiveSuspend)
#pragma alloc_text(PAGE, VUsbVhci_BUSIF_GetExtendedHubInformation)
#pragma alloc_text(PAGE, VUsbVhci_BUSIF_GetRootHubSymbolicName)
#pragma alloc_text(PAGE, VUsbVhci_BUSIF_GetDeviceBusContext)
#pragma alloc_text(PAGE, VUsbVhci_BUSIF_Initialize20Hub)
#pragma alloc_text(PAGE, VUsbVhci_BUSIF_RootHubInitNotification)
#pragma alloc_text(PAGE, VUsbVhci_BUSIF_FlushTransfers)
#pragma alloc_text(PAGE, VUsbVhci_BUSIF_SetDeviceHandleData)
#endif // ALLOC_PRAGMA

VOID
VUsbVhci_InterfaceReference(
	_In_ PVOID Context
	)
{
	PPDO_DEVICE_DATA pdoData;
	PUSBHUB_CONTEXT  rhub = Context;
	ULONG            res;
	ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);
	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, ">%!FUNC!");
	ASSERT(rhub);
	pdoData = ((PDEVICE_OBJECT)rhub->HcdContext)->DeviceExtension;
	ASSERT(pdoData);
	res = InterlockedIncrement((PLONG)&pdoData->InterfaceRefCount);
	ASSERT(res);
#if 0 // Stupid Microsoft drivers do not dereference the interface
	if(res == 1)
	{
		if(!NT_SUCCESS(IoAcquireRemoveLock(&pdoData->RemoveLock, pdoData)))
		{
			// We only acquire the remove lock when the ref count goes from 0 to 1.
			// This only happens if this routine is called from PDO_QueryInterface,
			// which is called somewhere in the PnP dispatcher routine, which has already
			// acquired the remove lock successfully. We don't get more than one PnP Irp
			// at the same time, so the device could not have been removed since the lock was
			// acquired in the PnP dispatcher. Therefore it must not fail this time.
			ASSERT(FALSE);
		}
	}
#endif
}

VOID
VUsbVhci_InterfaceDereference(
	_In_ PVOID Context
	)
{
	PPDO_DEVICE_DATA pdoData;
	PUSBDEV_CONTEXT  rhub = Context;
	ULONG            res;
	ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);
	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, ">%!FUNC!");
	ASSERT(rhub);
	pdoData = ((PDEVICE_OBJECT)rhub->HcdContext)->DeviceExtension;
	ASSERT(pdoData);
	res = InterlockedDecrement((PLONG)&pdoData->InterfaceRefCount);
	ASSERT((LONG)res >= 0);
#if 0 // Stupid Microsoft drivers do not dereference the interface
	if(!res)
	{
		// If this was the last reference, then release the remove lock
		IoReleaseRemoveLock(&pdoData->RemoveLock, pdoData);
	}
#endif
}

NTSTATUS
USB_BUSIFFN
VUsbVhci_BUSIF_CreateUsbDevice(
	_In_    PVOID              BusContext,
	_Inout_ PUSB_DEVICE_HANDLE *DeviceHandle,
	_In_    PUSB_DEVICE_HANDLE HubDeviceHandle,
	_In_    USHORT             PortStatus,
	_In_    USHORT             PortNumber
	)
/*
    Documentation from http://www.osronline.com/:

    The CreateUsbDevice routine indicates to the port driver that
    a new USB device has arrived.

    This routine sets up the internal data structures that the port
    driver uses to keep track of the device and assigns the device
    an address. The hub driver calls this routine for each new device
    on the USB bus.

    Return Value
      STATUS_SUCCESS
        The call completed successfully.
      STATUS_DEVICE_NOT_CONNECTED
        The hub device handle is not valid.
      STATUS_INSUFFICIENT_RESOURCES
        Internal data structures could not be allocated due to a lack of resources.
      STATUS_DEVICE_DATA_ERROR
        Parent hub driver must disable the port.

    IRQL = PASSIVE_LEVEL
*/
{
	NTSTATUS         status;
	PUSBHUB_CONTEXT  rhub = BusContext;
	PFDO_DEVICE_DATA fdoData;
	PUSBDEV_CONTEXT  dev;
	PUSBHUB_CONTEXT  hub = NULL;

	PAGED_CODE();

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, ">%!FUNC!");

	ASSERT(rhub);
	fdoData = rhub->HcdContext2;
	ASSERT(fdoData);

	status = VUsbVhci_ReferenceUsbDeviceByHandle(fdoData, HubDeviceHandle, (PUSBDEV_CONTEXT *)&hub);
	if(!NT_SUCCESS(status))
	{
		return status;
	}
	ASSERT(hub);

	status = VUsbVhci_CreateUsbDevice(fdoData,
	                                  DeviceHandle,
	                                  hub,
	                                  PortStatus,
	                                  PortNumber);
	if(NT_SUCCESS(status) && DeviceHandle && *DeviceHandle)
	{
		// we store our fdoData in every device context
		dev = *DeviceHandle;
		dev->HcdContext2 = fdoData;
	}

	VUsbVhci_DereferenceUsbDevice(fdoData, (PUSBDEV_CONTEXT)hub);
	return status;
}

NTSTATUS
USB_BUSIFFN
VUsbVhci_BUSIF_InitializeUsbDevice(
	_In_    PVOID              BusContext,
	_Inout_ PUSB_DEVICE_HANDLE DeviceHandle
	)
/*
    Documentation from http://www.osronline.com/:

    The InitializeUsbDevice routine initializes a new USB device.

    The hub driver calls the InitializeUsbDevice routine to assign a device
    address to a new USB device. This routine scans the list of device
    addresses that are recorded in the parent hub's device extension until
    it finds an unused address and assigns that unused address to the new
    device. There are 128 available addresses (0 - 127), as specified by
    version 1.1 of the Universal Serial Bus specification. The address
    remains valid until the device is removed by a call to the RemoveUsbDevice
    routine, or until the device is reset or the system powered down. On the
    next enumeration, the device might be assigned a different address. For
    more information on how USB addresses are assigned, see the section of
    the Universal Serial Bus Specification that describes the Set Address
    request.

    Return Value
      STATUS_SUCCESS
        The call completed successfully.
      STATUS_DEVICE_DATA_ERROR
        The routine was unable to retrieve information from the
        device that was necessary to complete the call.

    IRQL = PASSIVE_LEVEL
*/
{
	PUSBHUB_CONTEXT  rhub = BusContext;
	PFDO_DEVICE_DATA fdoData;
	PUSBDEV_CONTEXT  dev = NULL;
	NTSTATUS         status;

	PAGED_CODE();

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, ">%!FUNC!");

	ASSERT(rhub);
	fdoData = rhub->HcdContext2;
	ASSERT(fdoData);

	status = VUsbVhci_ReferenceUsbDeviceByHandle(fdoData, DeviceHandle, &dev);
	if(!NT_SUCCESS(status))
	{
		return status;
	}
	ASSERT(dev);

	status = VUsbVhci_InitializeUsbDevice(fdoData, dev);

	VUsbVhci_DereferenceUsbDevice(fdoData, dev);
	return status;
}

NTSTATUS
USB_BUSIFFN
VUsbVhci_BUSIF_RemoveUsbDevice(
	_In_    PVOID              BusContext,
	_Inout_ PUSB_DEVICE_HANDLE DeviceHandle,
	_In_    ULONG              Flags
	)
/*
    Documentation from http://www.osronline.com/:

    The RemoveUsbDevice routine removes a USB device.

    The hub driver calls the RemoveUsbDevice routine for each USB device to be
    removed. This routine frees the handles to specific configurations,
    interfaces, and pipes associated with the device. It also frees the device
    address that was assigned to the device by the InitializeUsbDevice routine.
    If neither of the flags USBD_KEEP_DEVICE_DATA or USBD_MARK_DEVICE_BUSY are
    set in the Flags parameter, then it also frees the device handle.

    Parameters
      Flags
        Contains flag values that indicate how the port driver should
        interpret the call to RemoveUsbDevice.
          Flag                    Meaning
          USBD_KEEP_DEVICE_DATA   Directs the port driver not to free
                                  the device handle, after removing the device.
          USBD_MARK_DEVICE_BUSY   Directs the port driver to stop accepting
                                  requests for the indicated device. The hub
                                  driver calls RemoveUsbDevice with this flag
                                  set when handling an IOCTL_INTERNAL_USB_RESET_PORT
                                  request. The handle remains valid and can be
                                  used to restore the device after the reset.

    Return Value
      STATUS_SUCCESS
        The call completed successfully.
      STATUS_DEVICE_NOT_CONNECTED
        The device handle is bad.

    IRQL = PASSIVE_LEVEL
*/
{
	PUSBHUB_CONTEXT  rhub = BusContext;
	PFDO_DEVICE_DATA fdoData;
	PUSBDEV_CONTEXT  dev = NULL;
	// mask the known flags
	ULONG            flags = Flags & (USBD_KEEP_DEVICE_DATA | USBD_MARK_DEVICE_BUSY);
	NTSTATUS         status;

	PAGED_CODE();

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, ">%!FUNC!");

	ASSERT(rhub);
	fdoData = rhub->HcdContext2;
	ASSERT(fdoData);

	status = VUsbVhci_ReferenceUsbDeviceByHandle(fdoData, DeviceHandle, &dev);
	if(!NT_SUCCESS(status))
	{
		return status;
	}
	ASSERT(dev);

#if DBG
	if(!flags)
	{
		dev->HcdContext = (PVOID)(ULONG_PTR)0xdeadf00d;
		dev->HcdContext2 = (PVOID)(ULONG_PTR)0xdeadf00d;
	}
#endif

	return VUsbVhci_RemoveUsbDevice(fdoData, dev, flags, TRUE);
}

NTSTATUS
USB_BUSIFFN
VUsbVhci_BUSIF_GetUsbDescriptors(
	_In_    PVOID              BusContext,
	_Inout_ PUSB_DEVICE_HANDLE DeviceHandle,
	_Inout_ PUCHAR             DeviceDescriptorBuffer,
	_Inout_ PULONG             DeviceDescriptorBufferLength,
	_Inout_ PUCHAR             ConfigDescriptorBuffer,
	_Inout_ PULONG             ConfigDescriptorBufferLength
	)
/*
    Documentation from http://www.osronline.com/:

    The GetUsbDescriptors routine retrieves the configuration and
    device descriptors for the indicated device.

    Parameters
      DeviceDescriptorBuffer
        Contains a buffer holding the device descriptor data, formatted
        as a USB_DEVICE_DESCRIPTOR structure.
      DeviceDescriptorBufferLength
        Contains, on input, the length of the device descriptor buffer
        that was allocated by the caller. On output, this member indicates
        the number of bytes returned for the actual descriptor.
      ConfigDescriptorBuffer
        Contains a buffer holding the configuration descriptor data, formatted
        as a USB_CONFIGURATION_DESCRIPTOR structure.
      ConfigDescriptorBufferLength
        Contains, on input, the length of the configuration descriptor
        buffer that was allocated by the caller. On output, this member
        indicates the number of bytes returned for the actual descriptor.

    Return Value
      STATUS_SUCCESS
        The call completed successfully.
      STATUS_DEVICE_DATA_ERROR
        A complete descriptor could not be retrieved.

    IRQL = PASSIVE_LEVEL
*/
{
	PUSBHUB_CONTEXT  rhub = BusContext;
	PFDO_DEVICE_DATA fdoData;
	PUSBDEV_CONTEXT  dev = NULL;
	NTSTATUS         status;

	PAGED_CODE();

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, ">%!FUNC!");

	ASSERT(rhub);
	fdoData = rhub->HcdContext2;
	ASSERT(fdoData);

	status = VUsbVhci_ReferenceUsbDeviceByHandle(fdoData, DeviceHandle, &dev);
	if(!NT_SUCCESS(status))
	{
		return status;
	}
	ASSERT(dev);

	if(dev->State != Addressed && dev->State != Configured)
	{
		status = STATUS_DEVICE_DATA_ERROR;
		goto End;
	}

	ASSERT(dev->Descriptor);
	ASSERT(dev->Configuration);

	if(DeviceDescriptorBuffer && DeviceDescriptorBufferLength)
	{
		ULONG l = min(dev->Descriptor->bLength, *DeviceDescriptorBufferLength);
		RtlCopyMemory(DeviceDescriptorBuffer, dev->Descriptor, l);
		*DeviceDescriptorBufferLength = l;
	}

	if(dev->State != Configured)
	{
		if(ConfigDescriptorBuffer && ConfigDescriptorBufferLength)
		{
			*ConfigDescriptorBufferLength = 0;
		}
		status = STATUS_SUCCESS;
		goto End;
	}

	if(ConfigDescriptorBuffer && ConfigDescriptorBufferLength)
	{
		ULONG i = dev->ActiveConfiguration;
		ULONG l = min(dev->Configuration[i].Descriptor->bLength, *ConfigDescriptorBufferLength);
		RtlCopyMemory(ConfigDescriptorBuffer, dev->Configuration[i].Descriptor, l);
		*ConfigDescriptorBufferLength = l;
	}

	status = STATUS_SUCCESS;
End:
	VUsbVhci_DereferenceUsbDevice(fdoData, dev);
	return status;
}

NTSTATUS
USB_BUSIFFN
VUsbVhci_BUSIF_RestoreUsbDevice(
	_In_    PVOID              BusContext,
	_Inout_ PUSB_DEVICE_HANDLE OldDeviceHandle,
	_Inout_ PUSB_DEVICE_HANDLE NewDeviceHandle
	)
/*
    Documentation from http://www.osronline.com/:

    The RestoreUsbDevice routine recreates a USB device using the
    information in the device handle of a device that has been removed.

    The RestoreUsbDevice routine will recreate the device using the
    information supplied in OldDeviceHandle. RestoreUsbDevice returns
    NewDeviceHandle which duplicates the information in OldDeviceHandle,
    provided that the device has the same VID/PID as the device that
    was assigned to OldDeviceHandle. When the routine completes, it
    frees the old device handle.

    Return Value
      STATUS_SUCCESS
        The call completed successfully.
      STATUS_DEVICE_NOT_CONNECTED
        The old device handle was not valid.

    IRQL = PASSIVE_LEVEL
*/
{
	PUSBHUB_CONTEXT  rhub = BusContext;
	PFDO_DEVICE_DATA fdoData;
	PUSBDEV_CONTEXT  dev = NULL;
	NTSTATUS         status;

	PAGED_CODE();

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, ">%!FUNC!");

	ASSERT(rhub);
	fdoData = rhub->HcdContext2;
	ASSERT(fdoData);

	status = VUsbVhci_ReferenceUsbDeviceByHandle(fdoData, OldDeviceHandle, &dev);
	if(!NT_SUCCESS(status))
	{
		return status;
	}
	ASSERT(dev);

	if(OldDeviceHandle == NewDeviceHandle)
	{
		// TODO: wtf does the caller want from us?
		VUsbVhci_DereferenceUsbDevice(fdoData, dev);
		return STATUS_SUCCESS;
	}

	VUsbVhci_DereferenceUsbDevice(fdoData, dev);
	// TODO: implement
	KdBreakPoint();
	return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
USB_BUSIFFN
VUsbVhci_BUSIF_GetUsbDeviceHackFlags(
	_In_    PVOID              BusContext,
	_In_    PUSB_DEVICE_HANDLE DeviceHandle,
	_Inout_ PULONG             HackFlags
	)
/*
    Documentation from http://www.osronline.com/:

    Fetches device specific 'hack' flags from a global refistry key.

    These flags modify the behavior of the hub driver.

    IRQL = PASSIVE_LEVEL
*/
{
	PAGED_CODE();

	UNREFERENCED_PARAMETER(BusContext);
	UNREFERENCED_PARAMETER(DeviceHandle);
	UNREFERENCED_PARAMETER(HackFlags);

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, ">%!FUNC!");

	// TODO: implement
	KdBreakPoint();

	return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
USB_BUSIFFN
VUsbVhci_BUSIF_GetPortHackFlags(
	_In_    PVOID  BusContext,
	_Inout_ PULONG HackFlags
	)
/*
    Documentation from http://www.osronline.com/:

    Fetches global port 'hack' flags from a global refistry key.

    These flags modify the behavior of the hub driver.

    IRQL = PASSIVE_LEVEL
*/
{
	PAGED_CODE();

	UNREFERENCED_PARAMETER(BusContext);
	UNREFERENCED_PARAMETER(HackFlags);

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, ">%!FUNC!");

	// TODO: implement
	KdBreakPoint();

	return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
USB_BUSIFFN
VUsbVhci_BUSIF_QueryDeviceInformation(
	_In_    PVOID              BusContext,
	_In_    PUSB_DEVICE_HANDLE DeviceHandle,
	_Inout_ PVOID              DeviceInformationBuffer,
	_In_    ULONG              DeviceInformationBufferLength,
	_Inout_ PULONG             LengthOfDataReturned
	)
/*
    Documentation from http://www.osronline.com/:

    The QueryDeviceInformation routine retrieves information about a USB device.

    Return Value
      STATUS_SUCCESS
        The call completed successfully.
      STATUS_NOT_SUPPORTED
        The information level requested is not supported. The information level
        is indicated in the InformationLevel member of the
        USB_DEVICE_INFORMATION_0 structure that is passed to the routine in
        DeviceInformationBuffer.
      STATUS_BUFFER_TOO_SMALL
        The buffer size in DeviceInformationBufferLength is smaller than the
        size of the data to be reported.

    IRQL = PASSIVE_LEVEL
*/
{
	PUSBHUB_CONTEXT           rhub = BusContext;
	PFDO_DEVICE_DATA          fdoData;
	PUSBDEV_CONTEXT           dev = NULL;
	PUSB_DEVICE_INFORMATION_0 inf = DeviceInformationBuffer;
	PCONF_CONTEXT             conf;
	PIFC_CONTEXT              ifc;
	ULONG                     len, i, pipeCount;
	NTSTATUS                  status;

	PAGED_CODE();

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, ">%!FUNC!");

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, "--> BusContext:                    0x%p", BusContext);
	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, "--> DeviceHandle:                  0x%p", DeviceHandle);
	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, "--> DeviceInformationBuffer:       0x%p", DeviceInformationBuffer);
	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, "--> DeviceInformationBufferLength: %lu",  DeviceInformationBufferLength);
	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, "--> LengthOfDataReturned:          0x%p", LengthOfDataReturned);

	ASSERT(rhub);
	fdoData = rhub->HcdContext2;
	ASSERT(fdoData);
	ASSERT(rhub->RootHub == rhub);
	ASSERT(rhub->HcdContext);
	ASSERT(rhub->HcdContext2);

	if(LengthOfDataReturned) *LengthOfDataReturned = 0;

	status = VUsbVhci_ReferenceUsbDeviceByHandle(fdoData, DeviceHandle, &dev);
	if(!NT_SUCCESS(status))
	{
		return status;
	}
	ASSERT(dev);

	if(dev->State != Addressed && dev->State != Configured)
	{
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_BUSIF, "device is not addressed or configured");
		status = STATUS_DEVICE_NOT_READY;
		goto End;
	}

	if(DeviceInformationBufferLength < sizeof(USB_LEVEL_INFORMATION))
	{
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_BUSIF, "buffer too small -- even to report required buffer size");
		status = STATUS_BUFFER_TOO_SMALL;
		goto End;
	}

	if(!inf)
	{
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_BUSIF, "no buffer");
		status = STATUS_INVALID_PARAMETER;
		goto End;
	}

	if(inf->InformationLevel != 0)
	{
		TraceEvents(TRACE_LEVEL_WARNING, TRACE_BUSIF, "InformationLevel != 0");
		TraceEvents(TRACE_LEVEL_WARNING, TRACE_BUSIF, "InformationLevel == %lu", inf->InformationLevel);
		status = STATUS_NOT_SUPPORTED;
		goto End;
	}

	len = FIELD_OFFSET(USB_DEVICE_INFORMATION_0, PipeList[0]);
	if(DeviceInformationBufferLength < len)
	{
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_BUSIF, "buffer too small -- even without endpoints");
		goto BufferTooSmall;
	}

	if(dev == (PUSBDEV_CONTEXT)rhub)
	{
		ASSERT(!dev->ParentPort);
		inf->PortNumber = 0;
	}
	else
	{
		ASSERT(dev->ParentPort);
		inf->PortNumber = dev->ParentPort->Index + 1;
	}

	ASSERT(dev->Descriptor);
	RtlCopyMemory(&inf->DeviceDescriptor, dev->Descriptor, sizeof(USB_DEVICE_DESCRIPTOR));
	inf->DeviceDescriptor.bLength = sizeof(USB_DEVICE_DESCRIPTOR);

	// my guess: MBZ=MustBeZero
	inf->ReservedMBZ = 0;

	inf->DeviceAddress = dev->Address;

	// TODO: is this right?
	//       (ReactOS guys set this always to 1)
	if(dev == (PUSBDEV_CONTEXT)rhub)
	{
		inf->HubAddress = 1;
	}
	else
	{
		ASSERT(dev->ParentHub);
		inf->HubAddress = dev->ParentHub->Address;
	}

	inf->DeviceSpeed = dev->Speed;
	inf->DeviceType = dev->Type;

	if(dev->State == Configured)
	{
		conf = dev->Configuration;
		i = dev->ActiveConfiguration;
		ASSERT(conf);
		ASSERT(i < dev->ConfigurationCount);
		conf = &conf[i];
		ASSERT(conf->Descriptor);
		inf->CurrentConfigurationValue = conf->Descriptor->bConfigurationValue;

		ifc = conf->Interface;
		ASSERT(ifc);

		pipeCount = 0;
		for(i = 0; i < conf->InterfaceCount; i++)
		{
			PAIFC_CONTEXT aifc = ifc[i].AltSetting;
			PPIPE_CONTEXT ep;
			ULONG         j = ifc[i].ActiveAltSetting;
			ULONG         k, c;

			ASSERT(j < ifc[i].AltSettingCount);
			ep = aifc[j].Endpoint;
			c = aifc[j].EndpointCount;
			ASSERT(!c || (c && ep));

			len = FIELD_OFFSET(USB_DEVICE_INFORMATION_0, PipeList[pipeCount + c]);
			if(DeviceInformationBufferLength < len)
			{
				TraceEvents(TRACE_LEVEL_ERROR, TRACE_BUSIF, "buffer too small");
				goto BufferTooSmall2;
			}

			for(k = 0; k < c; k++)
			{
				RtlCopyMemory(&inf->PipeList[pipeCount].EndpointDescriptor, ep[k].Descriptor, sizeof(USB_ENDPOINT_DESCRIPTOR));
				inf->PipeList[pipeCount].EndpointDescriptor.bLength = sizeof(USB_ENDPOINT_DESCRIPTOR);
				inf->PipeList[pipeCount].ScheduleOffset = 0;
				pipeCount++;
			}
		}

		ASSERT(pipeCount);
		ASSERT(pipeCount <= 30); // 15*IN + 15*OUT = 30
		inf->NumberOfOpenPipes = pipeCount;
	}
	else // dev->State != Configured
	{
		inf->CurrentConfigurationValue = 0;
		inf->NumberOfOpenPipes = 0;
	}

	inf->ActualLength = len;
	if(LengthOfDataReturned) *LengthOfDataReturned = len;
	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, "--> *LengthOfDataReturned:         %lu", len);
	status = STATUS_SUCCESS;
End:
	VUsbVhci_DereferenceUsbDevice(fdoData, dev);
	return status;

BufferTooSmall:
	if(dev->State != Configured)
	{
		inf->ActualLength = FIELD_OFFSET(USB_DEVICE_INFORMATION_0, PipeList[0]);
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_BUSIF, "required buffer size: %lu", inf->ActualLength);
		status = STATUS_BUFFER_TOO_SMALL;
		goto End;
	}

	conf = dev->Configuration;
	i = dev->ActiveConfiguration;
	ASSERT(conf);
	ASSERT(i < dev->ConfigurationCount);
	conf = &conf[i];
	ifc = conf->Interface;
	ASSERT(ifc);

BufferTooSmall2: // calculate required buffer size
	pipeCount = 0;
	for(i = 0; i < conf->InterfaceCount; i++)
	{
		PAIFC_CONTEXT aifc = ifc[i].AltSetting;
		ULONG         j = ifc[i].ActiveAltSetting;
		ULONG         c;

		ASSERT(j < ifc[i].AltSettingCount);
		c = aifc[j].EndpointCount;

		pipeCount += c;
	}
	ASSERT(pipeCount);
	ASSERT(pipeCount <= 30); // 15*IN + 15*OUT = 30

	inf->ActualLength = FIELD_OFFSET(USB_DEVICE_INFORMATION_0, PipeList[pipeCount]);
	TraceEvents(TRACE_LEVEL_ERROR, TRACE_BUSIF, "required buffer size: %lu", inf->ActualLength);
	status = STATUS_BUFFER_TOO_SMALL;
	goto End;
}

NTSTATUS
USB_BUSIFFN
VUsbVhci_BUSIF_GetControllerInformation(
	_In_    PVOID  BusContext,
	_Inout_ PVOID  ControllerInformationBuffer,
	_In_    ULONG  ControllerInformationBufferLength,
	_Inout_ PULONG LengthOfDataReturned
	)
/*
    Documentation from http://www.osronline.com/:

    The GetControllerInformation routine retrieves information about the host controller.

    Return Value
      STATUS_SUCCESS
        The call completed successfully.
      STATUS_BUFFER_TOO_SMALL
        The size of the buffer at ControllerInformationBuffer was less than
        sizeof(USB_CONTROLLER_INFORMATION_0).
      STATUS_NOT_SUPPORTED
        The value of InformationLevel in USB_CONTROLLER_INFORMATION_0 was not equal to zero.

    IRQL = PASSIVE_LEVEL
*/
{
	//PUSBHUB_CONTEXT               rhub = BusContext;
	PUSB_CONTROLLER_INFORMATION_0 inf = ControllerInformationBuffer;
	ULONG                         len = sizeof(USB_CONTROLLER_INFORMATION_0);

	PAGED_CODE();

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, ">%!FUNC!");

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, "--> BusContext:                        0x%p", BusContext);
	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, "--> ControllerInformationBuffer:       0x%p", ControllerInformationBuffer);
	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, "--> ControllerInformationBufferLength: %lu",  ControllerInformationBufferLength);
	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, "--> LengthOfDataReturned:              0x%p", LengthOfDataReturned);

	//ASSERT(rhub);
	//ASSERT(rhub->RootHub == rhub);
	//ASSERT(rhub->HcdContext);
	//ASSERT(rhub->HcdContext2);

	if(LengthOfDataReturned) *LengthOfDataReturned = 0;

	if(ControllerInformationBufferLength < sizeof(USB_LEVEL_INFORMATION))
	{
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_BUSIF, "buffer too small -- even to report required buffer size");
		return STATUS_BUFFER_TOO_SMALL;
	}

	if(!inf)
	{
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_BUSIF, "no buffer");
		return STATUS_INVALID_PARAMETER;
	}

	if(inf->InformationLevel != 0)
	{
		TraceEvents(TRACE_LEVEL_WARNING, TRACE_BUSIF, "InformationLevel != 0");
		TraceEvents(TRACE_LEVEL_WARNING, TRACE_BUSIF, "InformationLevel == %lu", inf->InformationLevel);
		return STATUS_NOT_SUPPORTED;
	}

	inf->ActualLength = len;
	if(ControllerInformationBufferLength < len)
	{
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_BUSIF, "buffer too small");
		return STATUS_BUFFER_TOO_SMALL;
	}

	inf->SelectiveSuspendEnabled = FALSE;
	inf->IsHighSpeedController = TRUE;

	if(LengthOfDataReturned) *LengthOfDataReturned = len;
	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, "--> *LengthOfDataReturned:             %lu", len);
	return STATUS_SUCCESS;
}

NTSTATUS
USB_BUSIFFN
VUsbVhci_BUSIF_ControllerSelectiveSuspend(
	_In_ PVOID   BusContext,
	_In_ BOOLEAN Enable
	)
/*
    Documentation from http://www.osronline.com/:

    The ControllerSelectiveSuspend routine enables the selective-suspend
    facility on the indicated bus.

    The port driver must enable the selective-suspend facility before the
    bus driver can use it. The bus driver calls this routine to request that
    the port driver enable selective suspend.

    Parameters
      Enable
        Specifies whether selective suspend is enabled on the bus indicated
        by BusContext. A value of TRUE enables selective suspend; FALSE
        disables it.

    Return Value
      STATUS_SUCCESS
        The call completed successfully.
      STATUS_INSUFFICIENT_RESOURCES
        The routine could not change the selective-suspend status of the bus
        in the registry.

    IRQL = PASSIVE_LEVEL
*/
{
	PAGED_CODE();

	UNREFERENCED_PARAMETER(BusContext);
	UNREFERENCED_PARAMETER(Enable);

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, ">%!FUNC!");

	// TODO: implement
	KdBreakPoint();

	return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
USB_BUSIFFN
VUsbVhci_BUSIF_GetExtendedHubInformation(
	_In_    PVOID          BusContext,
	_In_    PDEVICE_OBJECT HubPhysicalDeviceObject,
	_Inout_ PVOID          HubInformationBuffer,
	_In_    ULONG          HubInformationBufferLength,
	_Inout_ PULONG         LengthOfDataReturned
	)
/*
    Documentation from http://www.osronline.com/:

    The GetExtendedHubInformation routine returns information for
    all of the ports on the indicated hub.

    Return Value
      STATUS_SUCCESS
        The call completed successfully.
      STATUS_BUFFER_TOO_SMALL
        The buffer pointed to by HubInformationBuffer is too small to hold
        the data returned.
      STATUS_NOT_SUPPORTED
        The caller failed to set the InformationLevel member of
        USB_EXTHUB_INFORMATION_0 to zero.

    IRQL = PASSIVE_LEVEL
*/
{
	PUSBDEV_CONTEXT           dev = BusContext;
	PUSB_EXTHUB_INFORMATION_0 inf = HubInformationBuffer;
	ULONG                     len, i;

	PAGED_CODE();

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, ">%!FUNC!");

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, "--> BusContext:                 0x%p", BusContext);
	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, "--> HubPhysicalDeviceObject:    0x%p", HubPhysicalDeviceObject);
	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, "--> HubInformationBuffer:       0x%p", HubInformationBuffer);
	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, "--> HubInformationBufferLength: %lu",  HubInformationBufferLength);
	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, "--> LengthOfDataReturned:       0x%p", LengthOfDataReturned);

	ASSERT(dev);
	ASSERT(dev->RootHub);
	ASSERT(dev->RootHub->HcdContext);
	ASSERT(dev->RootHub->HcdContext2);

	if(LengthOfDataReturned) *LengthOfDataReturned = 0;

	if(HubInformationBufferLength < (ULONG)FIELD_OFFSET(USB_EXTHUB_INFORMATION_0, Port[0]))
	{
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_BUSIF, "buffer too small -- even without ports");
		return STATUS_BUFFER_TOO_SMALL;
	}

	if(!inf)
	{
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_BUSIF, "no buffer");
		return STATUS_INVALID_PARAMETER;
	}

	// Windows -- at least XP -- does not initialize
	// this field to zero. MSDN told us crap, again!
	// This is what we would do according to MSDN:
	//if(inf->InformationLevel != 0)
	//{
	//	TraceEvents(TRACE_LEVEL_WARNING, TRACE_BUSIF, "InformationLevel != 0");
	//	TraceEvents(TRACE_LEVEL_WARNING, TRACE_BUSIF, "InformationLevel == %lu", inf->InformationLevel);
	//	return STATUS_NOT_SUPPORTED;
	//}

	if(HubPhysicalDeviceObject != (PDEVICE_OBJECT)dev->RootHub->HcdContext)
	{
		TraceEvents(TRACE_LEVEL_WARNING, TRACE_BUSIF, "GetExtendedHubInformation is not supported for devices other than the root-hub.");
		// TODO: implement for other hubs -- we have to search our device tree for the pdo stored in HcdContext
		return STATUS_NOT_SUPPORTED;
	}

	inf->NumberOfPorts = dev->RootHub->PortCount;
	ASSERT(inf->NumberOfPorts);
	ASSERT(inf->NumberOfPorts <= 255);
	len = FIELD_OFFSET(USB_EXTHUB_INFORMATION_0, Port[inf->NumberOfPorts]);
	if(HubInformationBufferLength < len)
	{
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_BUSIF, "buffer too small");
		return STATUS_BUFFER_TOO_SMALL;
	}

	for(i = 0; i < inf->NumberOfPorts; i++)
	{
		inf->Port[i].PhysicalPortNumber = i + 1;

		inf->Port[i].PortLabelNumber = i + 1; // WTF is this?
		// MSDN says it MUST NOT be the same as PhysicalPortNumber
		//  -- so, what MUST it be?
		// A comment in hubbusif.h says it MAY NOT be the same
		//  -- so, we give a crap and set it to the same value.

		inf->Port[i].VidOverride = 0;
		inf->Port[i].PidOverride = 0;
		inf->Port[i].PortAttributes = USB_PORTATTR_SHARED_USB2;
	}

	if(LengthOfDataReturned) *LengthOfDataReturned = len;
	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, "--> *LengthOfDataReturned:      %lu", len);
	return STATUS_SUCCESS;
}

NTSTATUS
USB_BUSIFFN
VUsbVhci_BUSIF_GetRootHubSymbolicName(
	_In_    PVOID  BusContext,
	_Inout_ PVOID  HubInformationBuffer,
	_In_    ULONG  HubInformationBufferLength,
	_Out_   PULONG HubNameActualLength
	)
/*
    Documentation from http://www.osronline.com/:

    The GetRootHubSymbolicName routine returns the symbolic name of
    the root hub device.

    Parameters
      HubInformationBuffer
        Contains, on return, the symbolic name of the root hub. Only
        the symbolic name itself, and not the fully qualified path,
        is returned. If the fully qualified pathname of the root hub
        in the object tree is "\xxx\name", GetRootHubSymbolicName
        removes "\xxx\" where "x" represents zero or more characters.
        So, for instance, if the fully qualified name is "\\??\USB\ROOT_HUB,
        GetRootHubSymbolicName returns "ROOT_HUB."
      HubInformationBufferLength
        Contains the length in bytes of the caller-allocated buffer
        that was passed to GetRootHubSymbolicName in the parameter
        HubInformationBuffer.
      HubNameActualLength
        Contains, on return, the length in bytes of the root hub
        symbolic name.

    IRQL = PASSIVE_LEVEL
*/
{
	NTSTATUS         status;
	PUSBHUB_CONTEXT  rhub = BusContext;
	PPDO_DEVICE_DATA pdoData;
	ULONG            i, basenameCharLen, basenameByteLen, basenameCharAvail;

	PAGED_CODE();

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, ">%!FUNC!");

	ASSERT(rhub);
	ASSERT(rhub->HcdContext);
	pdoData = (PPDO_DEVICE_DATA)((PDEVICE_OBJECT)rhub->HcdContext)->DeviceExtension;
	ASSERT(pdoData);

	status = VUsbVhci_GetAndStoreRootHubName(pdoData);
	if(!NT_SUCCESS(status))
	{
		return status;
	}
	ASSERT(pdoData->InterfaceName.Buffer);
	ASSERT(pdoData->InterfaceName.Length / 2);

	// we want the basename -- search for the start index
	for(i = pdoData->InterfaceName.Length / 2; i; i--)
	{
		if(pdoData->InterfaceName.Buffer[i - 1] == L'\\')
		{
			break;
		}
	}

	basenameCharLen = pdoData->InterfaceName.Length / 2 - i + 1;
	basenameByteLen = basenameCharLen * sizeof(WCHAR);
	if(HubNameActualLength) *HubNameActualLength = basenameByteLen;

	basenameCharAvail = basenameCharLen;
	if(basenameByteLen > HubInformationBufferLength)
	{
		basenameCharAvail = HubInformationBufferLength / sizeof(WCHAR);
	}

	if(HubInformationBuffer && HubInformationBufferLength)
	{
		RtlCopyMemory(HubInformationBuffer,
		              pdoData->InterfaceName.Buffer + i,
		              basenameCharAvail * sizeof(WCHAR));
	}
	if(basenameCharAvail)
	{
		// make sure the last character is 0
		((PWCHAR)HubInformationBuffer)[basenameCharAvail - 1] = 0;
		TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, "RootHubSymlink: %ws", HubInformationBuffer);
	}

	return STATUS_SUCCESS;
}

PVOID
USB_BUSIFFN
VUsbVhci_BUSIF_GetDeviceBusContext(
	_In_ PVOID HubBusContext,
	_In_ PVOID DeviceHandle
	)
/*
    Documentation from http://www.osronline.com/:

    The GetDeviceBusContext routine returns the bus context.

    If the bus (hub) is a USB 1.1-compliant bus, GetDeviceBusContext returns
    the context for the physical hub. If the hub is a USB 2.0-compliant hub,
    it returns the context for the virtual USB 1.1 hub that is associated with
    DeviceHandle.

    Return Value
      GetDeviceBusContext returns a pointer to the bus context of the device.

    IRQL = PASSIVE_LEVEL
*/
{
	PAGED_CODE();

	UNREFERENCED_PARAMETER(DeviceHandle);

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, ">%!FUNC!");

	ASSERT(HubBusContext);
	return HubBusContext;
}

NTSTATUS
USB_BUSIFFN
VUsbVhci_BUSIF_Initialize20Hub(
	_In_ PVOID              HubBusContext,
	_In_ PUSB_DEVICE_HANDLE HubDeviceHandle,
	_In_ ULONG              TtCount
	)
/*
    Documentation from http://www.osronline.com/:

    The InitializeUsbDevice routine initializes a USB 2.0 hub.

    Parameters
      TtCount
        Indicates the number of virtual USB 1.1 hubs to create for this
        high-speed USB 2.0 hub.

    Return Value
      STATUS_SUCCESS
        The call completed successfully.
      STATUS_INVALID_PARAMETER
        The device handle is invalid.
      STATUS_INSUFFICIENT_RESOURCES
        The port driver could not allocate the resources necessary to
        properly initialize the bus.

    IRQL = PASSIVE_LEVEL
*/
{
	PAGED_CODE();

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, ">%!FUNC!");

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, "HubBusContext   = 0x%p", HubBusContext);
	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, "HubDeviceHandle = 0x%p", HubDeviceHandle);
	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, "TtCount         = %lu", TtCount);

	return STATUS_SUCCESS;
}

NTSTATUS
USB_BUSIFFN
VUsbVhci_BUSIF_RootHubInitNotification(
	_In_ PVOID             HubBusContext,
	_In_ PVOID             CallbackContext,
	_In_ PRH_INIT_CALLBACK CallbackFunction
	)
/*
    Documentation from http://www.osronline.com/:

    The RootHubInitNotification routine provides the port driver with
    a callback routine that the port driver uses to notify the bus driver
    that it may enumerate its devices.

    Parameters
      CallbackContext
        Pointer to a buffer containing the context information that the
        port driver should pass to the CallbackFunction.
      CallbackFunction
        Pointer to a callback routine. The port driver calls this routine
        to notify the bus driver that it can enumerate the devices on the
        bus. The bus driver should not enumerate its devices before receiving
        this notification.

    Return Value
      RootHubInitNotification returns STATUS_SUCCESS.

    IRQL = PASSIVE_LEVEL
*/
{
	//PUSBHUB_CONTEXT rhub = HubBusContext;

	PAGED_CODE();

	UNREFERENCED_PARAMETER(HubBusContext);

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, ">%!FUNC!");

	//ASSERT(rhub);

	// TODO: Do we need to call this ever again?
	//       Do we have to store the function-pointer
	//       somewhere for later use?
	if(CallbackFunction)
	{
		TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, "*** NOTIFY ROOT-HUB ***");
		// We are ready! -- Come get some!
		CallbackFunction(CallbackContext);
		TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, "*** NOTIFY ROOT-HUB DONE ***");
	}
	//else
	//{
		// The hub-driver calls this routine again to clear
		// the callback-function-pointer, if the hub
		// gets removed.
	//}

	return STATUS_SUCCESS;
}

VOID
USB_BUSIFFN
VUsbVhci_BUSIF_FlushTransfers(
	PVOID BusContext,
	PVOID DeviceHandle
	)
/*
    Documentation from http://www.osronline.com/:

    The FlushTransfesr routine retrieves flushes any outstanding transfers
    for the indicated device.

    In addition to flushing outstanding tranfers, this routine also flushes
    the bad request list. If the caller does not pass a device handle in the
    DeviceHandle parameter, FlushTransfers just flushes all tranfers on the
    bad request list. The purpose of this function is to complete any transfers
    that may be pending for client drivers that are about to unload.

    IRQL = PASSIVE_LEVEL
*/
{
	PAGED_CODE();

	UNREFERENCED_PARAMETER(BusContext);
	UNREFERENCED_PARAMETER(DeviceHandle);

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, ">%!FUNC!");

	// TODO: implement
}

VOID
USB_BUSIFFN
VUsbVhci_BUSIF_SetDeviceHandleData(
	PVOID          BusContext,
	PVOID          DeviceHandle,
	PDEVICE_OBJECT UsbDevicePdo
	)
/*
    The SetDeviceHandleData routine associates a particular PDO with a
    device handle for use in post mortem debugging.

    IRQL = PASSIVE_LEVEL
*/
{
	PUSBHUB_CONTEXT  rhub = BusContext;
	PFDO_DEVICE_DATA fdoData;
	PUSBDEV_CONTEXT  dev = NULL;

	PAGED_CODE();

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, ">%!FUNC!");

	ASSERT(rhub);
	fdoData = rhub->HcdContext2;
	ASSERT(fdoData);

	if(NT_SUCCESS(VUsbVhci_ReferenceUsbDeviceByHandle(fdoData, DeviceHandle, &dev)))
	{
		dev->HcdContext = UsbDevicePdo;
		VUsbVhci_DereferenceUsbDevice(fdoData, dev);
	}
}

NTSTATUS
USB_BUSIFFN
VUsbVhci_BUSIF_SubmitIsoOutUrb(
	_In_ PVOID BusContext,
	_In_ PURB  Urb
	)
/*
    Documentation from http://www.osronline.com/:

    The SubmitIsoOutUrb function submits a USB request block (URB)
    directly to the bus driver without requiring the allocation of an IRP.

    This function replaces the USBD_BusSubmitIsoOutUrb library function
    provided by usbd.sys.
    This function allows clients running in real-time threads at an elevated
    IRQL to have rapid access to the bus driver. This USB host controller must
    support real-time threads for this function to work.
    The calling driver forfeits any packet-level error information when calling
    this function.

    Return Value
      STATUS_SUCCESS
        The call completed successfully.
      STATUS_NOT_SUPPORTED
        Fast isochronous interfaces and real-time threads are not supported by the host controller.

    IRQL <= DISPATCH_LEVEL
*/
{
	UNREFERENCED_PARAMETER(BusContext);
	UNREFERENCED_PARAMETER(Urb);

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, ">%!FUNC!");
	return STATUS_NOT_SUPPORTED;
}

VOID
USB_BUSIFFN
VUsbVhci_BUSIF_GetUSBDIVersion(
	_In_    PVOID                     BusContext,
	_Inout_ PUSBD_VERSION_INFORMATION VersionInformation,
	_Inout_ PULONG                    HcdCapabilities
	)
/*
    Documentation from http://www.osronline.com/:

    The GetUSBDIVersion function returns the USB interface version number
    and the version number of the USB specification that defines the
    interface, along with information about host controller capabilities.

    The function returns the highest USBDI Interface Version supported by
    the port driver. This function replaces the USBD_GetUSBDIVersion library
    function provided by usbd.sys.

    Released interface versions are listed in the following table.
      Operating System     Interface Version
      Windows 98 Gold             0x00000102
      Windows 98 SE               0x00000200
      Windows 2000                0x00000300
      Windows Millennium Edition  0x00000400
      Windows XP                  0x00000500

    Parameters
      VersionInformation
        Returns a pointer to a USBD_VERSION_INFORMATION structure that
        contains the USB interface version number and the USB specification
        version number. For further information on USBD_VERSION_INFORMATION,
        see USBD_GetUSBDIVersion.
      HcdCapabilities
        Returns the host capability flags. Currently, the only flag reported
        is the USB_HCD_CAPS_SUPPORTS_RT_THREADS flag. This flag indicates
        whether the host controller supports real-time threads.

    IRQL <= DISPATCH_LEVEL
*/
{
	UNREFERENCED_PARAMETER(BusContext);

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, ">%!FUNC!");

	if(VersionInformation)
	{
#ifdef TARGETING_Win2K
		VersionInformation->USBDI_Version = 0x300; // 2k
#else
		VersionInformation->USBDI_Version = 0x500; // xp
#endif
		VersionInformation->Supported_USB_Version = 0x200; // usb spec 2.00
	}

	if(HcdCapabilities)
	{
		*HcdCapabilities = 0; // real-time threads not supported
	}
}

NTSTATUS
USB_BUSIFFN
VUsbVhci_BUSIF_QueryBusTime(
	_In_    PVOID  BusContext,
	_Inout_ PULONG CurrentUsbFrame
	)
/*
    Documentation from http://www.osronline.com/:

    The QueryBusTime function returns the current 32-bit USB frame number.

    This function replaces the USBD_QueryBusTime library function provided by
    usbd.sys. (Note: This library function is obsolete and is no longer documented.)

    IRQL <= DISPATCH_LEVEL
*/
{
	UNREFERENCED_PARAMETER(BusContext);
	UNREFERENCED_PARAMETER(CurrentUsbFrame);

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, ">%!FUNC!");

	// TODO: implement
	KdBreakPoint();

	return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
USB_BUSIFFN
VUsbVhci_BUSIF_EnumLogEntry(
	PVOID BusContext,
	ULONG DriverTag,
	ULONG EnumTag,
	ULONG P1,
	ULONG P2
	)
/*
    Documentation from http://www.osronline.com/:

    The EnumLogEntry routine makes a log entry.

    Return Value
      The EnumLogEntry routine always returns STATUS_SUCCESS.

    IRQL <= DISPATCH_LEVEL
*/
{
	UNREFERENCED_PARAMETER(BusContext);
	UNREFERENCED_PARAMETER(DriverTag);
	UNREFERENCED_PARAMETER(EnumTag);
	UNREFERENCED_PARAMETER(P1);
	UNREFERENCED_PARAMETER(P2);
	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, ">%!FUNC!");
	return STATUS_SUCCESS;
}

NTSTATUS
USB_BUSIFFN
VUsbVhci_BUSIF_QueryBusInformation(
	_In_    PVOID  BusContext,
	_In_    ULONG  Level,
	_Inout_ PVOID  BusInformationBuffer,
	_Inout_ PULONG BusInformationBufferLength,
	_Out_   PULONG BusInformationActualLength
	)
/*
    Documentation from http://www.osronline.com/:

    The QueryBusInformation function returns bus information.

    The exact information returned by this function depends on the value
    of the Level parameter. This function replaces the USBD_QueryBusInformation
    library function provided by usbd.sys.

    Parameters
      Level
        Specifies the level of information to be returned. If Level is zero,
        the function returns the total bandwidth and the total consumed
        bandwidth in bits per second. If Level is 1, the function returns the
        symbolic name of the controller in Unicode, in addition to the total
        bandwidth and the total consumed bandwidth.
      BusInformationBuffer
        Pointer to a buffer that receives the requested bus information.
      BusInformationBufferLength
        On input, the length of the buffer specified by BusInformationBuffer.
        On output, the length of the output data.
      BusInformationActualLength
        Specifies the length of the output data.

    Return Value
      STATUS_SUCCESS
        The call completed successfully.
      STATUS_BUFFER_TOO_SMALL
        The buffer was too small. This error code is returned in two cases:
        * Whenever Level = 0, this error code is returned if the size of the buffer
          pointed to by BusInformationBuffer < sizeof(USB_BUS_INFORMATION_LEVEL_0).
        * Whenever Level = 1, this error code is returned if the size of the buffer
          pointed to by BusInformationBuffer < sizeof(USB_BUS_INFORMATION_LEVEL_1).

    IRQL <= DISPATCH_LEVEL
*/
{
	PUSBHUB_CONTEXT              rhub = BusContext;
	PUSB_BUS_INFORMATION_LEVEL_1 inf = BusInformationBuffer;
	PFDO_DEVICE_DATA             fdoData;
	PWCHAR                       srcBuf;
	ULONG                        i, srcCharLen, dstCharLen, dstStructLen, dstCharAvail;
	NTSTATUS                     status = STATUS_SUCCESS;

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, ">%!FUNC!");

	ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);
	ASSERT(rhub);
	fdoData = rhub->HcdContext2;
	ASSERT(fdoData);

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, "Level                       = %lu", Level);
	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, "BusInformationBuffer        = 0x%p", BusInformationBuffer);
	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, "BusInformationBufferLength  = 0x%p", BusInformationBufferLength);
	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, "BusInformationActualLength  = 0x%p", BusInformationActualLength);

	if(!BusInformationBuffer || !BusInformationBufferLength)
	{
		return STATUS_INVALID_PARAMETER;
	}

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, "*BusInformationBufferLength = %lu", *BusInformationBufferLength);

	if(Level != 0 && Level != 1)
	{
		return STATUS_NOT_SUPPORTED;
	}

	if(Level == 0)
	{
		if(*BusInformationBufferLength < sizeof(USB_BUS_INFORMATION_LEVEL_0))
		{
			return STATUS_BUFFER_TOO_SMALL;
		}
	}
	else
	{
		if(*BusInformationBufferLength < sizeof(USB_BUS_INFORMATION_LEVEL_1))
		{
			return STATUS_BUFFER_TOO_SMALL;
		}
	}

	// TODO: insert right values here
	//       (use interval-fields of active int&iso-endpoints to calculate consumed bandwidth)
	inf->TotalBandwidth = 480000000; // bits/s
	inf->ConsumedBandwidth = 1138;   // bits/s

	if(Level == 0)
	{
		*BusInformationBufferLength = sizeof(USB_BUS_INFORMATION_LEVEL_0);
		if(BusInformationActualLength)
			*BusInformationActualLength = sizeof(USB_BUS_INFORMATION_LEVEL_0);
		return STATUS_SUCCESS;
	}

	srcBuf = fdoData->InterfaceName.Buffer;
	srcCharLen = fdoData->InterfaceName.Length / 2;

	// we want the basename -- search for the start index
	for(i = srcCharLen; i; i--)
	{
		if(srcBuf[i - 1] == L'\\')
		{
			break;
		}
	}

	dstCharLen = srcCharLen - i + 1;
	dstStructLen = FIELD_OFFSET(USB_BUS_INFORMATION_LEVEL_1, ControllerNameUnicodeString[0]) +
	               dstCharLen * sizeof(WCHAR);
	inf->ControllerNameLength = dstCharLen * sizeof(WCHAR);

	dstCharAvail = dstCharLen;
	if(dstStructLen > *BusInformationBufferLength)
	{
		dstCharAvail = *BusInformationBufferLength - FIELD_OFFSET(USB_BUS_INFORMATION_LEVEL_1, ControllerNameUnicodeString[0]);
		dstCharAvail /= sizeof(WCHAR);
	}

	RtlCopyMemory(inf->ControllerNameUnicodeString,
	              srcBuf + i,
	              dstCharAvail * sizeof(WCHAR));
	if(dstCharAvail)
	{
		// make sure the last character is 0
		((PWCHAR)inf->ControllerNameUnicodeString)[dstCharAvail - 1] = 0;
		TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, "HcdSymlink: %ws", inf->ControllerNameUnicodeString);
	}

	if(*BusInformationBufferLength < dstStructLen)
	{
		// The few documentation you find on the internet/MSDN about this routine does not say this,
		// but we have to return STATUS_BUFFER_TOO_SMALL if there is no space for the complete string.
		// If we return STATUS_SUCCESS, the caller trys again with the same small buffer. How stupid!
		status = STATUS_BUFFER_TOO_SMALL;
	}

	// Number of bytes written to buffer
	// (I'm not sure if this is right) (*)
	*BusInformationBufferLength = FIELD_OFFSET(USB_BUS_INFORMATION_LEVEL_1, ControllerNameUnicodeString[0]) +
	                              dstCharAvail * sizeof(WCHAR);

	// Number of bytes needed to write the complete string to the buffer
	// (Here I'm sure this is right) (*)
	if(BusInformationActualLength) *BusInformationActualLength = dstStructLen;

	// *)
	// NOTE: I tested it and found out that it is important to store dstStructLen in
	//       *BusInformationActualLength. The value we store in *BusInformationBufferLength,
	//       however, seems to have no meaning to the caller. We could also use dstStructLen
	//       for this, but it makes somehow no sense to me. Why return the same value twice?

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, "*BusInformationBufferLength = %lu", *BusInformationBufferLength);
	if(BusInformationActualLength)
		TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, "*BusInformationActualLength = %lu", *BusInformationActualLength);

	return status;
}

BOOLEAN
USB_BUSIFFN
VUsbVhci_BUSIF_IsDeviceHighSpeed(
	_In_ PVOID BusContext
	)
/*
    Documentation from http://www.osronline.com/:

    The IsDeviceHighSpeed function returns TRUE if the host controller is a
    high speed USB 2.0-compliant host controller.

    Remarks from MSDN:

    The IsDeviceHighSpeed routine does not indicate whether a device is capable of
    high-speed operation, but whether it is in fact operating at high speed.

    IRQL <= DISPATCH_LEVEL
*/
{
	PUSBHUB_CONTEXT rhub = BusContext;
	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, ">%!FUNC!");
	return rhub->Speed == UsbHighSpeed;
}

#endif // !TARGETING_Win2K
