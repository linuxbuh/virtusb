#include "virtusb.h"
#include "trace.h"
#include "busif.tmh"
#include "busif.h"
#include "proc_urb.h"
#include "usbdev.h"

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, VirtUsb_BUSIF_CreateUsbDevice)
#pragma alloc_text(PAGE, VirtUsb_BUSIF_InitializeUsbDevice)
#endif // ALLOC_PRAGMA

VOID
VirtUsb_InterfaceReference(
	_In_ PVOID Context
	)
{
	PPDO_DEVICE_DATA pdoData = Context;
	ULONG            res;
	ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);
	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, ">%!FUNC!");
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
	_In_ PVOID Context
	)
{
	PPDO_DEVICE_DATA pdoData = Context;
	ULONG            res;
	ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);
	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, ">%!FUNC!");
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
	_In_ PVOID Context
	)
/*
    IRQL <= DISPATCH_LEVEL
*/
{
	PPDO_DEVICE_DATA pdoData = Context;
	ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);
	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, ">%!FUNC!");
	ASSERT(pdoData);
	return pdoData->RootHub;
}

PUSB_HUB_DESCRIPTOR
VIRTUSB_BUSIFFN
VirtUsb_BUSIF_GetRootHubHubDescriptor(
	_In_ PVOID Context
	)
/*
    IRQL <= DISPATCH_LEVEL
*/
{
	PPDO_DEVICE_DATA pdoData = Context;
	ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);
	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, ">%!FUNC!");
	ASSERT(pdoData);
	ASSERT(pdoData->RootHub);
	return pdoData->RootHub->HubDescriptor;
}

NTSTATUS
VIRTUSB_BUSIFFN
VirtUsb_BUSIF_ReferenceUsbDeviceByHandle(
	_In_  PVOID           Context,
	_In_  PVOID           DeviceHandle,
	_Out_ PUSBDEV_CONTEXT *DeviceContext
	)
/*
    IRQL <= DISPATCH_LEVEL
*/
{
	PPDO_DEVICE_DATA pdoData = Context;
	PUSBDEV_CONTEXT  dev;

	ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, ">%!FUNC!");

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
	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, ">%!FUNC!");
	VirtUsb_ReferenceUsbDevice(DeviceContext, (PVOID)(ULONG_PTR)0x1face);
}

VOID
VIRTUSB_BUSIFFN
VirtUsb_BUSIF_DereferenceUsbDevice(
	_In_ PVOID           Context,
	_In_ PUSBDEV_CONTEXT DeviceContext
	)
/*
    IRQL <= DISPATCH_LEVEL
*/
{
	UNREFERENCED_PARAMETER(Context);
	ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);
	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, ">%!FUNC!");
	VirtUsb_DereferenceUsbDevice(DeviceContext, (PVOID)(ULONG_PTR)0x1face, FALSE);
}

NTSTATUS
VIRTUSB_BUSIFFN
VirtUsb_BUSIF_CreateUsbDevice(
	_In_    PVOID           Context,
	_Inout_ PVOID           *DeviceHandle,
	_In_    PUSBHUB_CONTEXT HubDeviceContext,
	_In_    USHORT          PortStatus,
	_In_    USHORT          PortNumber
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

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, ">%!FUNC!");

	ASSERT(pdoData);
	ASSERT(HubDeviceContext);
	ASSERT(HubDeviceContext->RootHub == pdoData->RootHub);

	if(!PortNumber || PortNumber > HubDeviceContext->PortCount)
		return STATUS_INVALID_PARAMETER;

	port = &HubDeviceContext->Port[PortNumber - 1];

	// Check if there is already a device in this port
	if(port->UsbDevice)
	{
		TraceEvents(TRACE_LEVEL_WARNING, TRACE_BUSIF, "*** WARNING: recreate existing device ***");
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
		TraceEvents(TRACE_LEVEL_WARNING, TRACE_BUSIF, "*** NOTICE: port status mismatch ***");
		TraceEvents(TRACE_LEVEL_WARNING, TRACE_BUSIF, "*** I say: 0x%04hx  --  hub driver says: 0x%04hx", port->Status, PortStatus);
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
	_In_    PVOID           Context,
	_Inout_ PUSBDEV_CONTEXT DeviceContext
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

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, ">%!FUNC!");

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
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_BUSIF, "SET_ADDRESS failed");
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
		TraceEvents(TRACE_LEVEL_WARNING, TRACE_BUSIF, "GET_DESCRIPTOR(DEVICE) tblen=64 failed -- trying tblen=8");

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
			TraceEvents(TRACE_LEVEL_ERROR, TRACE_BUSIF, "GET_DESCRIPTOR(DEVICE) tblen=8 failed");
			status = STATUS_DEVICE_DATA_ERROR;
			goto Err;
		}
	}

	if(urb->UrbControlTransfer.TransferBufferLength < 8 ||
	   buf[0] < sizeof(USB_DEVICE_DESCRIPTOR))
	{
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_BUSIF, "DEVICE descriptor too small");
		status = STATUS_DEVICE_DATA_ERROR;
		goto Err;
	}

	if(buf[1] != USB_DEVICE_DESCRIPTOR_TYPE)
	{
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_BUSIF, "invalid DEVICE descriptor type");
		status = STATUS_DEVICE_DATA_ERROR;
		goto Err;
	}

	if(buf[0] > urb->UrbControlTransfer.TransferBufferLength)
	{
		TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, "DEVICE descriptor not complete -- trying tblen=%lu", (ULONG)buf[0]);

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
			TraceEvents(TRACE_LEVEL_ERROR, TRACE_BUSIF, "GET_DESCRIPTOR(DEVICE) tblen=%lu failed", (ULONG)buf[0]);
			status = STATUS_DEVICE_DATA_ERROR;
			goto Err;
		}

		if(urb->UrbControlTransfer.TransferBufferLength < 8 ||
		   buf[0] < sizeof(USB_DEVICE_DESCRIPTOR))
		{
			TraceEvents(TRACE_LEVEL_ERROR, TRACE_BUSIF, "DEVICE descriptor too small");
			status = STATUS_DEVICE_DATA_ERROR;
			goto Err;
		}

		if(buf[1] != USB_DEVICE_DESCRIPTOR_TYPE)
		{
			TraceEvents(TRACE_LEVEL_ERROR, TRACE_BUSIF, "invalid DEVICE descriptor type");
			status = STATUS_DEVICE_DATA_ERROR;
			goto Err;
		}
	}

	// Phew! Now we have the DEVICE descriptor and can store it in our device tree.
	status = VirtUsb_UsbDevInitDesc(DeviceContext, (PUCHAR *)&DeviceContext->Descriptor, buf);
	if(!NT_SUCCESS(status))
	{
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_BUSIF, "failed to store DEVICE descriptor");
		goto Err;
	}

	if(DeviceContext->Descriptor->bcdUSB >= 0x0200)
		DeviceContext->Type = Usb20Device;
	else
		DeviceContext->Type = Usb11Device;

	if(DeviceContext->Descriptor->bDeviceClass == USB_DEVICE_CLASS_HUB)
	{
		TraceEvents(TRACE_LEVEL_WARNING, TRACE_BUSIF, "WARNING: it's a hub!");
		// TODO: get HUB descriptor for port count and call VirtUsb_ConvertToUsbHub
	}

	// allocate array for configurations
	status = VirtUsb_UsbDevInitConfigArray(DeviceContext, DeviceContext->Descriptor->bNumConfigurations);
	if(!NT_SUCCESS(status))
	{
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_BUSIF, "failed to allocate configurations array");
		goto Err;
	}

	for(i = 0; i < DeviceContext->Descriptor->bNumConfigurations; i++)
	{
		ULONG  total, _total;
		PUCHAR tmp = NULL, bufr = buf;

		TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, "GET_DESCRIPTOR(CONFIGURATION, Index=%lu) tblen=%lu", i, (ULONG)sizeof(USB_CONFIGURATION_DESCRIPTOR));

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
			TraceEvents(TRACE_LEVEL_ERROR, TRACE_BUSIF, "GET_DESCRIPTOR(CONFIGURATION) failed");
			status = STATUS_DEVICE_DATA_ERROR;
			goto Err;
		}

		if(urb->UrbControlTransfer.TransferBufferLength < sizeof(USB_CONFIGURATION_DESCRIPTOR) ||
		   buf[0] < sizeof(USB_CONFIGURATION_DESCRIPTOR))
		{
			TraceEvents(TRACE_LEVEL_ERROR, TRACE_BUSIF, "CONFIGURATION descriptor too small");
			status = STATUS_DEVICE_DATA_ERROR;
			goto Err;
		}

		if(buf[1] != USB_CONFIGURATION_DESCRIPTOR_TYPE)
		{
			TraceEvents(TRACE_LEVEL_ERROR, TRACE_BUSIF, "invalid CONFIGURATION descriptor type");
			status = STATUS_DEVICE_DATA_ERROR;
			goto Err;
		}

		total = buf[2] | (buf[3] << 8);
		if(total < buf[0])
		{
			TraceEvents(TRACE_LEVEL_ERROR, TRACE_BUSIF, "CONFIGURATION descriptor total length invalid");
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

			TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, "GET_DESCRIPTOR(CONFIGURATION, Index=%lu) tblen=%lu", i, total);

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
				TraceEvents(TRACE_LEVEL_ERROR, TRACE_BUSIF, "GET_DESCRIPTOR(CONFIGURATION) failed");
				status = STATUS_DEVICE_DATA_ERROR;
				goto ErrConf;
			}

			if(urb->UrbControlTransfer.TransferBufferLength < sizeof(USB_CONFIGURATION_DESCRIPTOR) ||
			   bufr[0] < sizeof(USB_CONFIGURATION_DESCRIPTOR))
			{
				TraceEvents(TRACE_LEVEL_ERROR, TRACE_BUSIF, "CONFIGURATION descriptor too small");
				status = STATUS_DEVICE_DATA_ERROR;
				goto ErrConf;
			}

			if(bufr[1] != USB_CONFIGURATION_DESCRIPTOR_TYPE)
			{
				TraceEvents(TRACE_LEVEL_ERROR, TRACE_BUSIF, "invalid CONFIGURATION descriptor type");
				status = STATUS_DEVICE_DATA_ERROR;
				goto ErrConf;
			}

			_total = bufr[2] | (bufr[3] << 8);
			if(_total < bufr[0] || _total > total)
			{
				TraceEvents(TRACE_LEVEL_ERROR, TRACE_BUSIF, "CONFIGURATION descriptor total length invalid");
				status = STATUS_DEVICE_DATA_ERROR;
				goto ErrConf;
			}
			total = _total;
		}

		if(!VirtUsb_CheckConfDescBounds((PUSB_CONFIGURATION_DESCRIPTOR)bufr))
		{
			TraceEvents(TRACE_LEVEL_ERROR, TRACE_BUSIF, "CONFIGURATION descriptor bounds check failed");
			status = STATUS_DEVICE_DATA_ERROR;
			goto ErrConf;
		}

		// Store CONFIGURATION descriptor in our device tree
		status = VirtUsb_UsbDevInitDesc(DeviceContext, (PUCHAR *)&DeviceContext->Configuration[i].Descriptor, bufr);
		if(!NT_SUCCESS(status))
		{
			TraceEvents(TRACE_LEVEL_ERROR, TRACE_BUSIF, "failed to store CONFIGURATION descriptor");
			goto ErrConf;
		}

		// allocate interfaces, endpoints, ...
		status = VirtUsb_UsbDevParseConfDesc(&DeviceContext->Configuration[i]);
		if(!NT_SUCCESS(status))
		{
			TraceEvents(TRACE_LEVEL_ERROR, TRACE_BUSIF, "failed to parse CONFIGURATION descriptor");
			goto ErrConf;
		}

		TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, "configuration context %lu initialization complete", i);

		if(tmp)
			ExFreePoolWithTag(tmp, VIRTUSB_POOL_TAG);
		continue;
ErrConf:
		if(tmp)
			ExFreePoolWithTag(tmp, VIRTUSB_POOL_TAG);
		goto Err;
	}

	DeviceContext->State = Addressed;

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, "*** device initialization done ***");

	if(DeviceContext->ActiveConfiguration != MAXULONG)
	{
		PCONF_CONTEXT conf;
		UCHAR         confVal;

		TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, "*** restoring device configuration after reset ***");
		ASSERT(DeviceContext->ActiveConfiguration < DeviceContext->ConfigurationCount);

		conf = &DeviceContext->Configuration[DeviceContext->ActiveConfiguration];
		confVal = conf->Descriptor->bConfigurationValue;
		TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, "ActiveConfiguration: index=%lu value=%lu",
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
			TraceEvents(TRACE_LEVEL_ERROR, TRACE_BUSIF, "SET_CONFIGURATION failed");
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
			TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, "set interface: ifcnum=%lu altnum=%lu",
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
					TraceEvents(TRACE_LEVEL_ERROR, TRACE_BUSIF, "SET_INTERFACE failed");
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
	_In_    PVOID           Context,
	_Inout_ PUSBDEV_CONTEXT DeviceContext,
	_In_    ULONG           Flags,
	_In_    BOOLEAN         Dereference
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

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_BUSIF, ">%!FUNC!");

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
		TraceEvents(TRACE_LEVEL_WARNING, TRACE_BUSIF, "device can not be removed -- outstanding irps");
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
				TraceEvents(TRACE_LEVEL_WARNING, TRACE_BUSIF, "device can not be removed -- hub has sub-devices");
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
