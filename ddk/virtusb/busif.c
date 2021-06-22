#include "virtusb.h"
#include "busif.h"
#include "proc_urb.h"
#include "usbdev.h"

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, VirtUsb_BUSIF_CreateUsbDevice)
#pragma alloc_text(PAGE, VirtUsb_BUSIF_InitializeUsbDevice)
#endif // ALLOC_PRAGMA

VOID
VirtUsb_InterfaceReference(
	IN PVOID Context
	)
{
	PPDO_DEVICE_DATA pdoData = Context;
	ULONG            res;
	ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);
	VIRTUSB_KDPRINT("in VirtUsb_InterfaceReference\n");
	ASSERT(pdoData);
	res = InterlockedIncrement((PLONG)&pdoData->InterfaceRefCount);
	ASSERT(res);
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
}

VOID
VirtUsb_InterfaceDereference(
	IN PVOID Context
	)
{
	PPDO_DEVICE_DATA pdoData = Context;
	ULONG            res;
	ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);
	VIRTUSB_KDPRINT("in VirtUsb_InterfaceDereference\n");
	ASSERT(pdoData);
	res = InterlockedDecrement((PLONG)&pdoData->InterfaceRefCount);
	ASSERT((LONG)res >= 0);
	if(!res)
	{
		// If this was the last reference, then release the remove lock
		IoReleaseRemoveLock(&pdoData->RemoveLock, pdoData);
	}
}

PUSBHUB_CONTEXT
VIRTUSB_BUSIFFN
VirtUsb_BUSIF_GetRootHubContext(
	IN PVOID Context
	)
/*
    IRQL <= DISPATCH_LEVEL
*/
{
	PPDO_DEVICE_DATA pdoData = Context;
	ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);
	VIRTUSB_KDPRINT("in VirtUsb_BUSIF_GetRootHubContext\n");
	ASSERT(pdoData);
	return pdoData->RootHub;
}

PUSB_HUB_DESCRIPTOR
VIRTUSB_BUSIFFN
VirtUsb_BUSIF_GetRootHubHubDescriptor(
	IN PVOID Context
	)
/*
    IRQL <= DISPATCH_LEVEL
*/
{
	PPDO_DEVICE_DATA pdoData = Context;
	ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);
	VIRTUSB_KDPRINT("in VirtUsb_BUSIF_GetRootHubHubDescriptor\n");
	ASSERT(pdoData);
	ASSERT(pdoData->RootHub);
	return pdoData->RootHub->HubDescriptor;
}

NTSTATUS
VIRTUSB_BUSIFFN
VirtUsb_BUSIF_ReferenceUsbDeviceByHandle(
	IN  PVOID           Context,
	IN  PVOID           DeviceHandle,
	OUT PUSBDEV_CONTEXT *DeviceContext
	)
/*
    IRQL <= DISPATCH_LEVEL
*/
{
	PPDO_DEVICE_DATA pdoData = Context;
	PUSBDEV_CONTEXT  dev;

	ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);

	VIRTUSB_KDPRINT("in VirtUsb_BUSIF_ReferenceUsbDeviceByHandle\n");

	ASSERT(pdoData);

	dev = VirtUsb_ReferenceUsbDeviceByHandle(pdoData, DeviceHandle, (PVOID)(ULONG_PTR)0x1face);
	if(dev)
	{
		if(DeviceContext) *DeviceContext = dev;
		return STATUS_SUCCESS;
	}
	else
	{
		if(DeviceContext) *DeviceContext = NULL;
		return STATUS_DEVICE_NOT_CONNECTED;
	}
}

VOID
VIRTUSB_BUSIFFN
VirtUsb_BUSIF_ReferenceUsbDevice(
	IN PVOID           Context,
	IN PUSBDEV_CONTEXT DeviceContext
	)
/*
    IRQL <= DISPATCH_LEVEL
*/
{
	UNREFERENCED_PARAMETER(Context);
	ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);
	VIRTUSB_KDPRINT("in VirtUsb_BUSIF_ReferenceUsbDevice\n");
	VirtUsb_ReferenceUsbDevice(DeviceContext, (PVOID)(ULONG_PTR)0x1face);
}

VOID
VIRTUSB_BUSIFFN
VirtUsb_BUSIF_DereferenceUsbDevice(
	IN PVOID           Context,
	IN PUSBDEV_CONTEXT DeviceContext
	)
/*
    IRQL <= DISPATCH_LEVEL
*/
{
	UNREFERENCED_PARAMETER(Context);
	ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);
	VIRTUSB_KDPRINT("in VirtUsb_BUSIF_DereferenceUsbDevice\n");
	VirtUsb_DereferenceUsbDevice(DeviceContext, (PVOID)(ULONG_PTR)0x1face, FALSE);
}

NTSTATUS
VIRTUSB_BUSIFFN
VirtUsb_BUSIF_CreateUsbDevice(
	IN     PVOID           Context,
	IN OUT PVOID           *DeviceHandle,
	IN     PUSBHUB_CONTEXT HubDeviceContext,
	IN     USHORT          PortStatus,
	IN     USHORT          PortNumber
	)
/*
    IRQL = PASSIVE_LEVEL
*/
{
	NTSTATUS         status = STATUS_SUCCESS;
	PUSBDEV_CONTEXT  dev;
	PPDO_DEVICE_DATA pdoData = Context;
	PPORT_CONTEXT    port;

	PAGED_CODE();

	VIRTUSB_KDPRINT("in VirtUsb_BUSIF_CreateUsbDevice\n");

	ASSERT(pdoData);
	ASSERT(HubDeviceContext);
	ASSERT(HubDeviceContext->RootHub == pdoData->RootHub);

	if(!PortNumber || PortNumber > HubDeviceContext->PortCount)
		return STATUS_INVALID_PARAMETER;

	port = &HubDeviceContext->Port[PortNumber - 1];

	// Check if there is already a device in this port
	if(port->UsbDevice)
	{
		VIRTUSB_KDPRINT("*** WARNING: recreate existing device ***\n");
		dev = port->UsbDevice;
		// The MS usb hub driver calls CreateUsbDevice again and again, if InitializeUsbDevice
		// has failed. It does NOT call RemoveUsbDevice in such a case. So we have to handle
		// this stupid case somehow.

		// If he already has the same handle (DeviceHandle is IN OUT), then we are done here.
		// In the scenario described above, this is NOT the case. Normally, *DeviceHandle is
		// initialized with NULL; but it won't hurt to check for equality.
		if(DeviceHandle && *DeviceHandle == dev)
			return status;

		// We just skip the allocation.
		goto SetSpeed;
	}

	status = VirtUsb_AllocateUsbDev(pdoData, &dev, port);
	if(!NT_SUCCESS(status))
	{
		return status;
	}
	dev->AssignedAddress = 42; // TODO: find free address
	port->UsbDevice = dev;

SetSpeed:
	if(PortStatus != port->Status)
	{
		VIRTUSB_KDPRINT("*** NOTICE: port status mismatch ***\n");
		VIRTUSB_KDPRINT2("*** I say: 0x%04hx  --  hub driver says: 0x%04hx\n", port->Status, PortStatus);
	}

	if(PortStatus & USB_PORT_STATUS_LOW_SPEED)
		dev->Speed = UsbLowSpeed;
	else if(PortStatus & USB_PORT_STATUS_HIGH_SPEED)
		dev->Speed = UsbHighSpeed;
	else
		dev->Speed = UsbFullSpeed;

	if(DeviceHandle) *DeviceHandle = dev;
	return status;
}

NTSTATUS
VIRTUSB_BUSIFFN
VirtUsb_BUSIF_InitializeUsbDevice(
	IN     PVOID           Context,
	IN OUT PUSBDEV_CONTEXT DeviceContext
	)
/*
    IRQL = PASSIVE_LEVEL
*/
{
	PPDO_DEVICE_DATA      pdoData = Context;
	PIRP                  irp;
	PURB                  urb;
	PUCHAR                buf;
	PVIRTUSB_SETUP_PACKET sp;
	NTSTATUS              status;
	CONST ULONG           len = 2048;
	ULONG                 i;

	PAGED_CODE();

	VIRTUSB_KDPRINT("in VirtUsb_BUSIF_InitializeUsbDevice\n");

	ASSERT(pdoData);
	ASSERT(DeviceContext);
	ASSERT(DeviceContext->RootHub == pdoData->RootHub);

	urb = ExAllocatePoolWithTag(NonPagedPool,
	                            sizeof(URB) + len,
	                            VIRTUSB_POOL_TAG);
	if(!urb)
	{
		return STATUS_INSUFFICIENT_RESOURCES;
	}
	buf = (PUCHAR)urb + sizeof(URB);
	sp = (PVIRTUSB_SETUP_PACKET)urb->UrbControlTransfer.SetupPacket;

	irp = IoAllocateIrp(pdoData->Self->StackSize, FALSE);
	if(!irp)
	{
		ExFreePoolWithTag(urb, VIRTUSB_POOL_TAG);
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	DeviceContext->Ready = TRUE;

	RtlZeroMemory(urb, sizeof(URB));
	urb->UrbHeader.Length                        = sizeof(URB);
	urb->UrbHeader.Function                      = URB_FUNCTION_CONTROL_TRANSFER;
	urb->UrbHeader.Status                        = USBD_STATUS_PENDING;
	urb->UrbHeader.UsbdDeviceHandle              = DeviceContext;
	urb->UrbControlTransfer.PipeHandle           = &DeviceContext->EndpointZero;
	urb->UrbControlTransfer.TransferFlags        = USBD_TRANSFER_DIRECTION_OUT |
	                                               USBD_SHORT_TRANSFER_OK |
	                                               USBD_DEFAULT_PIPE_TRANSFER;
	urb->UrbControlTransfer.TransferBufferLength = 0;
	urb->UrbControlTransfer.TransferBuffer       = NULL;
	sp->bmRequestType                            = 0x00;
	sp->bRequest                                 = USB_REQUEST_SET_ADDRESS;
	sp->wValue                                   = DeviceContext->AssignedAddress;
	sp->wIndex                                   = 0;
	sp->wLength                                  = 0;

	status = VirtUsb_SubmitUrb(pdoData, irp, urb);
	if(!NT_SUCCESS(status))
	{
		VIRTUSB_KDPRINT("SET_ADDRESS failed\n");
		status = STATUS_DEVICE_DATA_ERROR;
		goto Err;
	}

	DeviceContext->Address = DeviceContext->AssignedAddress;
	// we will switch DeviceContext->State to 'Addressed' after we have initialized the
	// descriptors, because other routines make the assumption that the descriptor pointers
	// are not NULL if State is 'Addressed'.

	IoReuseIrp(irp, STATUS_UNSUCCESSFUL);

	RtlZeroMemory(urb, sizeof(URB));
	urb->UrbHeader.Length                        = sizeof(URB);
	urb->UrbHeader.Function                      = URB_FUNCTION_CONTROL_TRANSFER;
	urb->UrbHeader.Status                        = USBD_STATUS_PENDING;
	urb->UrbHeader.UsbdDeviceHandle              = DeviceContext;
	urb->UrbControlTransfer.PipeHandle           = &DeviceContext->EndpointZero;
	urb->UrbControlTransfer.TransferFlags        = USBD_TRANSFER_DIRECTION_IN |
	                                               USBD_SHORT_TRANSFER_OK |
	                                               USBD_DEFAULT_PIPE_TRANSFER;
	urb->UrbControlTransfer.TransferBufferLength = 64;
	urb->UrbControlTransfer.TransferBuffer       = buf;
	sp->bmRequestType                            = 0x80;
	sp->bRequest                                 = USB_REQUEST_GET_DESCRIPTOR;
	sp->wValue                                   = USB_DEVICE_DESCRIPTOR_TYPE << 8;
	sp->wIndex                                   = 0;
	sp->wLength                                  = 64;

	status = VirtUsb_SubmitUrb(pdoData, irp, urb);
	if(!NT_SUCCESS(status))
	{
		VIRTUSB_KDPRINT("GET_DESCRIPTOR(DEVICE) tblen=64 failed -- trying tblen=8\n");

		IoReuseIrp(irp, STATUS_UNSUCCESSFUL);

		RtlZeroMemory(urb, sizeof(URB));
		urb->UrbHeader.Length                        = sizeof(URB);
		urb->UrbHeader.Function                      = URB_FUNCTION_CONTROL_TRANSFER;
		urb->UrbHeader.Status                        = USBD_STATUS_PENDING;
		urb->UrbHeader.UsbdDeviceHandle              = DeviceContext;
		urb->UrbControlTransfer.PipeHandle           = &DeviceContext->EndpointZero;
		urb->UrbControlTransfer.TransferFlags        = USBD_TRANSFER_DIRECTION_IN |
		                                               USBD_SHORT_TRANSFER_OK |
		                                               USBD_DEFAULT_PIPE_TRANSFER;
		urb->UrbControlTransfer.TransferBufferLength = 8;
		urb->UrbControlTransfer.TransferBuffer       = buf;
		sp->bmRequestType                            = 0x80;
		sp->bRequest                                 = USB_REQUEST_GET_DESCRIPTOR;
		sp->wValue                                   = USB_DEVICE_DESCRIPTOR_TYPE << 8;
		sp->wIndex                                   = 0;
		sp->wLength                                  = 8;

		status = VirtUsb_SubmitUrb(pdoData, irp, urb);
		if(!NT_SUCCESS(status))
		{
			VIRTUSB_KDPRINT("GET_DESCRIPTOR(DEVICE) tblen=8 failed\n");
			status = STATUS_DEVICE_DATA_ERROR;
			goto Err;
		}
	}

	if(urb->UrbControlTransfer.TransferBufferLength < 8 ||
	   buf[0] < sizeof(USB_DEVICE_DESCRIPTOR))
	{
		VIRTUSB_KDPRINT("DEVICE descriptor too small\n");
		status = STATUS_DEVICE_DATA_ERROR;
		goto Err;
	}

	if(buf[1] != USB_DEVICE_DESCRIPTOR_TYPE)
	{
		VIRTUSB_KDPRINT("invalid DEVICE descriptor type\n");
		status = STATUS_DEVICE_DATA_ERROR;
		goto Err;
	}

	if(buf[0] > urb->UrbControlTransfer.TransferBufferLength)
	{
		VIRTUSB_KDPRINT1("DEVICE descriptor not complete -- trying tblen=%lu\n", (ULONG)buf[0]);

		IoReuseIrp(irp, STATUS_UNSUCCESSFUL);

		RtlZeroMemory(urb, sizeof(URB));
		urb->UrbHeader.Length                        = sizeof(URB);
		urb->UrbHeader.Function                      = URB_FUNCTION_CONTROL_TRANSFER;
		urb->UrbHeader.Status                        = USBD_STATUS_PENDING;
		urb->UrbHeader.UsbdDeviceHandle              = DeviceContext;
		urb->UrbControlTransfer.PipeHandle           = &DeviceContext->EndpointZero;
		urb->UrbControlTransfer.TransferFlags        = USBD_TRANSFER_DIRECTION_IN |
		                                               USBD_SHORT_TRANSFER_OK |
		                                               USBD_DEFAULT_PIPE_TRANSFER;
		urb->UrbControlTransfer.TransferBufferLength = buf[0];
		urb->UrbControlTransfer.TransferBuffer       = buf;
		sp->bmRequestType                            = 0x80;
		sp->bRequest                                 = USB_REQUEST_GET_DESCRIPTOR;
		sp->wValue                                   = USB_DEVICE_DESCRIPTOR_TYPE << 8;
		sp->wIndex                                   = 0;
		sp->wLength                                  = buf[0];

		status = VirtUsb_SubmitUrb(pdoData, irp, urb);
		if(!NT_SUCCESS(status))
		{
			VIRTUSB_KDPRINT1("GET_DESCRIPTOR(DEVICE) tblen=%lu failed\n", (ULONG)buf[0]);
			status = STATUS_DEVICE_DATA_ERROR;
			goto Err;
		}

		if(urb->UrbControlTransfer.TransferBufferLength < 8 ||
		   buf[0] < sizeof(USB_DEVICE_DESCRIPTOR))
		{
			VIRTUSB_KDPRINT("DEVICE descriptor too small\n");
			status = STATUS_DEVICE_DATA_ERROR;
			goto Err;
		}

		if(buf[1] != USB_DEVICE_DESCRIPTOR_TYPE)
		{
			VIRTUSB_KDPRINT("invalid DEVICE descriptor type\n");
			status = STATUS_DEVICE_DATA_ERROR;
			goto Err;
		}
	}

	// Phew! Now we have the DEVICE descriptor and can store it in our device tree.
	status = VirtUsb_UsbDevInitDesc(DeviceContext, (PUCHAR *)&DeviceContext->Descriptor, buf);
	if(!NT_SUCCESS(status))
	{
		VIRTUSB_KDPRINT("failed to store DEVICE descriptor\n");
		goto Err;
	}

	if(DeviceContext->Descriptor->bcdUSB >= 0x0200)
		DeviceContext->Type = Usb20Device;
	else
		DeviceContext->Type = Usb11Device;

	if(DeviceContext->Descriptor->bDeviceClass == USB_DEVICE_CLASS_HUB)
	{
		VIRTUSB_KDPRINT("WARNING: it's a hub!\n");
		// TODO: get HUB descriptor for port count and call VirtUsb_ConvertToUsbHub
	}

	// allocate array for configurations
	status = VirtUsb_UsbDevInitConfigArray(DeviceContext, DeviceContext->Descriptor->bNumConfigurations);
	if(!NT_SUCCESS(status))
	{
		VIRTUSB_KDPRINT("failed to allocate configurations array\n");
		goto Err;
	}

	for(i = 0; i < DeviceContext->Descriptor->bNumConfigurations; i++)
	{
		ULONG  total, _total;
		PUCHAR tmp = NULL, bufr = buf;

		VIRTUSB_KDPRINT2("GET_DESCRIPTOR(CONFIGURATION, Index=%lu) tblen=%lu\n", i, (ULONG)sizeof(USB_CONFIGURATION_DESCRIPTOR));

		IoReuseIrp(irp, STATUS_UNSUCCESSFUL);

		RtlZeroMemory(urb, sizeof(URB));
		urb->UrbHeader.Length                        = sizeof(URB);
		urb->UrbHeader.Function                      = URB_FUNCTION_CONTROL_TRANSFER;
		urb->UrbHeader.Status                        = USBD_STATUS_PENDING;
		urb->UrbHeader.UsbdDeviceHandle              = DeviceContext;
		urb->UrbControlTransfer.PipeHandle           = &DeviceContext->EndpointZero;
		urb->UrbControlTransfer.TransferFlags        = USBD_TRANSFER_DIRECTION_IN |
		                                               USBD_SHORT_TRANSFER_OK |
		                                               USBD_DEFAULT_PIPE_TRANSFER;
		urb->UrbControlTransfer.TransferBufferLength = sizeof(USB_CONFIGURATION_DESCRIPTOR);
		urb->UrbControlTransfer.TransferBuffer       = buf;
		sp->bmRequestType                            = 0x80;
		sp->bRequest                                 = USB_REQUEST_GET_DESCRIPTOR;
		sp->wValue                                   = (USHORT)((USB_CONFIGURATION_DESCRIPTOR_TYPE << 8) | i);
		sp->wIndex                                   = 0;
		sp->wLength                                  = sizeof(USB_CONFIGURATION_DESCRIPTOR);

		status = VirtUsb_SubmitUrb(pdoData, irp, urb);
		if(!NT_SUCCESS(status))
		{
			VIRTUSB_KDPRINT("GET_DESCRIPTOR(CONFIGURATION) failed\n");
			status = STATUS_DEVICE_DATA_ERROR;
			goto Err;
		}

		if(urb->UrbControlTransfer.TransferBufferLength < sizeof(USB_CONFIGURATION_DESCRIPTOR) ||
		   buf[0] < sizeof(USB_CONFIGURATION_DESCRIPTOR))
		{
			VIRTUSB_KDPRINT("CONFIGURATION descriptor too small\n");
			status = STATUS_DEVICE_DATA_ERROR;
			goto Err;
		}

		if(buf[1] != USB_CONFIGURATION_DESCRIPTOR_TYPE)
		{
			VIRTUSB_KDPRINT("invalid CONFIGURATION descriptor type\n");
			status = STATUS_DEVICE_DATA_ERROR;
			goto Err;
		}

		total = buf[2] | (buf[3] << 8);
		if(total < buf[0])
		{
			VIRTUSB_KDPRINT("CONFIGURATION descriptor total length invalid\n");
			status = STATUS_DEVICE_DATA_ERROR;
			goto Err;
		}

		if(total > sizeof(USB_CONFIGURATION_DESCRIPTOR))
		{
			if(total > len)
			{
				// Uff, we need to allocate an extra buffer here...
				tmp = ExAllocatePoolWithTag(NonPagedPool,
				                            total,
				                            VIRTUSB_POOL_TAG);
				if(!tmp)
				{
					status = STATUS_INSUFFICIENT_RESOURCES;
					goto Err;
				}
				bufr = tmp;
			}

			VIRTUSB_KDPRINT2("GET_DESCRIPTOR(CONFIGURATION, Index=%lu) tblen=%lu\n", i, total);

			IoReuseIrp(irp, STATUS_UNSUCCESSFUL);

			RtlZeroMemory(urb, sizeof(URB));
			urb->UrbHeader.Length                        = sizeof(URB);
			urb->UrbHeader.Function                      = URB_FUNCTION_CONTROL_TRANSFER;
			urb->UrbHeader.Status                        = USBD_STATUS_PENDING;
			urb->UrbHeader.UsbdDeviceHandle              = DeviceContext;
			urb->UrbControlTransfer.PipeHandle           = &DeviceContext->EndpointZero;
			urb->UrbControlTransfer.TransferFlags        = USBD_TRANSFER_DIRECTION_IN |
			                                               USBD_SHORT_TRANSFER_OK |
			                                               USBD_DEFAULT_PIPE_TRANSFER;
			urb->UrbControlTransfer.TransferBufferLength = total;
			urb->UrbControlTransfer.TransferBuffer       = bufr;
			sp->bmRequestType                            = 0x80;
			sp->bRequest                                 = USB_REQUEST_GET_DESCRIPTOR;
			sp->wValue                                   = (USHORT)((USB_CONFIGURATION_DESCRIPTOR_TYPE << 8) | i);
			sp->wIndex                                   = 0;
			sp->wLength                                  = (USHORT)total;

			status = VirtUsb_SubmitUrb(pdoData, irp, urb);
			if(!NT_SUCCESS(status))
			{
				VIRTUSB_KDPRINT("GET_DESCRIPTOR(CONFIGURATION) failed\n");
				status = STATUS_DEVICE_DATA_ERROR;
				goto ErrConf;
			}

			if(urb->UrbControlTransfer.TransferBufferLength < sizeof(USB_CONFIGURATION_DESCRIPTOR) ||
			   bufr[0] < sizeof(USB_CONFIGURATION_DESCRIPTOR))
			{
				VIRTUSB_KDPRINT("CONFIGURATION descriptor too small\n");
				status = STATUS_DEVICE_DATA_ERROR;
				goto ErrConf;
			}

			if(bufr[1] != USB_CONFIGURATION_DESCRIPTOR_TYPE)
			{
				VIRTUSB_KDPRINT("invalid CONFIGURATION descriptor type\n");
				status = STATUS_DEVICE_DATA_ERROR;
				goto ErrConf;
			}

			_total = bufr[2] | (bufr[3] << 8);
			if(_total < bufr[0] || _total > total)
			{
				VIRTUSB_KDPRINT("CONFIGURATION descriptor total length invalid\n");
				status = STATUS_DEVICE_DATA_ERROR;
				goto ErrConf;
			}
			total = _total;
		}

		if(!VirtUsb_CheckConfDescBounds((PUSB_CONFIGURATION_DESCRIPTOR)bufr))
		{
			VIRTUSB_KDPRINT("CONFIGURATION descriptor bounds check failed\n");
			status = STATUS_DEVICE_DATA_ERROR;
			goto ErrConf;
		}

		// Store CONFIGURATION descriptor in our device tree
		status = VirtUsb_UsbDevInitDesc(DeviceContext, (PUCHAR *)&DeviceContext->Configuration[i].Descriptor, bufr);
		if(!NT_SUCCESS(status))
		{
			VIRTUSB_KDPRINT("failed to store CONFIGURATION descriptor\n");
			goto ErrConf;
		}

		// allocate interfaces, endpoints, ...
		status = VirtUsb_UsbDevParseConfDesc(&DeviceContext->Configuration[i]);
		if(!NT_SUCCESS(status))
		{
			VIRTUSB_KDPRINT("failed to parse CONFIGURATION descriptor\n");
			goto ErrConf;
		}

		VIRTUSB_KDPRINT1("configuration context %lu initialization complete\n", i);

		if(tmp)
			ExFreePoolWithTag(tmp, VIRTUSB_POOL_TAG);
		continue;
ErrConf:
		if(tmp)
			ExFreePoolWithTag(tmp, VIRTUSB_POOL_TAG);
		goto Err;
	}

	DeviceContext->State = Addressed;

	VIRTUSB_KDPRINT("*** device initialization done ***\n");

	if(DeviceContext->ActiveConfiguration != MAXULONG)
	{
		PCONF_CONTEXT conf;
		UCHAR         confVal;

		VIRTUSB_KDPRINT("*** restoring device configuration after reset ***\n");
		ASSERT(DeviceContext->ActiveConfiguration < DeviceContext->ConfigurationCount);

		conf = &DeviceContext->Configuration[DeviceContext->ActiveConfiguration];
		confVal = conf->Descriptor->bConfigurationValue;
		VIRTUSB_KDPRINT2("ActiveConfiguration: index=%lu value=%lu\n",
			DeviceContext->ActiveConfiguration,
			(ULONG)confVal);

		IoReuseIrp(irp, STATUS_UNSUCCESSFUL);

		RtlZeroMemory(urb, sizeof(URB));
		urb->UrbHeader.Length                        = sizeof(URB);
		urb->UrbHeader.Function                      = URB_FUNCTION_CONTROL_TRANSFER;
		urb->UrbHeader.Status                        = USBD_STATUS_PENDING;
		urb->UrbHeader.UsbdDeviceHandle              = DeviceContext;
		urb->UrbControlTransfer.PipeHandle           = &DeviceContext->EndpointZero;
		urb->UrbControlTransfer.TransferFlags        = USBD_TRANSFER_DIRECTION_OUT |
		                                               USBD_SHORT_TRANSFER_OK |
		                                               USBD_DEFAULT_PIPE_TRANSFER;
		urb->UrbControlTransfer.TransferBufferLength = 0;
		urb->UrbControlTransfer.TransferBuffer       = NULL;
		sp->bmRequestType                            = 0x00;
		sp->bRequest                                 = USB_REQUEST_SET_CONFIGURATION;
		sp->wValue                                   = confVal;
		sp->wIndex                                   = 0;
		sp->wLength                                  = 0;

		status = VirtUsb_SubmitUrb(pdoData, irp, urb);
		if(!NT_SUCCESS(status))
		{
			VIRTUSB_KDPRINT("SET_CONFIGURATION failed\n");
			status = STATUS_DEVICE_DATA_ERROR;
			goto Err;
		}

		DeviceContext->State = Configured;

		for(i = 0; i < conf->InterfaceCount; i++)
		{
			PIFC_CONTEXT ifc = &conf->Interface[i];
			UCHAR        ifcVal, altVal;
			ASSERT(ifc->ActiveAltSetting <= ifc->AltSettingCount);
			ifcVal = ifc->Descriptor->bInterfaceNumber;
			altVal = ifc->AltSetting[ifc->ActiveAltSetting].Descriptor->bAlternateSetting;
			VIRTUSB_KDPRINT2("set interface: ifcnum=%lu altnum=%lu\n",
				(ULONG)ifcVal, (ULONG)altVal);

			IoReuseIrp(irp, STATUS_UNSUCCESSFUL);

			RtlZeroMemory(urb, sizeof(URB));
			urb->UrbHeader.Length                        = sizeof(URB);
			urb->UrbHeader.Function                      = URB_FUNCTION_CONTROL_TRANSFER;
			urb->UrbHeader.Status                        = USBD_STATUS_PENDING;
			urb->UrbHeader.UsbdDeviceHandle              = DeviceContext;
			urb->UrbControlTransfer.PipeHandle           = &DeviceContext->EndpointZero;
			urb->UrbControlTransfer.TransferFlags        = USBD_TRANSFER_DIRECTION_OUT |
			                                               USBD_SHORT_TRANSFER_OK |
			                                               USBD_DEFAULT_PIPE_TRANSFER;
			urb->UrbControlTransfer.TransferBufferLength = 0;
			urb->UrbControlTransfer.TransferBuffer       = NULL;
			sp->bmRequestType                            = 0x01;
			sp->bRequest                                 = USB_REQUEST_SET_INTERFACE;
			sp->wValue                                   = altVal;
			sp->wIndex                                   = ifcVal;
			sp->wLength                                  = 0;

			status = VirtUsb_SubmitUrb(pdoData, irp, urb);
			if(!NT_SUCCESS(status))
			{
				// usb spec 2.0 section 9.4.10:
				//   If a device only supports a default setting for the
				//   specified interface, then a STALL may be returned in
				//   the Status stage of the request.
				if(ifc->AltSettingCount == 1 &&
				   urb->UrbHeader.Status == USBD_STATUS_STALL_PID)
				{
					/* success */
				}
				else
				{
					VIRTUSB_KDPRINT("SET_INTERFACE failed\n");
					// ignore these
					/*
					status = STATUS_DEVICE_DATA_ERROR;
					goto Err;
					*/
				}
			}
		}
	}

	status = STATUS_SUCCESS;

Err:
	IoFreeIrp(irp);
	ExFreePoolWithTag(urb, VIRTUSB_POOL_TAG);
	return status;
}

NTSTATUS
VIRTUSB_BUSIFFN
VirtUsb_BUSIF_RemoveUsbDevice(
	IN     PVOID           Context,
	IN OUT PUSBDEV_CONTEXT DeviceContext,
	IN     ULONG           Flags,
	IN     BOOLEAN         Dereference
	)
/*
    IRQL < DISPATCH_LEVEL
*/
{
	KIRQL            irql;
	NTSTATUS         status;
	PPDO_DEVICE_DATA pdoData = Context;
	PPORT_CONTEXT    port;
	LARGE_INTEGER    timeout;
	ULONG            retryCount = 3;

	ASSERT(KeGetCurrentIrql() < DISPATCH_LEVEL);

	VIRTUSB_KDPRINT("in VirtUsb_BUSIF_RemoveUsbDevice\n");

	ASSERT(pdoData);
	ASSERT(DeviceContext);
	ASSERT(DeviceContext->RootHub == pdoData->RootHub);

	DeviceContext->Ready = FALSE;

	if(Dereference)
	{
		VirtUsb_DereferenceUsbDevice(DeviceContext, (PVOID)(ULONG_PTR)0x1face, FALSE);
	}

	if(Flags & USBD_MARK_DEVICE_BUSY)
	{
		return STATUS_SUCCESS;
	}

	ASSERT(Dereference);

Retry:
	if(!(retryCount--))
	{
		return STATUS_DEVICE_BUSY;
	}

	timeout.QuadPart = -30000000; // 3 seconds
	status = KeWaitForSingleObject(&DeviceContext->DeviceRemovableEvent,
	                               Executive,
	                               KernelMode,
	                               FALSE,
	                               &timeout);
	if(status == STATUS_TIMEOUT)
	{
		VIRTUSB_KDPRINT("device can not be removed -- outstanding irps\n");
		return STATUS_DEVICE_BUSY;
	}

	ASSERT(!DeviceContext->Ready);

	KeAcquireSpinLock(&pdoData->Lock, &irql);
	if(DeviceContext->RefCount)
	{
		KeReleaseSpinLock(&pdoData->Lock, irql);
		// Damn race! Someone has referenced the device, just to check the Ready field.
		goto Retry;
	}

	if(DeviceContext->IsHub)
	{
		PUSBHUB_CONTEXT hub = (PUSBHUB_CONTEXT)DeviceContext;
		ULONG i;

		for(i = 0; i < hub->PortCount; i++)
		{
			if(hub->Port[i].UsbDevice)
			{
				KeReleaseSpinLock(&pdoData->Lock, irql);
				VIRTUSB_KDPRINT("device can not be removed -- hub has sub-devices\n");
				return STATUS_DEVICE_BUSY;
			}
		}
	}

	port = DeviceContext->ParentPort;
	ASSERT(port || ((PUSBDEV_CONTEXT)DeviceContext->RootHub == DeviceContext));

	if(Flags & USBD_KEEP_DEVICE_DATA)
	{
		// It appears that Windows expects ALL handles (including Pipe Handles)
		// to remain valid -- not only the device handle. Therefore we do not destroy them,
		// but just reset the device state.

		DeviceContext->State = DeviceAttached;
		DeviceContext->Ready = FALSE;
		DeviceContext->Address = 0x00;
	}
	else
	{
		VirtUsb_FreeUsbDev(DeviceContext);
		if(port)
		{
			port->UsbDevice = NULL;
		}
	}

	KeReleaseSpinLock(&pdoData->Lock, irql);
	return STATUS_SUCCESS;
}
