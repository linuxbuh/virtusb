#include "virtusb.h"
#include "usbdev.h"

BOOLEAN
VirtUsb_SearchUsbDeviceRecursive(
	IN PUSBHUB_CONTEXT Hub,
	IN PUSBDEV_CONTEXT Device
	);

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, VirtUsb_InitializePortContext)
#pragma alloc_text(PAGE, VirtUsb_AllocateUsbDev)
#pragma alloc_text(PAGE, VirtUsb_AllocateUsbHub)
#pragma alloc_text(PAGE, VirtUsb_CheckConfDescBounds)
#pragma alloc_text(PAGE, VirtUsb_UsbDevParseConfDesc)
#pragma alloc_text(PAGE, VirtUsb_UsbDevParseAIfcDesc)
#endif // ALLOC_PRAGMA

BOOLEAN
VirtUsb_SearchUsbDeviceRecursive(
	IN PUSBHUB_CONTEXT Hub,
	IN PUSBDEV_CONTEXT Device
	)
/*
    IRQL = PASSIVE_LEVEL
*/
{
	PUSBDEV_CONTEXT d;
	ULONG           i;

	if((PUSBDEV_CONTEXT)Hub == Device)
		return TRUE;

	for(i = 0; i < Hub->PortCount; i++)
	{
		d = Hub->Port[i].UsbDevice;
		if(d)
		{
			if(d->IsHub)
			{
				if(VirtUsb_SearchUsbDeviceRecursive((PUSBHUB_CONTEXT)d, Device))
					return TRUE;
			}
			if(d == Device)
				return TRUE;
		}
	}

	return FALSE;
}

VOID
VirtUsb_InitializePortContext(
	OUT PPORT_CONTEXT   Port,
	IN  PUSBHUB_CONTEXT UsbHub,
	IN  ULONG           Index
	)
/*
    Set the port-context into a known good starting state
*/
{
	PAGED_CODE();

	VIRTUSB_KDPRINT("in VirtUsb_InitializePortContext\n");

	ASSERT(Port);
	ASSERT(UsbHub);
	ASSERT(UsbHub->ParentPdo);
	ASSERT(Index < UsbHub->PortCount);

	Port->ParentHub = UsbHub;
	Port->Index = Index;
	KeInitializeEvent(&Port->HubDriverTouchedEvent, NotificationEvent, TRUE);
}

VOID
VirtUsb_DestroyPortContext(
	IN PPORT_CONTEXT Port
	)
{
	VIRTUSB_KDPRINT("in VirtUsb_DestroyPortContext\n");

	ASSERT(Port);
	ASSERT(Port->ParentHub);
	ASSERT(Port->Index < Port->ParentHub->PortCount);

	if(Port->UsbDevice)
	{
		VirtUsb_FreeUsbDev(Port->UsbDevice);
	}

#if DBG
	Port->ParentHub = (PVOID)(ULONG_PTR)0xdeadf00d;
	Port->UsbDevice = (PVOID)(ULONG_PTR)0xdeadf00d;
#endif
}

NTSTATUS
VirtUsb_AllocateUsbDev(
	IN  PPDO_DEVICE_DATA PdoData,
	OUT PUSBDEV_CONTEXT  *UsbDev,
	IN  PPORT_CONTEXT    Port
	)
{
	PUSBDEV_CONTEXT dev;

	PAGED_CODE();

	VIRTUSB_KDPRINT("in VirtUsb_AllocateUsbDev\n");

	ASSERT(PdoData);
	ASSERT(UsbDev);
	ASSERT(!Port || (Port->ParentHub && (Port->ParentHub->RootHub) && (Port->ParentHub->ParentPdo == PdoData->Self)));

	*UsbDev = NULL;

	dev = ExAllocatePoolWithTag(NonPagedPool,
	                            sizeof(USBHUB_CONTEXT), // we always allocate enough space for hubs
	                            VIRTUSB_DEVICE_POOL_TAG);
	if(!dev)
	{
		VIRTUSB_KDPRINT("VirtUsb_AllocateUsbDev failed\n");
		return STATUS_INSUFFICIENT_RESOURCES;
	}
	RtlZeroMemory(dev, sizeof(USBHUB_CONTEXT));

	dev->ParentPdo = PdoData->Self;
	if(Port)
	{
		dev->ParentPort = Port;
		dev->ParentHub = Port->ParentHub;
		dev->RootHub = Port->ParentHub->RootHub;
	}
	dev->ActiveConfiguration = MAXULONG;
	dev->EndpointZero.Device = dev;
	dev->EndpointZero.IsEP0 = TRUE;
#if DBG
	dev->EndpointZero.AltSetting          = (PVOID)(ULONG_PTR)0xdeadf00d;
	dev->EndpointZero.Descriptor          = (PVOID)(ULONG_PTR)0xdeadf00d;
#endif

	KeInitializeSpinLock(&dev->Lock);
	KeInitializeEvent(&dev->DeviceRemovableEvent, NotificationEvent, TRUE);

	*UsbDev = dev;
	return STATUS_SUCCESS;
}

NTSTATUS
VirtUsb_AllocateUsbHub(
	IN  PPDO_DEVICE_DATA PdoData,
	OUT PUSBHUB_CONTEXT  *UsbHub,
	IN  PPORT_CONTEXT    Port,
	IN  ULONG            PortCount
	)
{
	PUSBHUB_CONTEXT hub;
	NTSTATUS        status;

	PAGED_CODE();

	VIRTUSB_KDPRINT("in VirtUsb_AllocateUsbHub\n");

	ASSERT(PdoData);
	ASSERT(UsbHub);
	ASSERT(!Port || (Port->ParentHub && (Port->ParentHub->RootHub) && (Port->ParentHub->ParentPdo == PdoData->Self)));
	ASSERT(PortCount);
	ASSERT(PortCount <= 255); // see usb spec 2.0

	*UsbHub = NULL;

	status = VirtUsb_AllocateUsbDev(PdoData, (PUSBDEV_CONTEXT *)&hub, Port);
	if(!NT_SUCCESS(status))
	{
		VIRTUSB_KDPRINT("VirtUsb_AllocateUsbHub failed\n");
		return status;
	}

	status = VirtUsb_ConvertToUsbHub((PUSBDEV_CONTEXT)hub, PortCount);
	if(!NT_SUCCESS(status))
	{
		goto Err_DestroyDev;
	}

	*UsbHub = hub;
	return status;

Err_DestroyDev:
	VIRTUSB_KDPRINT("VirtUsb_AllocateUsbHub failed\n");
	VirtUsb_FreeUsbDev((PUSBDEV_CONTEXT)hub);
	return status;
}

VOID
VirtUsb_DestroyUsbDev(
	IN PUSBDEV_CONTEXT UsbDev,
	IN BOOLEAN         ReUse
	)
{
	ULONG i;

	VIRTUSB_KDPRINT("in VirtUsb_DestroyUsbDev\n");

	ASSERT(UsbDev);
	ASSERT(!UsbDev->RefCount);

	if(UsbDev->IsHub)
	{
		PUSBHUB_CONTEXT hub = (PUSBHUB_CONTEXT)UsbDev;

		ASSERT(hub->PortCount);
		ASSERT(hub->Port);

		for(i = hub->PortCount; i; i--)
		{
			VirtUsb_DestroyPortContext(&hub->Port[i - 1]);
		}

		VIRTUSB_KDPRINT("free port array\n");
		ExFreePoolWithTag(hub->Port, VIRTUSB_DEVICE_POOL_TAG);

		if(hub->HubDescriptor)
		{
			VIRTUSB_KDPRINT("free hub descriptor\n");
			ExFreePoolWithTag(hub->HubDescriptor, VIRTUSB_DEVICE_POOL_TAG);
		}
	}

	if(UsbDev->Configuration)
	{
		for(i = 0; i < UsbDev->ConfigurationCount; i++)
		{
			PCONF_CONTEXT conf = &UsbDev->Configuration[i];
			if(conf->Interface)
			{
				ULONG j;
				for(j = 0; j < conf->InterfaceCount; j++)
				{
					PIFC_CONTEXT ifc = &conf->Interface[j];
					if(ifc->AltSetting)
					{
						ULONG k;
						for(k = 0; k < ifc->AltSettingCount; k++)
						{
							PAIFC_CONTEXT aifc = &ifc->AltSetting[k];
							if(aifc->Endpoint)
							{
#if DBG
								ULONG l;
								for(l = 0; l < aifc->EndpointCount; l++)
								{
									PPIPE_CONTEXT ep = &aifc->Endpoint[l];
									ep->AltSetting = (PVOID)(ULONG_PTR)0xdeadf00d;
									ep->Device = (PVOID)(ULONG_PTR)0xdeadf00d;
									ep->Descriptor = (PVOID)(ULONG_PTR)0xdeadf00d;
								}
#endif

								VIRTUSB_KDPRINT3("free config[%lu] interface[%lu] alt-setting[%lu] endpoint array\n", i, j, k);
								ExFreePoolWithTag(aifc->Endpoint, VIRTUSB_DEVICE_POOL_TAG);
							}

#if DBG
							aifc->Interface = (PVOID)(ULONG_PTR)0xdeadf00d;
							aifc->Endpoint = (PVOID)(ULONG_PTR)0xdeadf00d;
							aifc->Descriptor = (PVOID)(ULONG_PTR)0xdeadf00d;
#endif
						}

						VIRTUSB_KDPRINT2("free config[%lu] interface[%lu] alt-setting array\n", i, j);
						ExFreePoolWithTag(ifc->AltSetting, VIRTUSB_DEVICE_POOL_TAG);
					}

#if DBG
					ifc->Configuration = (PVOID)(ULONG_PTR)0xdeadf00d;
					ifc->AltSetting = (PVOID)(ULONG_PTR)0xdeadf00d;
					ifc->Descriptor = (PVOID)(ULONG_PTR)0xdeadf00d;
#endif
				}

				VIRTUSB_KDPRINT1("free config[%lu] interface array\n", i);
				ExFreePoolWithTag(conf->Interface, VIRTUSB_DEVICE_POOL_TAG);
			}

			if(conf->Descriptor)
			{
				VIRTUSB_KDPRINT1("free config[%lu] descriptor\n", i);
				ExFreePoolWithTag(conf->Descriptor, VIRTUSB_DEVICE_POOL_TAG);
			}

#if DBG
			conf->Device = (PVOID)(ULONG_PTR)0xdeadf00d;
			conf->Interface = (PVOID)(ULONG_PTR)0xdeadf00d;
			conf->Descriptor = (PVOID)(ULONG_PTR)0xdeadf00d;
#endif
		}

		VIRTUSB_KDPRINT("free config descriptor array\n");
		ExFreePoolWithTag(UsbDev->Configuration, VIRTUSB_DEVICE_POOL_TAG);
	}

	if(UsbDev->Descriptor)
	{
		VIRTUSB_KDPRINT("free device descriptor\n");
		ExFreePoolWithTag(UsbDev->Descriptor, VIRTUSB_DEVICE_POOL_TAG);
	}

#if DBG
	if(!ReUse)
	{
		UsbDev->ParentPdo     = (PVOID)(ULONG_PTR)0xdeadf00d;
		UsbDev->ParentHub     = (PVOID)(ULONG_PTR)0xdeadf00d;
		UsbDev->RootHub       = (PVOID)(ULONG_PTR)0xdeadf00d;
		UsbDev->ParentPort    = (PVOID)(ULONG_PTR)0xdeadf00d;
		UsbDev->HcdContext    = (PVOID)(ULONG_PTR)0xdeadf00d;
		UsbDev->HcdContext2   = (PVOID)(ULONG_PTR)0xdeadf00d;
		UsbDev->Descriptor    = (PVOID)(ULONG_PTR)0xdeadf00d;
		UsbDev->Configuration = (PVOID)(ULONG_PTR)0xdeadf00d;
		UsbDev->EndpointZero.Device = (PVOID)(ULONG_PTR)0xdeadf00d;
		if(UsbDev->IsHub)
		{
			((PUSBHUB_CONTEXT)UsbDev)->HubDescriptor = (PVOID)(ULONG_PTR)0xdeadf00d;
			((PUSBHUB_CONTEXT)UsbDev)->Port          = (PVOID)(ULONG_PTR)0xdeadf00d;
		}
	}
#endif

	if(ReUse)
	{
		UsbDev->Descriptor    = NULL;
		UsbDev->Configuration = NULL;
		UsbDev->IsHub = FALSE;
		UsbDev->State = DeviceAttached;
		UsbDev->Ready = FALSE;
		UsbDev->Address = 0x00;
		UsbDev->ConfigurationCount = 0;
		UsbDev->ActiveConfiguration = MAXULONG;
	}
}

VOID
VirtUsb_FreeUsbDev(
	IN PUSBDEV_CONTEXT UsbDev
	)
{
	VIRTUSB_KDPRINT("in VirtUsb_FreeUsbDev\n");
	ASSERT(UsbDev);
	ASSERT(!UsbDev->RefCount);
	VirtUsb_DestroyUsbDev(UsbDev, FALSE);
	ExFreePoolWithTag(UsbDev, VIRTUSB_DEVICE_POOL_TAG);
}

NTSTATUS
VirtUsb_ConvertToUsbHub(
	IN OUT PUSBDEV_CONTEXT UsbDev,
	IN     ULONG           PortCount
	)
{
	PUSBHUB_CONTEXT hub = (PUSBHUB_CONTEXT)UsbDev;
	PPORT_CONTEXT   ports;
	ULONG           i;

	PAGED_CODE();

	VIRTUSB_KDPRINT("in VirtUsb_ConvertToUsbHub\n");

	ASSERT(UsbDev);
	ASSERT(UsbDev->ParentPdo);
	ASSERT(!UsbDev->IsHub);
	ASSERT(PortCount);
	ASSERT(PortCount <= 255); // see usb spec 2.0

	ports = ExAllocatePoolWithTag(NonPagedPool,
	                              sizeof(PORT_CONTEXT) * PortCount,
	                              VIRTUSB_DEVICE_POOL_TAG);
	if(!ports)
	{
		VIRTUSB_KDPRINT("VirtUsb_ConvertToUsbHub failed\n");
		return STATUS_INSUFFICIENT_RESOURCES;
	}
	RtlZeroMemory(ports, sizeof(PORT_CONTEXT) * PortCount);

	hub->PortCount = PortCount;
	for(i = 0; i < PortCount; i++)
	{
		VirtUsb_InitializePortContext(&ports[i], hub, i);
	}

	hub->Port = ports;
	KeMemoryBarrier();
	hub->IsHub = TRUE;
	if(!hub->ParentPort) // (if it is not connected to any port, then it must be the root-hub)
	{
		hub->RootHub = hub;
		// root-hub starts in ADDRESS state
		hub->State = Addressed;
		hub->Ready = TRUE;
		hub->Address = 1;
		hub->AssignedAddress = 1;
	}

	return STATUS_SUCCESS;
}

NTSTATUS
VirtUsb_UsbDevInitConfigArray(
	IN PUSBDEV_CONTEXT UsbDev,
	IN ULONG           NumConfigs
	)
/*
    Allocates the UsbDev->Configuration array and initializes
    its elements.
    If the array is already allocated, then this routine checks
    if the number of configs is correct. The number must not
    change -- a device does not (and must not) change its config count.
    This routine takes care of locking.

    IRQL = PASSIVE_LEVEL
*/
{
	KIRQL    irql;
	NTSTATUS status = STATUS_SUCCESS;

	ASSERT(KeGetCurrentIrql() < DISPATCH_LEVEL);

	VIRTUSB_KDPRINT("in VirtUsb_UsbDevInitConfigArray\n");

	ASSERT(UsbDev);

	KeAcquireSpinLock(&UsbDev->Lock, &irql);
	if(UsbDev->Configuration) // already done?
	{
		if(NumConfigs != UsbDev->ConfigurationCount)
		{
			// num configs must not change!
			status = STATUS_UNSUCCESSFUL;
		}
	}
	else
	{
		if(NumConfigs)
		{
			ULONG         len = sizeof(CONF_CONTEXT) * NumConfigs;
			PCONF_CONTEXT arr;
			KeReleaseSpinLock(&UsbDev->Lock, irql);
			arr = ExAllocatePoolWithTag(NonPagedPool,
			                            len,
			                            VIRTUSB_DEVICE_POOL_TAG);
			if(arr)
			{
				RtlZeroMemory(arr, len);
			}
			KeAcquireSpinLock(&UsbDev->Lock, &irql);
			if(arr)
			{
				// we released the spinlock for allocation,
				// so we have to check if someone other was
				// faster than we
				if(UsbDev->Configuration)
				{
					if(NumConfigs != UsbDev->ConfigurationCount)
					{
						status = STATUS_UNSUCCESSFUL;
					}
					KeReleaseSpinLock(&UsbDev->Lock, irql);
					ExFreePoolWithTag(arr, VIRTUSB_DEVICE_POOL_TAG);
					return status;
				}
				else
				{
					ULONG i;
					UsbDev->Configuration = arr;
					UsbDev->ConfigurationCount = NumConfigs;
					// Initialize array elements
					for(i = 0; i < NumConfigs; i++)
					{
						arr[i].Device = UsbDev;
						arr[i].Index = i;
					}
				}
			}
			else
			{
				// failed -- we check if some other thread
				// has had success while we released the spinlock...
				if(NumConfigs != UsbDev->ConfigurationCount)
				{
					// ... if not -> fail
					status = STATUS_INSUFFICIENT_RESOURCES;
				}
			}
		}
	}
	KeReleaseSpinLock(&UsbDev->Lock, irql);
	return status;
}

NTSTATUS
VirtUsb_UsbDevInitIfcArray(
	IN PCONF_CONTEXT Conf,
	IN ULONG         NumIfcs
	)
/*
    Allocates the Conf->Interface array and initializes
    its elements.
    If the array is already allocated, then this routine checks
    if the number of interfaces is correct. The number must not
    change -- a device does not (and must not) change its
    interface count for a given config.
    This routine takes care of locking.

    IRQL = PASSIVE_LEVEL
*/
{
	KIRQL           irql;
	NTSTATUS        status = STATUS_SUCCESS;
	PUSBDEV_CONTEXT dev;

	ASSERT(KeGetCurrentIrql() < DISPATCH_LEVEL);

	VIRTUSB_KDPRINT("in VirtUsb_UsbDevInitIfcArray\n");

	ASSERT(Conf);
	dev = Conf->Device;
	ASSERT(dev);

	KeAcquireSpinLock(&dev->Lock, &irql);
	if(Conf->Interface) // already done?
	{
		if(NumIfcs != Conf->InterfaceCount)
		{
			// num ifcs must not change!
			status = STATUS_UNSUCCESSFUL;
		}
	}
	else
	{
		if(NumIfcs)
		{
			ULONG        len = sizeof(IFC_CONTEXT) * NumIfcs;
			PIFC_CONTEXT arr;
			KeReleaseSpinLock(&dev->Lock, irql);
			arr = ExAllocatePoolWithTag(NonPagedPool,
			                            len,
			                            VIRTUSB_DEVICE_POOL_TAG);
			if(arr)
			{
				RtlZeroMemory(arr, len);
			}
			KeAcquireSpinLock(&dev->Lock, &irql);
			if(arr)
			{
				// we released the spinlock for allocation,
				// so we have to check if someone other was
				// faster than we
				if(Conf->Interface)
				{
					if(NumIfcs != Conf->InterfaceCount)
					{
						status = STATUS_UNSUCCESSFUL;
					}
					KeReleaseSpinLock(&dev->Lock, irql);
					ExFreePoolWithTag(arr, VIRTUSB_DEVICE_POOL_TAG);
					return status;
				}
				else
				{
					ULONG i;
					Conf->Interface = arr;
					Conf->InterfaceCount = NumIfcs;
					// Initialize array elements
					for(i = 0; i < NumIfcs; i++)
					{
						arr[i].Configuration = Conf;
						arr[i].Index = i;
					}
				}
			}
			else
			{
				// failed -- we check if some other thread
				// has had success while we released the spinlock...
				if(NumIfcs != Conf->InterfaceCount)
				{
					// ... if not -> fail
					status = STATUS_INSUFFICIENT_RESOURCES;
				}
			}
		}
	}
	KeReleaseSpinLock(&dev->Lock, irql);
	return status;
}

NTSTATUS
VirtUsb_UsbDevInitAIfcArray(
	IN PIFC_CONTEXT Ifc,
	IN ULONG        NumAIfcs
	)
/*
    Allocates the Ifc->AltSetting array and initializes
    its elements.
    If the array is already allocated, then this routine checks
    if the number of alternates is correct. The number must not
    change -- a device does not (and must not) change its
    alternate setting count for a given interface.
    This routine takes care of locking.

    IRQL = PASSIVE_LEVEL
*/
{
	KIRQL           irql;
	NTSTATUS        status = STATUS_SUCCESS;
	PUSBDEV_CONTEXT dev;

	ASSERT(KeGetCurrentIrql() < DISPATCH_LEVEL);

	VIRTUSB_KDPRINT("in VirtUsb_UsbDevInitAIfcArray\n");

	ASSERT(Ifc);
	ASSERT(Ifc->Configuration);
	dev = Ifc->Configuration->Device;
	ASSERT(dev);

	KeAcquireSpinLock(&dev->Lock, &irql);
	if(Ifc->AltSetting) // already done?
	{
		if(NumAIfcs != Ifc->AltSettingCount)
		{
			// num alts must not change!
			status = STATUS_UNSUCCESSFUL;
		}
	}
	else
	{
		if(NumAIfcs)
		{
			ULONG         len = sizeof(AIFC_CONTEXT) * NumAIfcs;
			PAIFC_CONTEXT arr;
			KeReleaseSpinLock(&dev->Lock, irql);
			arr = ExAllocatePoolWithTag(NonPagedPool,
			                            len,
			                            VIRTUSB_DEVICE_POOL_TAG);
			if(arr)
			{
				RtlZeroMemory(arr, len);
			}
			KeAcquireSpinLock(&dev->Lock, &irql);
			if(arr)
			{
				// we released the spinlock for allocation,
				// so we have to check if someone other was
				// faster than we
				if(Ifc->AltSetting)
				{
					if(NumAIfcs != Ifc->AltSettingCount)
					{
						status = STATUS_UNSUCCESSFUL;
					}
					KeReleaseSpinLock(&dev->Lock, irql);
					ExFreePoolWithTag(arr, VIRTUSB_DEVICE_POOL_TAG);
					return status;
				}
				else
				{
					ULONG i;
					Ifc->AltSetting = arr;
					Ifc->AltSettingCount = NumAIfcs;
					// Initialize array elements
					for(i = 0; i < NumAIfcs; i++)
					{
						arr[i].Interface = Ifc;
						arr[i].Index = i;
					}
				}
			}
			else
			{
				// failed -- we check if some other thread
				// has had success while we released the spinlock...
				if(NumAIfcs != Ifc->AltSettingCount)
				{
					// ... if not -> fail
					status = STATUS_INSUFFICIENT_RESOURCES;
				}
			}
		}
	}
	KeReleaseSpinLock(&dev->Lock, irql);
	return status;
}

NTSTATUS
VirtUsb_UsbDevInitEpArray(
	IN PAIFC_CONTEXT AIfc,
	IN ULONG         NumEps
	)
/*
    Allocates the AIfc->Endpoint array and initializes
    its elements.
    If the array is already allocated, then this routine checks
    if the number of endpoints is correct. The number must not
    change -- a device does not (and must not) change its
    endpoint count for a given alternate setting.
    This routine takes care of locking.

    IRQL = PASSIVE_LEVEL
*/
{
	KIRQL           irql;
	NTSTATUS        status = STATUS_SUCCESS;
	PUSBDEV_CONTEXT dev;

	ASSERT(KeGetCurrentIrql() < DISPATCH_LEVEL);

	VIRTUSB_KDPRINT("in VirtUsb_UsbDevInitEpArray\n");

	ASSERT(AIfc);
	ASSERT(AIfc->Interface);
	ASSERT(AIfc->Interface->Configuration);
	dev = AIfc->Interface->Configuration->Device;
	ASSERT(dev);

	KeAcquireSpinLock(&dev->Lock, &irql);
	if(AIfc->Endpoint) // already done?
	{
		if(NumEps != AIfc->EndpointCount)
		{
			// num endpoints must not change!
			status = STATUS_UNSUCCESSFUL;
		}
	}
	else
	{
		if(NumEps)
		{
			ULONG         len = sizeof(PIPE_CONTEXT) * NumEps;
			PPIPE_CONTEXT arr;
			KeReleaseSpinLock(&dev->Lock, irql);
			arr = ExAllocatePoolWithTag(NonPagedPool,
			                            len,
			                            VIRTUSB_DEVICE_POOL_TAG);
			if(arr)
			{
				RtlZeroMemory(arr, len);
			}
			KeAcquireSpinLock(&dev->Lock, &irql);
			if(arr)
			{
				// we released the spinlock for allocation,
				// so we have to check if someone other was
				// faster than we
				if(AIfc->Endpoint)
				{
					if(NumEps != AIfc->EndpointCount)
					{
						status = STATUS_UNSUCCESSFUL;
					}
					KeReleaseSpinLock(&dev->Lock, irql);
					ExFreePoolWithTag(arr, VIRTUSB_DEVICE_POOL_TAG);
					return status;
				}
				else
				{
					ULONG i;
					AIfc->Endpoint = arr;
					AIfc->EndpointCount = NumEps;
					// Initialize array elements
					for(i = 0; i < NumEps; i++)
					{
						arr[i].Device = dev;
						arr[i].AltSetting = AIfc;
						arr[i].Index = i;
					}
				}
			}
			else
			{
				// failed -- we check if some other thread
				// has had success while we released the spinlock...
				if(NumEps != AIfc->EndpointCount)
				{
					// ... if not -> fail
					status = STATUS_INSUFFICIENT_RESOURCES;
				}
			}
		}
	}
	KeReleaseSpinLock(&dev->Lock, irql);
	return status;
}

NTSTATUS
VirtUsb_UsbDevInitDesc(
	IN     PUSBDEV_CONTEXT UsbDev,
	IN OUT PUCHAR          *DstDesc,
	IN     CONST UCHAR     *SrcDesc
	)
/*
    Allocates and initializes the descriptor pointed by DstDesc
    with the data pointed by SrcDesc.
    If the descriptor is already allocated, then this routine checks
    if size and content of SrcDesc and DstDesc is equal.
    The size is read from the descriptor itself. Config descriptors are handled
    properly (wTotalLength is read instead of bLength).
    This routine takes care of locking -- therefore UsbDev is required.

    IRQL = PASSIVE_LEVEL
*/
{
	KIRQL    irql;
	NTSTATUS status = STATUS_SUCCESS;
	ULONG    srcLen, dstLen;
	PUCHAR   arr = NULL;
	BOOLEAN  freeArr = FALSE;

	ASSERT(KeGetCurrentIrql() < DISPATCH_LEVEL);

	VIRTUSB_KDPRINT("in VirtUsb_UsbDevInitDesc\n");

	ASSERT(UsbDev);
	ASSERT(DstDesc);
	ASSERT(SrcDesc);

	srcLen = *SrcDesc;
	if(srcLen < 2)
	{
		return STATUS_INVALID_PARAMETER;
	}
	if((SrcDesc[1] == USB_CONFIGURATION_DESCRIPTOR_TYPE) ||
	   (SrcDesc[1] == USB_OTHER_SPEED_CONFIGURATION_DESCRIPTOR_TYPE))
	{
		if(srcLen < 4)
		{
			return STATUS_INVALID_PARAMETER;
		}
		srcLen = (ULONG)SrcDesc[2] | ((ULONG)SrcDesc[3] << 8);
		if(srcLen < 4)
		{
			return STATUS_INVALID_PARAMETER;
		}
	}

	KeAcquireSpinLock(&UsbDev->Lock, &irql);
	if(*DstDesc) // already done?
	{
		goto Compare;
	}
	KeReleaseSpinLock(&UsbDev->Lock, irql);
	arr = ExAllocatePoolWithTag(NonPagedPool,
	                            srcLen,
	                            VIRTUSB_DEVICE_POOL_TAG);
	if(arr)
	{
		RtlCopyMemory(arr, SrcDesc, srcLen);
	}
	KeAcquireSpinLock(&UsbDev->Lock, &irql);
	if(arr)
	{
		// we released the spinlock for allocation,
		// so we have to check if someone other was
		// faster than we
		if(*DstDesc)
		{
			freeArr = TRUE; // we can't free now, because we are holding the spinlock
			goto Compare;
		}
		*DstDesc = arr;
	}
	else
	{
		// failed -- we check if some other thread
		// has had success while we released the spinlock...
		if(*DstDesc)
		{
			goto Compare;
		}
		// ... if not -> fail
		status = STATUS_INSUFFICIENT_RESOURCES;
	}
End:
	KeReleaseSpinLock(&UsbDev->Lock, irql);
	if(freeArr)
	{
		ExFreePoolWithTag(arr, VIRTUSB_DEVICE_POOL_TAG);
	}
	return status;

Compare:
	dstLen = **DstDesc;
	if(dstLen < 2)
	{
		status = STATUS_UNSUCCESSFUL;
		goto End;
	}
	if(((*DstDesc)[1] == USB_CONFIGURATION_DESCRIPTOR_TYPE) ||
	   ((*DstDesc)[1] == USB_OTHER_SPEED_CONFIGURATION_DESCRIPTOR_TYPE))
	{
		if(dstLen < 4)
		{
			status = STATUS_UNSUCCESSFUL;
			goto End;
		}
		dstLen = (ULONG)(*DstDesc)[2] | ((ULONG)(*DstDesc)[3] << 8);
	}
	if(dstLen != srcLen)
	{
		status = STATUS_UNSUCCESSFUL;
		goto End;
	}
	if(srcLen != RtlCompareMemory(*DstDesc, SrcDesc, srcLen))
	{
		status = STATUS_UNSUCCESSFUL;
	}
	goto End;
}

BOOLEAN
VirtUsb_CheckConfDescBounds(
	IN PUSB_CONFIGURATION_DESCRIPTOR Desc
	)
/*
    This routine checks if the wTotalLength field matches the sum
    of the lengths of all the descriptors. TRUE means everything is ok.

    IRQL = PASSIVE_LEVEL
*/
{
	PUCHAR d = (PUCHAR)Desc;

	PAGED_CODE();

	VIRTUSB_KDPRINT("in VirtUsb_CheckConfDescBounds\n");

	ASSERT(Desc);
	ASSERT(((ULONG_PTR)Desc & (TYPE_ALIGNMENT(USHORT) - 1)) == 0); // 2-byte alignment-check

	if(Desc->bLength < 4 || Desc->wTotalLength < sizeof(USB_CONFIGURATION_DESCRIPTOR))
	{
		VIRTUSB_KDPRINT("desc is too small\n");
		return FALSE;
	}
	if((Desc->bDescriptorType != USB_CONFIGURATION_DESCRIPTOR_TYPE) &&
	   (Desc->bDescriptorType != USB_OTHER_SPEED_CONFIGURATION_DESCRIPTOR_TYPE))
	{
		VIRTUSB_KDPRINT("desc is not a config desc\n");
		return FALSE;
	}

	while(d < (PUCHAR)Desc + Desc->wTotalLength)
	{
		if(*d < sizeof(USB_COMMON_DESCRIPTOR))
		{
			VIRTUSB_KDPRINT("found an invalid desc\n");
			return FALSE;
		}
		d += *d;
	}

	return d == (PUCHAR)Desc + Desc->wTotalLength;
}

NTSTATUS
VirtUsb_UsbDevParseConfDesc(
	IN PCONF_CONTEXT Conf
	)
/*
	NOTE: You should check the descriptor with VirtUsb_CheckConfDescBounds first.
*/
{
	PUSB_CONFIGURATION_DESCRIPTOR cd;
	NTSTATUS                      status = STATUS_SUCCESS;
	ULONG                         i, j, ifcCount = 0, ifcCapacity = 4;
	PUCHAR                        d;

	struct _IFC_STATS
	{
		struct _AIFC_STATS
		{
			PUSB_INTERFACE_DESCRIPTOR ptr;
			UCHAR num;
		} *aifc;
		PUSB_INTERFACE_DESCRIPTOR ptr;
		ULONG aifcCount;
		ULONG aifcCapacity;
		UCHAR num;
	} *ifc;

	PAGED_CODE();

	VIRTUSB_KDPRINT("in VirtUsb_UsbDevParseConfDesc\n");

	ASSERT(Conf);

	cd = Conf->Descriptor;
	if(!cd)
	{
		VIRTUSB_KDPRINT("desc is not known yet\n");
		return STATUS_UNSUCCESSFUL;
	}
	if(cd->bLength < 4 || cd->wTotalLength < sizeof(USB_CONFIGURATION_DESCRIPTOR))
	{
		VIRTUSB_KDPRINT("desc is too small\n");
		return STATUS_UNSUCCESSFUL;
	}
	if((cd->bDescriptorType != USB_CONFIGURATION_DESCRIPTOR_TYPE) &&
	   (cd->bDescriptorType != USB_OTHER_SPEED_CONFIGURATION_DESCRIPTOR_TYPE))
	{
		VIRTUSB_KDPRINT("desc is not a config desc\n");
		return STATUS_UNSUCCESSFUL;
	}
	if(!Conf->Interface)
	{
		status = VirtUsb_UsbDevInitIfcArray(Conf, cd->bNumInterfaces);
		if(!NT_SUCCESS(status))
		{
			return status;
		}
	}
	if(Conf->InterfaceCount != cd->bNumInterfaces)
	{
		VIRTUSB_KDPRINT("interface count mismatch\n");
		return STATUS_UNSUCCESSFUL;
	}

	ifc = ExAllocatePoolWithTag(PagedPool,
	                            sizeof(struct _IFC_STATS) * ifcCapacity,
	                            VIRTUSB_POOL_TAG);
	if(!ifc)
	{
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	d = (PUCHAR)cd + cd->bLength;
	while(d < (PUCHAR)cd + cd->wTotalLength)
	{
		if(d[1] == USB_INTERFACE_DESCRIPTOR_TYPE)
		{
			PUSB_INTERFACE_DESCRIPTOR idesc = (PUSB_INTERFACE_DESCRIPTOR)d;

			// lookup bInterfaceNumber
			for(i = 0; i < ifcCount; i++)
			{
				if(ifc[i].num == idesc->bInterfaceNumber)
				{
					// lookup bAlternateSetting
					for(j = 0; j < ifc[i].aifcCount; j++)
					{
						if(ifc[i].aifc[j].num == idesc->bAlternateSetting)
						{
							VIRTUSB_KDPRINT2("interface#%hu/alt#%hu is defined more than once\n", (USHORT)idesc->bInterfaceNumber, (USHORT)idesc->bAlternateSetting);
							goto End;
						}
					}

					// check if we have to reallocate ifc[i].aifc
					ASSERT(ifc[i].aifcCount <= ifc[i].aifcCapacity);
					if(ifc[i].aifcCount == ifc[i].aifcCapacity)
					{
						struct _AIFC_STATS *newArr;
						ifc[i].aifcCapacity <<= 1;
						ASSERT(ifc[i].aifcCapacity);
						ASSERT(ifc[i].aifcCapacity <= 256);
						newArr = ExAllocatePoolWithTag(PagedPool,
						                               sizeof(struct _AIFC_STATS) * ifc[i].aifcCapacity,
						                               VIRTUSB_POOL_TAG);
						if(!newArr)
						{
							status = STATUS_INSUFFICIENT_RESOURCES;
							goto End;
						}
						RtlCopyMemory(newArr, ifc[i].aifc, sizeof(struct _AIFC_STATS) * ifc[i].aifcCount);
						ExFreePoolWithTag(ifc[i].aifc, VIRTUSB_POOL_TAG);
						ifc[i].aifc = newArr;
					}

					ifc[i].aifc[ifc[i].aifcCount].num = idesc->bAlternateSetting;
					ifc[i].aifc[ifc[i].aifcCount].ptr = idesc;
					ifc[i].aifcCount++;
					goto ContinueOuterLoop;
				}
			}

			// check if we have to reallocate ifc
			ASSERT(ifcCount <= ifcCapacity);
			if(ifcCount == ifcCapacity)
			{
				struct _IFC_STATS *newArr;
				ifcCapacity <<= 1;
				ASSERT(ifcCapacity);
				ASSERT(ifcCapacity <= 256);
				newArr = ExAllocatePoolWithTag(PagedPool,
				                               sizeof(struct _IFC_STATS) * ifcCapacity,
				                               VIRTUSB_POOL_TAG);
				if(!newArr)
				{
					status = STATUS_INSUFFICIENT_RESOURCES;
					goto End;
				}
				RtlCopyMemory(newArr, ifc, sizeof(struct _IFC_STATS) * ifcCount);
				ExFreePoolWithTag(ifc, VIRTUSB_POOL_TAG);
				ifc = newArr;
			}

			ifc[ifcCount].aifcCapacity = 4;
			ifc[ifcCount].aifcCount = 1;
			ifc[ifcCount].aifc = ExAllocatePoolWithTag(PagedPool,
			                                           sizeof(struct _AIFC_STATS) * ifc[ifcCount].aifcCapacity,
			                                           VIRTUSB_POOL_TAG);
			if(!ifc[ifcCount].aifc)
			{
				status = STATUS_INSUFFICIENT_RESOURCES;
				goto End;
			}
			ifc[ifcCount].aifc[0].num = idesc->bAlternateSetting;
			ifc[ifcCount].aifc[0].ptr = idesc;
			ifc[ifcCount].num = idesc->bInterfaceNumber;
			ifc[ifcCount].ptr = idesc;
			ifcCount++;
		}
ContinueOuterLoop:
		d += *d;
	}

	if(Conf->InterfaceCount != ifcCount)
	{
		VIRTUSB_KDPRINT("interface count mismatch\n");
		status = STATUS_UNSUCCESSFUL;
		goto End;
	}

	for(i = 0; i < ifcCount; i++)
	{
		ASSERT(ifc[i].aifcCount);
		Conf->Interface[i].Descriptor = ifc[i].ptr;
		status = VirtUsb_UsbDevInitAIfcArray(&Conf->Interface[i], ifc[i].aifcCount);
		if(!NT_SUCCESS(status))
		{
			goto End;
		}
		for(j = 0; j < ifc[i].aifcCount; j++)
		{
			Conf->Interface[i].AltSetting[j].Descriptor = ifc[i].aifc[j].ptr;
			status = VirtUsb_UsbDevParseAIfcDesc(&Conf->Interface[i].AltSetting[j]);
			if(!NT_SUCCESS(status))
			{
				goto End;
			}
		}
	}

End:
	for(i = 0; i < ifcCount; i++)
	{
		if(ifc[i].aifc)
		{
			ExFreePoolWithTag(ifc[i].aifc, VIRTUSB_POOL_TAG);
		}
	}
	ExFreePoolWithTag(ifc, VIRTUSB_POOL_TAG);
	return status;
}

NTSTATUS
VirtUsb_UsbDevParseAIfcDesc(
	IN PAIFC_CONTEXT AIfc
	)
{
	PUSB_CONFIGURATION_DESCRIPTOR cd;
	PUSB_INTERFACE_DESCRIPTOR     idesc;
	NTSTATUS                      status = STATUS_SUCCESS;
	ULONG                         i, epCount = 0, epCapacity = 4;
	PUCHAR                        d;

	struct _EP_STATS
	{
		PUSB_ENDPOINT_DESCRIPTOR ptr;
		UCHAR adr;
	} *ep;

	PAGED_CODE();

	VIRTUSB_KDPRINT("in VirtUsb_UsbDevParseAIfcDesc\n");

	ASSERT(AIfc);
	ASSERT(AIfc->Interface);
	ASSERT(AIfc->Interface->Configuration);

	cd = AIfc->Interface->Configuration->Descriptor;
	idesc = AIfc->Descriptor;
	if(!idesc || !cd)
	{
		VIRTUSB_KDPRINT("desc is not known yet\n");
		return STATUS_UNSUCCESSFUL;
	}
	if(idesc->bLength < sizeof(USB_INTERFACE_DESCRIPTOR))
	{
		VIRTUSB_KDPRINT("desc is too small\n");
		return STATUS_UNSUCCESSFUL;
	}
	if(idesc->bDescriptorType != USB_INTERFACE_DESCRIPTOR_TYPE)
	{
		VIRTUSB_KDPRINT("desc is not a ifc desc\n");
		return STATUS_UNSUCCESSFUL;
	}
	if(!AIfc->Endpoint)
	{
		status = VirtUsb_UsbDevInitEpArray(AIfc, idesc->bNumEndpoints);
		if(!NT_SUCCESS(status))
		{
			return status;
		}
	}
	if(AIfc->EndpointCount != idesc->bNumEndpoints)
	{
		VIRTUSB_KDPRINT("endpoint count mismatch\n");
		return STATUS_UNSUCCESSFUL;
	}

	ep = ExAllocatePoolWithTag(PagedPool,
	                           sizeof(struct _EP_STATS) * epCapacity,
	                           VIRTUSB_POOL_TAG);
	if(!ep)
	{
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	d = (PUCHAR)idesc + idesc->bLength;
	while((d < (PUCHAR)cd + cd->wTotalLength) &&
	      (d[1] != USB_INTERFACE_DESCRIPTOR_TYPE))
	{
		if(d[1] == USB_ENDPOINT_DESCRIPTOR_TYPE)
		{
			PUSB_ENDPOINT_DESCRIPTOR epd = (PUSB_ENDPOINT_DESCRIPTOR)d;

			// lookup bEndpointAddress
			for(i = 0; i < epCount; i++)
			{
				if(!epd->bEndpointAddress)
				{
					VIRTUSB_KDPRINT("explicit ep0\n");
					goto End;
				}
				if(ep[i].adr == epd->bEndpointAddress)
				{
					VIRTUSB_KDPRINT1("ep#%hu is defined more than once\n", (USHORT)epd->bEndpointAddress);
					goto End;
				}
			}

			// check if we have to reallocate ep
			ASSERT(epCount <= epCapacity);
			if(epCount == epCapacity)
			{
				struct _EP_STATS *newArr;
				epCapacity <<= 1;
				ASSERT(epCapacity);
				ASSERT(epCapacity <= 256);
				newArr = ExAllocatePoolWithTag(PagedPool,
				                               sizeof(struct _EP_STATS) * epCapacity,
				                               VIRTUSB_POOL_TAG);
				if(!newArr)
				{
					status = STATUS_INSUFFICIENT_RESOURCES;
					goto End;
				}
				RtlCopyMemory(newArr, ep, sizeof(struct _EP_STATS) * epCount);
				ExFreePoolWithTag(ep, VIRTUSB_POOL_TAG);
				ep = newArr;
			}

			ep[epCount].adr = epd->bEndpointAddress;
			ep[epCount].ptr = epd;
			epCount++;
		}
		d += *d;
	}

	if(AIfc->EndpointCount != epCount)
	{
		VIRTUSB_KDPRINT("endpoint count mismatch\n");
		status = STATUS_UNSUCCESSFUL;
		goto End;
	}

	for(i = 0; i < epCount; i++)
	{
		AIfc->Endpoint[i].Descriptor = ep[i].ptr;
	}

End:
	ExFreePoolWithTag(ep, VIRTUSB_POOL_TAG);
	return status;
}

BOOLEAN
VirtUsb_IsValidDeviceHandle(
	IN PPDO_DEVICE_DATA   PdoData,
	IN PUSB_DEVICE_HANDLE DeviceHandle
	)
{
	KIRQL           irql;
	PUSBHUB_CONTEXT rhub;
	BOOLEAN         ret;

	ASSERT(PdoData);
	rhub = PdoData->RootHub;
	ASSERT(rhub);

	if(!DeviceHandle)
		return FALSE;

	KeAcquireSpinLock(&PdoData->Lock, &irql);
	ret = VirtUsb_SearchUsbDeviceRecursive(rhub, (PUSBDEV_CONTEXT)DeviceHandle);
	KeReleaseSpinLock(&PdoData->Lock, irql);
	return ret;
}

BOOLEAN
VirtUsb_IsValidPipeHandle(
	IN PUSBDEV_CONTEXT  Device,
	IN USBD_PIPE_HANDLE PipeHandle
	)
{
	PPIPE_CONTEXT ep = (PPIPE_CONTEXT)PipeHandle;
	ULONG         i, j, k, l;

	ASSERT(Device);

	if(ep == &Device->EndpointZero)
		return TRUE;

	for(i = 0; i < Device->ConfigurationCount; i++)
		for(j = 0; j < Device->Configuration[i].InterfaceCount; j++)
			for(k = 0; k < Device->Configuration[i].Interface[j].AltSettingCount; k++)
				for(l = 0; l < Device->Configuration[i].Interface[j].AltSetting[k].EndpointCount; l++)
					if(ep == &Device->Configuration[i].Interface[j].AltSetting[k].Endpoint[l])
						return TRUE;

	return FALSE;
}

PUSBDEV_CONTEXT
VirtUsb_ReferenceUsbDeviceByHandle(
	IN PPDO_DEVICE_DATA   PdoData,
	IN PUSB_DEVICE_HANDLE DeviceHandle,
	IN PVOID              Tag
	)
{
	KIRQL           irql;
	PUSBHUB_CONTEXT rhub;
	PUSBDEV_CONTEXT dev = (PUSBDEV_CONTEXT)DeviceHandle;
	BOOLEAN         ret;

	ASSERT(PdoData);
	rhub = PdoData->RootHub;
	ASSERT(rhub);

	if(!DeviceHandle)
		return NULL;

	KeAcquireSpinLock(&PdoData->Lock, &irql);
	ret = VirtUsb_SearchUsbDeviceRecursive(rhub, dev);
	if(ret)
	{
		if(!dev->RefCount)
		{
			KeClearEvent(&dev->DeviceRemovableEvent);
		}
		dev->RefCount++;
		ASSERT(dev->RefCount > 0);
		VIRTUSB_KDPRINT3("reference dev 0x%p with tag 0x%p (total refs: %lu)\n", dev, Tag, dev->RefCount);
	}
	KeReleaseSpinLock(&PdoData->Lock, irql);
	return ret ? dev : NULL;
}

VOID
VirtUsb_ReferenceUsbDevice(
	IN PUSBDEV_CONTEXT DeviceContext,
	IN PVOID           Tag
	)
{
	KIRQL            irql;
	PPDO_DEVICE_DATA pdoData;

	ASSERT(DeviceContext);
	pdoData = DeviceContext->ParentPdo->DeviceExtension;

	KeAcquireSpinLock(&pdoData->Lock, &irql);
	if(!DeviceContext->RefCount)
	{
		KeClearEvent(&DeviceContext->DeviceRemovableEvent);
	}
	DeviceContext->RefCount++;
	ASSERT(DeviceContext->RefCount > 0);
	KeReleaseSpinLock(&pdoData->Lock, irql);
	VIRTUSB_KDPRINT3("reference dev 0x%p with tag 0x%p (total refs: %lu)\n", DeviceContext, Tag, DeviceContext->RefCount);
}

VOID
VirtUsb_DereferenceUsbDevice(
	IN PUSBDEV_CONTEXT DeviceContext,
	IN PVOID           Tag,
	IN BOOLEAN         HasParentSpinLock
	)
{
	KIRQL            irql = 0;
	PPDO_DEVICE_DATA pdoData;

	ASSERT(DeviceContext);
	pdoData = DeviceContext->ParentPdo->DeviceExtension;

	if(!HasParentSpinLock)
		KeAcquireSpinLock(&pdoData->Lock, &irql);
	ASSERT(DeviceContext->RefCount > 0);
	DeviceContext->RefCount--;
	if(!DeviceContext->RefCount)
	{
		KeSetEvent(&DeviceContext->DeviceRemovableEvent, IO_NO_INCREMENT, FALSE);
	}
	if(!HasParentSpinLock)
		KeReleaseSpinLock(&pdoData->Lock, irql);
	VIRTUSB_KDPRINT3("dereference dev 0x%p with tag 0x%p (refs left: %lu)\n", DeviceContext, Tag, DeviceContext->RefCount);
}
