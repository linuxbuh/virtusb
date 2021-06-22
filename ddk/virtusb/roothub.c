#include "virtusb.h"
#include "roothub.h"
#include "proc_urb.h"
#include "user_io.h"
#include "usbdev.h"

VOID
VirtUsb_RootHubCancelStateNotifyIrp(
	__inout PDEVICE_OBJECT DeviceObject,
	__in    PIRP           Irp
	);

VOID
VirtUsb_RootHubNotifyWorkItemCallback(
	__in     PDEVICE_OBJECT DeviceObject,
	__in_opt PVOID          Context
	);

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, VirtUsb_GenHubDescriptor)
#pragma alloc_text(PAGE, VirtUsb_InitRootHubDescriptors)
#endif // ALLOC_PRAGMA

// NOTE: We make the same errors like windows does!
CONST UCHAR RootHubDeviceDescriptor[sizeof(USB_DEVICE_DESCRIPTOR)] =
{
	sizeof(RootHubDeviceDescriptor),
	USB_DEVICE_DESCRIPTOR_TYPE,
	0x00, 0x02,                          // 02.00 in BCD
	USB_DEVICE_CLASS_HUB,
	0x01,
	0x00,
	8,                                   // Max EP0 PacketSize
	0x38, 0x11,                          // Vendor ID
	0x00, 0xf0,                          // Product ID
	0x00, 0x00,                          // BCD Device
	0x00,
	0x00,
	0x00,
	1                                    // Num Configs
};
CONST UCHAR RootHubConfigurationDescriptor[sizeof(USB_CONFIGURATION_DESCRIPTOR) +
                                           sizeof(USB_INTERFACE_DESCRIPTOR) +
                                           sizeof(USB_ENDPOINT_DESCRIPTOR)] =
{
	sizeof(USB_CONFIGURATION_DESCRIPTOR),
	USB_CONFIGURATION_DESCRIPTOR_TYPE,
	sizeof(RootHubConfigurationDescriptor) & 0x00ff,
	sizeof(RootHubConfigurationDescriptor) >> 8,
	1,                                   // bNumInterfaces
	0x23,                                // bConfigurationValue
	0x00,                                // iConfiguration
	0x40,                                // bmAttributes (self powered)
	0,                                   // bMaxPower

	sizeof(USB_INTERFACE_DESCRIPTOR),
	USB_INTERFACE_DESCRIPTOR_TYPE,
	0x00,                                // bInterfaceNumber
	0x00,                                // bAlternateSetting
	1,                                   // bNumEndpoints
	USB_DEVICE_CLASS_HUB,
	0x01,
	0x00,
	0x00,                                // iInterface

	sizeof(USB_ENDPOINT_DESCRIPTOR),
	USB_ENDPOINT_DESCRIPTOR_TYPE,
	0x81,                                // bEndpointAddress
	USB_ENDPOINT_TYPE_INTERRUPT,
	0x08, 0x00,                          // wMaxPacketSize; has to be overwritten later, if the root-hub has more than 63 ports
	0x0a                                 // bInterval   2^(10-1) = 512µFrames = 64ms
};
CONST UCHAR RootHubHubDescriptor[7] =
{
	sizeof(RootHubHubDescriptor),        // size has to be filled in later; hub descriptors have variable length
	0x29,                                // hub descriptor type
	0,                                   // number of ports
	0x09, 0x00,                          // individual power and overcurrent
	2,                                   // wait 2*2ms=4ms after port power-up
	0                                    // root-hub takes 0mA current from bus
};

PUSB_HUB_DESCRIPTOR
VirtUsb_GenHubDescriptor(
	IN ULONG PortCount
	)
{
	ULONG  len, portArrLen;
	PUCHAR hubDesc;

	PAGED_CODE();

	VIRTUSB_KDPRINT("in VirtUsb_GenHubDescriptor\n");

	ASSERT(PortCount);
	ASSERT(PortCount <= 255); // see usb spec 2.0

	portArrLen = PortCount / 8 + 1; // length of one port bit-array in bytes
	len = 7 + 2 * portArrLen; // length of our hub descriptor
	ASSERT(len >= 9);
	ASSERT(len <= 71);
	hubDesc = ExAllocatePoolWithTag(NonPagedPool,
	                                len,
	                                VIRTUSB_DEVICE_POOL_TAG);
	if(!hubDesc)
	{
		return NULL;
	}
	// copy descriptor base
	RtlCopyMemory(hubDesc, RootHubHubDescriptor, 7);
	// clear every bit in the "DeviceRemovable" bit-array
	RtlZeroMemory(hubDesc + 7,                   portArrLen);
	// set every bit in the "PortPwrCtrlMask" bit-array
	RtlFillMemory(hubDesc + 7 + portArrLen,      portArrLen, 0xff);
	// set actual descriptor length and port count
	((PUSB_HUB_DESCRIPTOR)hubDesc)->bDescriptorLength = (UCHAR)len;
	((PUSB_HUB_DESCRIPTOR)hubDesc)->bNumberOfPorts = (UCHAR)PortCount;
	return (PUSB_HUB_DESCRIPTOR)hubDesc;
}

NTSTATUS
VirtUsb_InitRootHubDescriptors(
	IN PUSBHUB_CONTEXT RootHub,
	IN ULONG           PortCount
	)
{
	NTSTATUS status;
	ULONG    portArrLen;
	UCHAR    maxIntPktSize = 8;

	PAGED_CODE();

	VIRTUSB_KDPRINT("in VirtUsb_InitRootHubDescriptors\n");

	ASSERT(RootHub);
	ASSERT(PortCount);
	ASSERT(PortCount <= 255); // see usb spec 2.0

	portArrLen = PortCount / 8 + 1; // length of port bit-array in bytes
	ASSERT(portArrLen);
	if(portArrLen > 8)
	{
		if(portArrLen > 16)
		{
			ASSERT(portArrLen <= 32);
			VIRTUSB_KDPRINT("*** max interrupt transfer size = 32 ***\n");
			maxIntPktSize = 32;
		}
		else
		{
			VIRTUSB_KDPRINT("*** max interrupt transfer size = 16 ***\n");
			maxIntPktSize = 16;
		}
	}

	ASSERT(RootHubDeviceDescriptor[0] == sizeof(USB_DEVICE_DESCRIPTOR));
	status = VirtUsb_UsbDevInitDesc((PUSBDEV_CONTEXT)RootHub,
	                                (PUCHAR *)&RootHub->Descriptor,
	                                RootHubDeviceDescriptor);
	if(!NT_SUCCESS(status))
	{
		return status;
	}

	status = VirtUsb_UsbDevInitConfigArray((PUSBDEV_CONTEXT)RootHub, RootHub->Descriptor->bNumConfigurations);
	if(!NT_SUCCESS(status))
	{
		return status;
	}

	// On checked build we don't trust our own descriptors
	ASSERT(VirtUsb_CheckConfDescBounds((PUSB_CONFIGURATION_DESCRIPTOR)RootHubConfigurationDescriptor));
	ASSERT(RootHub->ConfigurationCount == 1);

	status = VirtUsb_UsbDevInitDesc((PUSBDEV_CONTEXT)RootHub,
	                                (PUCHAR *)&RootHub->Configuration->Descriptor,
	                                RootHubConfigurationDescriptor);
	if(!NT_SUCCESS(status))
	{
		return status;
	}

	// set the max packet size of the interrupt endpoint
	ASSERT(((PUCHAR)RootHub->Configuration->Descriptor)[22] == 8); // just make sure we got the right index
	((PUCHAR)RootHub->Configuration->Descriptor)[22] = maxIntPktSize;

	status = VirtUsb_UsbDevParseConfDesc(RootHub->Configuration);
	if(!NT_SUCCESS(status))
	{
		return status;
	}

	return STATUS_SUCCESS;
}

NTSTATUS
VirtUsb_ProcRootHubUrb(
	IN PPDO_DEVICE_DATA PdoData,
	IN PIRP             Irp
	)
{
	NTSTATUS                         ntStatus;
	PURB                             urb;
	USBD_STATUS                      usbStatus;
	PUSBHUB_CONTEXT                  rhub;
	PPIPE_CONTEXT                    ep0, epi;
	ULONG                            len, dlen, index, feature;
	PVOID                            buffer;
	PUSB_DEFAULT_PIPE_SETUP_PACKET   sp;
	PUSB_HUB_DESCRIPTOR              hubDesc;
	struct _URB_SELECT_CONFIGURATION *selectConf;
	USHORT                           reqType, *ps, *pc;
	UCHAR                            *pf;
	KIRQL                            irql, cancelIrql;
	BOOLEAN                          gotWork = FALSE;

	ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);

	VIRTUSB_KDPRINT("in VirtUsb_ProcRootHubUrb\n");

	ASSERT(PdoData);
	ASSERT(Irp);
	urb = URB_FROM_IRP(Irp);
	ASSERT(urb);
	rhub = (PUSBHUB_CONTEXT)urb->UrbHeader.UsbdDeviceHandle;
	ASSERT(rhub);
	ep0 = &rhub->EndpointZero;
	epi = rhub->Configuration->Interface->AltSetting->Endpoint;

	KeAcquireSpinLock(&PdoData->Lock, &irql);
	// (it is not that much what it looks like...)

	switch(urb->UrbHeader.Function)
	{
	case URB_FUNCTION_CONTROL_TRANSFER:
		VIRTUSB_KDPRINT("URB_FUNCTION_CONTROL_TRANSFER\n");
		if(urb->UrbControlTransfer.PipeHandle != ep0)
		{
			goto Inval;
		}
		len = urb->UrbControlTransfer.TransferBufferLength;
		buffer = urb->UrbControlTransfer.TransferBuffer;
		sp = (PUSB_DEFAULT_PIPE_SETUP_PACKET)&urb->UrbControlTransfer.SetupPacket;
		VIRTUSB_KDPRINT5("bmRequestType: 0x%02hx -- bRequest: 0x%02hx -- wValue: 0x%04hx -- wIndex: 0x%04hx -- wLength: 0x%04hx\n", (USHORT)sp->bmRequestType.B, (USHORT)sp->bRequest, sp->wValue.W, sp->wIndex.W, sp->wLength);
		reqType = ((USHORT)sp->bmRequestType.B << 8) | sp->bRequest;
		switch(reqType)
		{
		case STDREQ_GET_DESCRIPTOR:
			VIRTUSB_KDPRINT("GET_DESCRIPTOR\n");
			switch(urb->UrbControlDescriptorRequest.DescriptorType)
			{
			case USB_DEVICE_DESCRIPTOR_TYPE:
				VIRTUSB_KDPRINT("DEVICE_DESCRIPTOR\n");
				ASSERT(rhub->Descriptor);
				dlen = rhub->Descriptor->bLength;
				if(len > dlen)
				{
					len = dlen;
					urb->UrbControlTransfer.TransferBufferLength = len;
				}
				RtlCopyMemory(buffer, rhub->Descriptor, len);
				goto Ack;

			case USB_CONFIGURATION_DESCRIPTOR_TYPE:
				index = urb->UrbControlDescriptorRequest.Index;
				VIRTUSB_KDPRINT1("CONFIGURATION_DESCRIPTOR[%lu]\n", index);
				ASSERT(rhub->Configuration);
				ASSERT(rhub->ConfigurationCount); // our root-hub always has a config decriptor
				if(index >= rhub->ConfigurationCount)
				{
					VIRTUSB_KDPRINT("index out of range\n");
					goto Stall;
				}
				dlen = rhub->Configuration[index].Descriptor->wTotalLength;
				if(len > dlen)
				{
					len = dlen;
					urb->UrbControlTransfer.TransferBufferLength = len;
				}
				RtlCopyMemory(buffer, rhub->Configuration[index].Descriptor, len);
				goto Ack;

			case USB_STRING_DESCRIPTOR_TYPE:
				index = urb->UrbControlDescriptorRequest.Index;
				VIRTUSB_KDPRINT1("STRING_DESCRIPTOR[%lu]\n", index);
				goto Stall;
			}
			goto Stall;

		case STDREQ_GET_STATUS:
			VIRTUSB_KDPRINT("GET_STATUS\n");
			if(urb->UrbControlGetStatusRequest.Index || sp->wValue.W)
			{
				goto Stall;
			}
			dlen = sizeof(USHORT);
			if(len > dlen)
			{
				len = dlen;
				urb->UrbControlTransfer.TransferBufferLength = len;
			}
			*(PUSHORT)buffer = USB_GETSTATUS_SELF_POWERED;
			goto Ack;

		case HUBREQ_GET_HUB_STATUS:
			VIRTUSB_KDPRINT("GET_HUB_STATUS\n");
			if(urb->UrbControlGetStatusRequest.Index || sp->wValue.W)
			{
				goto Stall;
			}
			dlen = sizeof(ULONG);
			if(len > dlen)
			{
				len = dlen;
				urb->UrbControlTransfer.TransferBufferLength = len;
			}
			*(PULONG)buffer = 0;
			goto Ack;

		case HUBREQ_GET_PORT_STATUS:
			VIRTUSB_KDPRINT("GET_PORT_STATUS\n");
			index = urb->UrbControlGetStatusRequest.Index;
			if(!index || index > rhub->PortCount || sp->wValue.W)
			{
				goto Stall;
			}
			ps = &rhub->Port[index - 1].Status;
			pc = &rhub->Port[index - 1].Change;
			dlen = 2 * sizeof(USHORT);
			if(len > dlen)
			{
				len = dlen;
				urb->UrbControlTransfer.TransferBufferLength = len;
			}
			VIRTUSB_KDPRINT3("Port %lu [port_status=0x%04hx] [port_change=0x%04hx]\n", index, *ps, *pc);
			((PSHORT)buffer)[0] = *ps;
			((PSHORT)buffer)[1] = *pc;
			if(!KeSetEvent(&rhub->Port[index - 1].HubDriverTouchedEvent, IO_NO_INCREMENT, FALSE))
			{
				VIRTUSB_KDPRINT1("Port %lu HubDriverTouchedEvent signaled\n", index);
			}
			goto Ack;

		case HUBREQ_GET_HUB_DESCRIPTOR:
			VIRTUSB_KDPRINT("GET_HUB_DESCRIPTOR\n");
			switch(urb->UrbControlDescriptorRequest.DescriptorType)
			{
			case 0x00: // windows tries 0 first
			case 0x29: // hub descriptor
				VIRTUSB_KDPRINT("HUB_DESCRIPTOR\n");
				hubDesc = rhub->HubDescriptor;
				ASSERT(hubDesc);
				dlen = hubDesc->bDescriptorLength;
				if(len > dlen)
				{
					len = dlen;
					urb->UrbControlTransfer.TransferBufferLength = len;
				}
				RtlCopyMemory(buffer, hubDesc, len);
				goto Ack;
			}
			goto Stall;

		case HUBREQ_CLEAR_HUB_FEATURE:
		case HUBREQ_SET_HUB_FEATURE:
			VIRTUSB_KDPRINT("CLEAR/SET_HUB_FEATURE (no-op)\n");
			if(urb->UrbControlFeatureRequest.Index ||
			   sp->wLength ||
			   (urb->UrbControlFeatureRequest.FeatureSelector != 0 && // C_HUB_LOCAL_POWER
			    urb->UrbControlFeatureRequest.FeatureSelector != 1))  // C_HUB_OVER_CURRENT
			{
				goto Stall;
			}
			goto Ack; // no-op

		case HUBREQ_CLEAR_PORT_FEATURE:
			VIRTUSB_KDPRINT("CLEAR_PORT_FEATURE\n");
			feature = urb->UrbControlFeatureRequest.FeatureSelector;
			index = urb->UrbControlFeatureRequest.Index;
			if(!index || index > rhub->PortCount || sp->wLength)
			{
				goto Stall;
			}
			ps = &rhub->Port[index - 1].Status;
			pc = &rhub->Port[index - 1].Change;
			pf = &rhub->Port[index - 1].Flags;
			switch(feature)
			{
			case FEATURE_PORT_SUSPEND:
				// (see USB 2.0 spec section 11.5 and 11.24.2.7.1.3)
				if(*ps & USB_PORT_STATUS_SUSPEND)
				{
					VIRTUSB_KDPRINT1("Port %lu resuming\n", index);
					*pf |= VIRTUSB_PORT_STAT_FLAGS_RESUMING;
					VirtUsb_PortStatUpdateToUser(PdoData, (UCHAR)index);
					gotWork = TRUE;
				}
				goto Ack;
			case FEATURE_PORT_POWER:
				// (see USB 2.0 spec section 11.11 and 11.24.2.7.1.6)
				if(*ps & USB_PORT_STATUS_POWER)
				{
					VIRTUSB_KDPRINT1("Port %lu power-off\n", index);
					// clear all status bits except overcurrent (see USB 2.0 spec section 11.24.2.7.1)
					*ps &= USB_PORT_STATUS_OVER_CURRENT;
					// clear all change bits except overcurrent (see USB 2.0 spec section 11.24.2.7.2)
					*pc &= USB_PORT_STATUS_OVER_CURRENT;
					// clear resuming flag
					*pf &= ~VIRTUSB_PORT_STAT_FLAGS_RESUMING;
					VirtUsb_PortStatUpdateToUser(PdoData, (UCHAR)index);
					gotWork = TRUE;
				}
				goto Ack;
			case FEATURE_PORT_ENABLE:
				// (see USB 2.0 spec section 11.5.1.4 and 11.24.2.7.{1,2}.2)
				if(*ps & USB_PORT_STATUS_ENABLE)
				{
					VIRTUSB_KDPRINT1("Port %lu disabled\n", index);
					// clear enable and suspend bits (see section 11.24.2.7.1.{2,3})
					*ps &= ~(USB_PORT_STATUS_ENABLE | USB_PORT_STATUS_SUSPEND);
					// i'm not quite sure if the suspend change bit should be cleared too (see section 11.24.2.7.2.{2,3})
					*pc &= ~(USB_PORT_STATUS_ENABLE | USB_PORT_STATUS_SUSPEND);
					// clear resuming flag
					*pf &= ~VIRTUSB_PORT_STAT_FLAGS_RESUMING;
					// TODO: maybe we should clear the low/high speed bits here (section 11.24.2.7.1.{7,8})
					VirtUsb_PortStatUpdateToUser(PdoData, (UCHAR)index);
					gotWork = TRUE;
				}
				goto Ack;
			case FEATURE_PORT_CONNECTION:
			case FEATURE_PORT_OVER_CURRENT:
			case FEATURE_PORT_RESET:
			case FEATURE_PORT_LOW_SPEED:
			case FEATURE_PORT_HIGH_SPEED:
			case FEATURE_PORT_INDICATOR:
				goto Ack; // no-op
			case FEATURE_C_PORT_CONNECTION:
			case FEATURE_C_PORT_ENABLE:
			case FEATURE_C_PORT_SUSPEND:
			case FEATURE_C_PORT_OVER_CURRENT:
			case FEATURE_C_PORT_RESET:
				if(*pc & (1 << (feature - 16)))
				{
					*pc &= ~(1 << (feature - 16));
					VirtUsb_PortStatUpdateToUser(PdoData, (UCHAR)index);
					gotWork = TRUE;
				}
				goto Ack;
			//case FEATURE_PORT_TEST:
			}
			goto Stall;

		case HUBREQ_SET_PORT_FEATURE:
			VIRTUSB_KDPRINT("SET_PORT_FEATURE\n");
			feature = urb->UrbControlFeatureRequest.FeatureSelector;
			index = urb->UrbControlFeatureRequest.Index;
			if(!index || index > rhub->PortCount || sp->wLength)
			{
				goto Stall;
			}
			ps = &rhub->Port[index - 1].Status;
			pc = &rhub->Port[index - 1].Change;
			pf = &rhub->Port[index - 1].Flags;
			switch(feature)
			{
			case FEATURE_PORT_SUSPEND:
				// USB 2.0 spec section 11.24.2.7.1.3:
				//  "This bit can be set only if the port’s PORT_ENABLE bit is set and the hub receives
				//  a SetPortFeature(PORT_SUSPEND) request."
				// The spec also says that the suspend bit has to be cleared whenever the enable bit is cleared.
				// (see also section 11.5)
				if((*ps & USB_PORT_STATUS_ENABLE) && !(*ps & USB_PORT_STATUS_SUSPEND))
				{
					VIRTUSB_KDPRINT1("Port %lu suspended\n", index);
					*ps |= USB_PORT_STATUS_SUSPEND;
					VirtUsb_PortStatUpdateToUser(PdoData, (UCHAR)index);
					gotWork = TRUE;
				}
				goto Ack;
			case FEATURE_PORT_POWER:
				// (see USB 2.0 spec section 11.11 and 11.24.2.7.1.6)
				if(!(*ps & USB_PORT_STATUS_POWER))
				{
					VIRTUSB_KDPRINT1("Port %lu power-on\n", index);
					*ps |= USB_PORT_STATUS_POWER;
					VirtUsb_PortStatUpdateToUser(PdoData, (UCHAR)index);
					gotWork = TRUE;
				}
				goto Ack;
			case FEATURE_PORT_RESET:
				// (see USB 2.0 spec section 11.24.2.7.1.5)
				// initiate reset only if there is a device plugged into the port and if there isn't already a reset pending
				if((*ps & USB_PORT_STATUS_CONNECT) && !(*ps & USB_PORT_STATUS_RESET))
				{
					VIRTUSB_KDPRINT1("Port %lu resetting\n", index);

					// keep the state of these bits and clear all others
					*ps &= USB_PORT_STATUS_POWER
					     | USB_PORT_STATUS_CONNECT
					     | USB_PORT_STATUS_LOW_SPEED
					     | USB_PORT_STATUS_HIGH_SPEED
					     | USB_PORT_STATUS_OVER_CURRENT;

					*ps |= USB_PORT_STATUS_RESET; // reset initiated

					// clear resuming flag
					*pf &= ~VIRTUSB_PORT_STAT_FLAGS_RESUMING;

					VirtUsb_PortStatUpdateToUser(PdoData, (UCHAR)index);
					gotWork = TRUE;
				}
				else
				{
					VIRTUSB_KDPRINT2("Port %lu reset not possible because of port_state=%04hx\n", index, *ps);
				}
				goto Ack;
			case FEATURE_PORT_CONNECTION:
			case FEATURE_PORT_OVER_CURRENT:
			case FEATURE_PORT_LOW_SPEED:
			case FEATURE_PORT_HIGH_SPEED:
			case FEATURE_PORT_INDICATOR:
				goto Ack; // no-op
			case FEATURE_C_PORT_CONNECTION:
			case FEATURE_C_PORT_ENABLE:
			case FEATURE_C_PORT_SUSPEND:
			case FEATURE_C_PORT_OVER_CURRENT:
			case FEATURE_C_PORT_RESET:
				if(!(*pc & (1 << (feature - 16))))
				{
					*pc |= 1 << (feature - 16);
					VirtUsb_PortStatUpdateToUser(PdoData, (UCHAR)index);
					gotWork = TRUE;
				}
				goto Ack;
			//case FEATURE_PORT_ENABLE: // port can't be enabled without reseting (USB 2.0 spec section 11.24.2.7.1.2)
			//case FEATURE_PORT_TEST:
			}
			goto Stall;

		default:
			VIRTUSB_KDPRINT1("*** unhandled control transfer -- reqType: 0x%04hx\n", reqType);
			// TODO: did we miss something?
			KdBreakPoint();
		}
		goto Stall;

	case URB_FUNCTION_SELECT_CONFIGURATION:
		VIRTUSB_KDPRINT("URB_FUNCTION_SELECT_CONFIGURATION\n");
		VIRTUSB_KDPRINT1("--> Hdr.Length:              %lu\n", urb->UrbHeader.Length);
		selectConf = &urb->UrbSelectConfiguration;
		VIRTUSB_KDPRINT1("--> ConfigurationDescriptor: 0x%p\n", selectConf->ConfigurationDescriptor);
		VIRTUSB_KDPRINT1("--> ConfigurationHandle:     0x%p\n", selectConf->ConfigurationHandle);
		if(selectConf->ConfigurationDescriptor)
		{
			UCHAR                       confValue = selectConf->ConfigurationDescriptor->bConfigurationValue;
			ULONG                       confIndex, i, j;
			PCONF_CONTEXT               conf;
			PUSBD_INTERFACE_INFORMATION ifcInf;

			// TODO: Implement a routine for dumping
			//       configuration descriptors and call
			//       it here.
			VIRTUSB_KDPRINT1("--> *** ConfigurationDescriptor.bConfigurationValue: 0x%02hu\n", (USHORT)confValue);

			// Search for the config
			for(confIndex = 0; confIndex < rhub->ConfigurationCount; confIndex++)
			{
				conf = &rhub->Configuration[confIndex];
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
			VIRTUSB_KDPRINT2("*** config index is %lu; handle&context are 0x%p ***\n", confIndex, conf);
			selectConf->ConfigurationHandle = conf;

			// configure device
			rhub->ActiveConfiguration = confIndex;
			KeMemoryBarrier(); // enforce setting index first
			rhub->State = Configured;

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
				ifcInf = (PUSBD_INTERFACE_INFORMATION)((PUCHAR)ifcInf + ifcInf->Length);

				// select alternate setting
				ifc->ActiveAltSetting = altIndex;
			}
		}
		else
		{
			// unconfigure the device
			rhub->State = Addressed;
			KeMemoryBarrier(); // enforce setting state first
			rhub->ActiveConfiguration = MAXULONG;
		}
		goto Ack;

	case URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER:
		VIRTUSB_KDPRINT("URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER\n");
		if(urb->UrbBulkOrInterruptTransfer.PipeHandle != epi)
		{
			goto Inval;
		}
		urb->UrbBulkOrInterruptTransfer.TransferFlags |= USBD_TRANSFER_DIRECTION_IN;
		len = urb->UrbBulkOrInterruptTransfer.TransferBufferLength;
		buffer = urb->UrbBulkOrInterruptTransfer.TransferBuffer;

		IoMarkIrpPending(Irp);
		IoAcquireCancelSpinLock(&cancelIrql);
		IoSetCancelRoutine(Irp, &VirtUsb_RootHubCancelStateNotifyIrp);
		if(Irp->Cancel)
		{
			PDRIVER_CANCEL cancelCallback = IoSetCancelRoutine(Irp, NULL);
			IoReleaseCancelSpinLock(cancelIrql);
			InitializeListHead(&Irp->Tail.Overlay.ListEntry);
			KeReleaseSpinLock(&PdoData->Lock, irql);
			if(cancelCallback)
			{
				VirtUsb_DereferenceUsbDevice((PUSBDEV_CONTEXT)rhub, Irp, FALSE);
				Irp->IoStatus.Status = STATUS_CANCELLED;
				urb->UrbHeader.Status = USBD_STATUS_CANCELED;
				Irp->IoStatus.Information = 0;
				IoCompleteRequest(Irp, IO_NO_INCREMENT);
				IoReleaseRemoveLock(&PdoData->RemoveLock, Irp);
			}
		}
		else
		{
			IoReleaseCancelSpinLock(cancelIrql);
			InsertTailList(&PdoData->RootHubRequests,
			               &Irp->Tail.Overlay.ListEntry);
			KeReleaseSpinLock(&PdoData->Lock, irql);
		}

		VirtUsb_RootHubNotify(PdoData);
		VIRTUSB_KDPRINT("*** state notify irp pending\n");
		return STATUS_PENDING;

	default:
		VIRTUSB_KDPRINT1("inval urb function: 0x%08lx\n", urb->UrbHeader.Function);
		usbStatus = USBD_STATUS_INVALID_URB_FUNCTION;
		ntStatus = STATUS_INVALID_PARAMETER;
		break;

Ack:
		usbStatus = USBD_STATUS_SUCCESS;
		ntStatus = STATUS_SUCCESS;
		break;

Stall:
		usbStatus = USBD_STATUS_STALL_PID;
		ntStatus = STATUS_UNSUCCESSFUL;
		break;

Inval:
		usbStatus = USBD_STATUS_INVALID_PARAMETER;
		ntStatus = STATUS_INVALID_PARAMETER;
		break;
	}

	if(gotWork)
	{
		VirtUsb_GotWork(PdoData, irql); // releases spin lock
	}
	else
	{
		KeReleaseSpinLock(&PdoData->Lock, irql);
	}

	VirtUsb_RootHubNotify(PdoData);
	VirtUsb_DereferenceUsbDevice((PUSBDEV_CONTEXT)rhub, Irp, FALSE);
	urb->UrbHeader.Status = usbStatus;
	Irp->IoStatus.Status = ntStatus;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	IoReleaseRemoveLock(&PdoData->RemoveLock, Irp);
	return ntStatus;
}

VOID
VirtUsb_RootHubNotify(
	IN PPDO_DEVICE_DATA PdoData
	)
{
	PIO_WORKITEM wi;
	KIRQL        irql;

	ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);

	VIRTUSB_KDPRINT("in VirtUsb_RootHubNotify\n");

	ASSERT(PdoData);

	wi = IoAllocateWorkItem(PdoData->Self);
	if(!NT_SUCCESS(IoAcquireRemoveLock(&PdoData->RemoveLock, wi)))
	{
		IoFreeWorkItem(wi);
		VIRTUSB_KDPRINT("failed to acquire remove lock\n");
		return;
	}
	KeAcquireSpinLock(&PdoData->Lock, &irql);
	if(PdoData->RootHubNotifyWorkItem)
	{
		KeReleaseSpinLock(&PdoData->Lock, irql);
		IoReleaseRemoveLock(&PdoData->RemoveLock, wi);
		IoFreeWorkItem(wi);
		return;
	}
	VIRTUSB_KDPRINT("*** root-hub: work item queued\n");
	IoQueueWorkItem(wi, VirtUsb_RootHubNotifyWorkItemCallback, DelayedWorkQueue, PdoData);
	PdoData->RootHubNotifyWorkItem = wi;
	KeReleaseSpinLock(&PdoData->Lock, irql);
}

VOID
VirtUsb_RootHubNotifyWorkItemCallback(
	__in     PDEVICE_OBJECT DeviceObject,
	__in_opt PVOID          Context
	)
{
	ULONG_PTR        bitArr[256 / (sizeof(ULONG_PTR) * 8)]; // 256 bits
	PDRIVER_CANCEL   cancelCallback;
	BOOLEAN          hasChanges;
	PIRP             notifyIrp = NULL;
	PPDO_DEVICE_DATA pdoData = Context;
	KIRQL            irql, cancelIrql;
	PIO_WORKITEM     wi;

	UNREFERENCED_PARAMETER(DeviceObject);

	ASSERT(KeGetCurrentIrql() < DISPATCH_LEVEL);

	VIRTUSB_KDPRINT("in VirtUsb_RootHubNotifyWorkItemCallback\n");

	ASSERT(pdoData);

	KeAcquireSpinLock(&pdoData->Lock, &irql);
	wi = pdoData->RootHubNotifyWorkItem;
	ASSERT(wi);
	pdoData->RootHubNotifyWorkItem = NULL;
	IoFreeWorkItem(wi);

	hasChanges = VirtUsb_ReportRootHubChanges(pdoData, bitArr);
	if(hasChanges && !IsListEmpty(&pdoData->RootHubRequests))
	{
		// search for an uncanceled notify irp
		while(TRUE)
		{
			PLIST_ENTRY entry = RemoveHeadList(&pdoData->RootHubRequests);
			notifyIrp = CONTAINING_RECORD(entry, IRP, Tail.Overlay.ListEntry);
			IoAcquireCancelSpinLock(&cancelIrql);
			cancelCallback = IoSetCancelRoutine(notifyIrp, NULL);
			if(notifyIrp->Cancel)
			{
				IoReleaseCancelSpinLock(cancelIrql);
				if(cancelCallback)
				{
					// this one was marked for cancelation but the cancel-routine was not called yet --
					// so we can use this one
					break;
				}
				else
				{
					InitializeListHead(entry);
				}
				notifyIrp = NULL;
			}
			else
			{
				IoReleaseCancelSpinLock(cancelIrql);
				// found an uncancelled irp
				break;
			}
		}
	}

	KeReleaseSpinLock(&pdoData->Lock, irql);

	// if we have something to report AND we have an pending irp for it, then complete that irp
	if(notifyIrp)
	{
		ULONG  len;
		PUCHAR buffer;
		PURB   notifyUrb = URB_FROM_IRP(notifyIrp);
		ASSERT(notifyUrb);
		len = notifyUrb->UrbBulkOrInterruptTransfer.TransferBufferLength;
		buffer = notifyUrb->UrbBulkOrInterruptTransfer.TransferBuffer;
		if(len > 32)
		{
			RtlZeroMemory(buffer + 32, len - 32);
			len = 32;
		}
		RtlCopyMemory(buffer, bitArr, len);
		VirtUsb_DereferenceUsbDevice((PUSBDEV_CONTEXT)notifyUrb->UrbHeader.UsbdDeviceHandle, notifyIrp, FALSE);
		notifyIrp->IoStatus.Status = STATUS_SUCCESS;
		notifyUrb->UrbHeader.Status = USBD_STATUS_SUCCESS;
		IoCompleteRequest(notifyIrp, IO_NO_INCREMENT);
		IoReleaseRemoveLock(&pdoData->RemoveLock, notifyIrp);
		VIRTUSB_KDPRINT("*** root-hub driver notification done\n");
	}

	IoReleaseRemoveLock(&pdoData->RemoveLock, wi);
}

VOID
VirtUsb_RootHubCancelStateNotifyIrp(
	__inout PDEVICE_OBJECT DeviceObject,
	__in    PIRP           Irp
	)
{
	PPDO_DEVICE_DATA pdoData = DeviceObject->DeviceExtension;
	PURB             urb;

	ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);

	VIRTUSB_KDPRINT("in VirtUsb_RootHubCancelStateNotifyIrp\n");

	ASSERT(pdoData);
	ASSERT(Irp);
	urb = (PURB)IoGetCurrentIrpStackLocation(Irp)->Parameters.Others.Argument1;
	ASSERT(urb);

	IoReleaseCancelSpinLock(DISPATCH_LEVEL);
	KeAcquireSpinLockAtDpcLevel(&pdoData->Lock);
	RemoveEntryList(&Irp->Tail.Overlay.ListEntry);
	KeReleaseSpinLockFromDpcLevel(&pdoData->Lock);
	VirtUsb_DereferenceUsbDevice((PUSBDEV_CONTEXT)urb->UrbHeader.UsbdDeviceHandle, Irp, FALSE);
	KeLowerIrql(Irp->CancelIrql);

	Irp->IoStatus.Status = STATUS_CANCELLED;
	urb->UrbHeader.Status = USBD_STATUS_CANCELED;
	Irp->IoStatus.Information = 0;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	IoReleaseRemoveLock(&pdoData->RemoveLock, Irp);
}

BOOLEAN
VirtUsb_ReportRootHubChanges(
	IN  PPDO_DEVICE_DATA PdoData,
	OUT PULONG_PTR       BitArr
	)
/*
	caller has lock
*/
{
	ULONG           i;
	PUSBHUB_CONTEXT rhub;
	BOOLEAN         hasChanges = FALSE;

	ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);

	VIRTUSB_KDPRINT("in VirtUsb_ReportRootHubChanges\n");

	ASSERT(PdoData);
	rhub = PdoData->RootHub;
	ASSERT(rhub);

	if(BitArr)
	{
		RtlZeroMemory(BitArr, 32);
	}

	for(i = 0; i < rhub->PortCount; i++)
	{
		if(rhub->Port[i].Change)
		{
			VIRTUSB_KDPRINT2("*** port %lu has changes: 0x%04hx\n", i + 1, rhub->Port[i].Change);

			hasChanges = TRUE;
			if(BitArr)
			{
				set_bit(i + 1, BitArr);
			}
		}
	}

	return hasChanges;
}
