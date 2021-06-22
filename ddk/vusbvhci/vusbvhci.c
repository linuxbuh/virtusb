#include "vusbvhci.h"
#include "power.h"
#include "pnp.h"
#include "wmi.h"
#include "internal_io.h"
#include "user_io.h"

NTSTATUS
DriverEntry(
	IN PDRIVER_OBJECT  DriverObject,
	IN PUNICODE_STRING RegistryPath
	);

VOID
VUsbVhci_Unload(
	IN PDRIVER_OBJECT DriverObject
	);

NTSTATUS
VUsbVhci_AddDevice(
	IN PDRIVER_OBJECT DriverObject,
	IN PDEVICE_OBJECT PhysicalDeviceObject
	);

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(PAGE, VUsbVhci_Unload)
#pragma alloc_text(PAGE, VUsbVhci_AddDevice)
#pragma alloc_text(PAGE, VUsbVhci_FdoInterfaceDown)
#pragma alloc_text(PAGE, VUsbVhci_StartFdo)
#pragma alloc_text(PAGE, VUsbVhci_CreatePdo)
#pragma alloc_text(PAGE, VUsbVhci_DestroyPdo)
#pragma alloc_text(PAGE, VUsbVhci_GetVirtUsbInterface)
#pragma alloc_text(PAGE, VUsbVhci_SendIrpSynchronously)
#endif // ALLOC_PRAGMA

// Path to the driver's Services Key in the registry
UNICODE_STRING RegPath;

CONST WCHAR *CONST RootHubText        = L"Virtual USB 2.0 Root Hub";
CONST WCHAR *CONST RootHubDeviceID    = L"USB\\ROOT_HUB20";
CONST WCHAR *CONST RootHubHardwareIDs = L"USB\\ROOT_HUB20&VID1138&PID3417&REV0000\0USB\\ROOT_HUB20&VID1138&PID3417\0USB\\ROOT_HUB20\0";

CONST WCHAR *CONST FDONameFormatString    = L"\\Device\\USBFDO-%lu";
CONST WCHAR *CONST FDOSymlinkFormatString = L"\\DosDevices\\HCD%lu";
CONST WCHAR *CONST PDONameFormatString    = L"\\Device\\USBPDO-%lu";

NTSTATUS
DriverEntry(
	IN PDRIVER_OBJECT  DriverObject,
	IN PUNICODE_STRING RegistryPath
	)
{
	ASSERT(KeGetCurrentIrql() < DISPATCH_LEVEL);
	VUSBVHCI_KDPRINT("in DriverEntry\n");

	// Save the RegistryPath for WMI.
	RegPath.MaximumLength = RegistryPath->Length + sizeof(UNICODE_NULL);
	RegPath.Length = RegistryPath->Length;
	RegPath.Buffer = ExAllocatePoolWithTag(PagedPool,
	                                       RegPath.MaximumLength,
	                                       VUSBVHCI_POOL_TAG);
	if(!RegPath.Buffer)
	{
		return STATUS_INSUFFICIENT_RESOURCES;
	}
	RtlCopyUnicodeString(&RegPath, RegistryPath);

	VUSBVHCI_KDPRINT1("RegPath: %wZ\n", &RegPath);

	DriverObject->DriverUnload                                  = VUsbVhci_Unload;
	DriverObject->DriverExtension->AddDevice                    = VUsbVhci_AddDevice;
	DriverObject->MajorFunction[IRP_MJ_CREATE]                  =
	DriverObject->MajorFunction[IRP_MJ_CLOSE]                   = VUsbVhci_DispatchCreateClose;
	DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL]          = VUsbVhci_DispatchDeviceControl;
	DriverObject->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = VUsbVhci_DispatchInternalDeviceControl;
	DriverObject->MajorFunction[IRP_MJ_PNP]                     = VUsbVhci_DispatchPnP;
	DriverObject->MajorFunction[IRP_MJ_POWER]                   = VUsbVhci_DispatchPower;
	DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL]          = VUsbVhci_DispatchSystemControl;

	VUSBVHCI_KDPRINT("driver initialization done\n");

	return STATUS_SUCCESS;
}

VOID
VUsbVhci_Unload(
	IN PDRIVER_OBJECT DriverObject
	)
{
	PAGED_CODE();

	UNREFERENCED_PARAMETER(DriverObject);

	VUSBVHCI_KDPRINT("in VUsbVhci_Unload\n");

	// All the device objects should be gone.
	ASSERT(!DriverObject->DeviceObject);

	// Here we free all the resources allocated in the DriverEntry
	if(RegPath.Buffer)
	{
		ExFreePoolWithTag(RegPath.Buffer, VUSBVHCI_POOL_TAG);
	}
}

NTSTATUS
VUsbVhci_AddDevice(
	IN PDRIVER_OBJECT DriverObject,
	IN PDEVICE_OBJECT PhysicalDeviceObject
	)
{
	NTSTATUS         status;
	PDEVICE_OBJECT   fdo = NULL;
	PFDO_DEVICE_DATA fdoData = NULL;
	ULONG            usbDevNum = 0;
	WCHAR            cDevName[64], cLnkName[64];
	UNICODE_STRING   devName, lnkName;
#if DBG
	PWCHAR           deviceName = NULL;
	ULONG            nameLength;
#endif

	PAGED_CODE();

	VUSBVHCI_KDPRINT("in VUsbVhci_AddDevice\n");
	VUSBVHCI_KDPRINT1("PhysicalDeviceObject: 0x%p\n", PhysicalDeviceObject);

	VUSBVHCI_KDPRINT("creating FDO\n");

	// I got this from the ReactOS EHCI driver. I don't know
	// if it is safe to get the next free UsbDeviceNumber
	// this way on a Windows kernel, or if this will cause
	// other xHCD devices, which may be "plugged-in" after
	// our one, to fail.

	do
	{
		RtlStringCchPrintfW(cDevName,
		                    RTL_NUMBER_OF(cDevName),
		                    FDONameFormatString,
		                    usbDevNum++);
		ASSERT(usbDevNum < 10000);
		RtlInitUnicodeString(&devName, cDevName);
		status = IoCreateDevice(
			DriverObject,                // Our driver object
			sizeof(FDO_DEVICE_DATA),     // Device object extension size
			&devName,                    // \Device\USBFDO-n
			FILE_DEVICE_CONTROLLER,      // We are a controller
			0,                           // Device characteristics
			FALSE,                       // "exclusive" has no effect on FDOs
			&fdo                         // The device object created
			);
	} while((status == STATUS_OBJECT_NAME_EXISTS) ||
	        (status == STATUS_OBJECT_NAME_COLLISION));
	if(!NT_SUCCESS(status))
	{
		VUSBVHCI_KDPRINT2("Couldn't create %ws (status: 0x08lx)\n", cDevName, status);
		goto ErrDev;
	}
	VUSBVHCI_KDPRINT1("%ws created\n", cDevName);

	fdoData = (PFDO_DEVICE_DATA)fdo->DeviceExtension;
	RtlZeroMemory(fdoData, sizeof(FDO_DEVICE_DATA));
	fdoData->Self = fdo;
	fdoData->IsFDO = TRUE;
	fdoData->UsbDeviceNumber = --usbDevNum;

	RtlStringCchPrintfW(cLnkName,
	                    RTL_NUMBER_OF(cLnkName),
	                    FDOSymlinkFormatString, // \DosDevices\HCDn
	                    usbDevNum);
	RtlInitUnicodeString(&lnkName, cLnkName);
	if(NT_SUCCESS(IoCreateSymbolicLink(&lnkName, &devName)))
	{
		VUSBVHCI_KDPRINT2("success: symlinked %ws -> %ws\n", cLnkName, cDevName);
		fdoData->SymlinkFailed = FALSE;
	}
	else
	{
		VUSBVHCI_KDPRINT2("fail: symlinking %ws -> %ws\n", cLnkName, cDevName);
		fdoData->SymlinkFailed = TRUE;
	}

	// Set the PDO for use with Plug&Play functions
	fdoData->UnderlyingPDO = PhysicalDeviceObject;

	// Set the initial state of the FDO
	INITIALIZE_PNP_STATE(fdoData);

	// Set the initial powerstate of the FDO
	fdoData->DevicePowerState = PowerDeviceUnspecified;
	fdoData->SystemPowerState = PowerSystemWorking;

	fdo->Flags |= DO_POWER_PAGABLE;

	// Attach our FDO to the device stack.
	// The return value of IoAttachDeviceToDeviceStack is the top of the
	// attachment chain. This is where all the IRPs should be routed.
	fdoData->NextLowerDriver = IoAttachDeviceToDeviceStack(fdo, PhysicalDeviceObject);
	if(!fdoData->NextLowerDriver)
	{
		status = STATUS_NO_SUCH_DEVICE;
		goto ErrDev;
	}

#if DBG
	status = IoGetDeviceProperty(PhysicalDeviceObject,
	                             DevicePropertyPhysicalDeviceObjectName,
	                             0,
	                             NULL,
	                             &nameLength);
	if(status != STATUS_BUFFER_TOO_SMALL)
	{
		VUSBVHCI_KDPRINT1("AddDevice: IoGetDeviceProperty #1 failed (0x%08lx)\n", status);
		goto ErrDbg;
	}

	deviceName = ExAllocatePoolWithTag(NonPagedPool, nameLength, VUSBVHCI_POOL_TAG);
	if(!deviceName)
	{
		VUSBVHCI_KDPRINT1("AddDevice: no memory to alloc for deviceName(%lu)\n", nameLength);
		status =  STATUS_INSUFFICIENT_RESOURCES;
		goto ErrDbg;
	}

	status = IoGetDeviceProperty(PhysicalDeviceObject,
	                             DevicePropertyPhysicalDeviceObjectName,
	                             nameLength,
	                             deviceName,
	                             &nameLength);
	if(!NT_SUCCESS(status))
	{
		VUSBVHCI_KDPRINT1("AddDevice: IoGetDeviceProperty #2 failed (0x%08lx)\n", status);
		goto ErrDbg;
	}

	VUSBVHCI_KDPRINT4("AddDevice: fdo[0x%p:%ws] to nxtLwr[0x%p]->lwrPdo[0x%p]\n",
	                  fdo,
	                  deviceName,
	                  fdoData->NextLowerDriver,
	                  PhysicalDeviceObject);

	ExFreePoolWithTag(deviceName, VUSBVHCI_POOL_TAG);
#endif

	// Tell the Plug & Play system that this device will need a
	// device interface.
	status = IoRegisterDeviceInterface(PhysicalDeviceObject,
	                                   &GUID_DEVINTERFACE_USB_HOST_CONTROLLER,
	                                   NULL,
	                                   &fdoData->InterfaceName);
	if(NT_SUCCESS(status))
	{
		HANDLE         key;
		UNICODE_STRING valStr;

		status = IoOpenDeviceRegistryKey(PhysicalDeviceObject,
		                                 PLUGPLAY_REGKEY_DEVICE,
		                                 KEY_ALL_ACCESS,
		                                 &key);
		if(!NT_SUCCESS(status))
		{
			VUSBVHCI_KDPRINT1("IoOpenDeviceRegistryKey failed (0x%08lx)\n", status);
			goto ErrIgnore;
		}

		RtlInitUnicodeString(&valStr, L"SymbolicName");

		status = ZwSetValueKey(key,
		                       &valStr,
		                       0,
		                       REG_SZ,
		                       fdoData->InterfaceName.Buffer,
		                       fdoData->InterfaceName.Length);
		if(!NT_SUCCESS(status))
		{
			VUSBVHCI_KDPRINT1("ZwSetValueKey failed (0x%08lx)\n", status);
		}

		ZwClose(key);
	}
	else
	{
		VUSBVHCI_KDPRINT1("AddDevice: IoRegisterDeviceInterface failed (0x%08lx)\n", status);
		// (ignored)
	}
ErrIgnore:

	IoInitializeRemoveLock(&fdoData->RemoveLock, VUSBVHCI_NOT_OWNER_POOL_TAG, 0, 0);

	// We are done with initializing, so let's indicate that and return.
	// This should be the final step in the AddDevice process.
	fdo->Flags &= ~DO_DEVICE_INITIALIZING;

	return STATUS_SUCCESS;

#if DBG
ErrDbg:
	if(deviceName)
	{
		ExFreePoolWithTag(deviceName, VUSBVHCI_POOL_TAG);
	}

	IoDetachDevice(fdoData->NextLowerDriver);
#endif

ErrDev:
	if(fdoData && !fdoData->SymlinkFailed)
	{
		IoDeleteSymbolicLink(&lnkName);
	}

	if(fdo)
	{
		IoDeleteDevice(fdo);
	}

	return status;
}

VOID
VUsbVhci_FdoInterfaceDown(
	IN OUT PFDO_DEVICE_DATA FdoData
	)
{
	PAGED_CODE();

	VUSBVHCI_KDPRINT("in VUsbVhci_FdoInterfaceDown\n");

	ASSERT(FdoData);

	// Stop all access to the device, fail any outstanding I/O to the device,
	// and free all the resources associated with the device.

	// Disable the device interface and free the buffer
	if(FdoData->InterfaceName.Buffer)
	{
		IoSetDeviceInterfaceState(&FdoData->InterfaceName, FALSE);

		ExFreePool(FdoData->InterfaceName.Buffer);
		RtlZeroMemory(&FdoData->InterfaceName,
		              sizeof(UNICODE_STRING));
	}

	// Inform WMI to remove this DeviceObject from its
	// list of providers.
	VUsbVhci_WmiDeRegistration(FdoData);
}

NTSTATUS
VUsbVhci_StartFdo(
	IN OUT PFDO_DEVICE_DATA FdoData
	)
{
	NTSTATUS    status;
	POWER_STATE powerState;
	ULONG       propSize, devAdr, busNum;

	PAGED_CODE();

	VUSBVHCI_KDPRINT("in VUsbVhci_StartFdo\n");

	if(!FdoData->VirtUsbInterface.Size)
	{
		status = VUsbVhci_GetVirtUsbInterface(FdoData->NextLowerDriver,
		                                      &FdoData->VirtUsbInterface);
		if(!NT_SUCCESS(status))
		{
			VUSBVHCI_KDPRINT1("StartFdo: VUsbVhci_GetVirtUsbInterface failed: 0x%08lx\n", status);
			return status;
		}
	}

	// Enable device interface. If the return status is
	// STATUS_OBJECT_NAME_EXISTS means we are enabling the interface
	// that was already enabled, which could happen if the device
	// is stopped and restarted for resource rebalancing.
	if(FdoData->InterfaceName.Buffer)
	{
		status = IoSetDeviceInterfaceState(&FdoData->InterfaceName, TRUE);
		if(!NT_SUCCESS(status))
		{
			VUSBVHCI_KDPRINT1("StartFdo: IoSetDeviceInterfaceState failed: 0x%08lx\n", status);
		}
	}

	// Set the device power state to fully on.
	FdoData->DevicePowerState = PowerDeviceD0;
	powerState.DeviceState = PowerDeviceD0;
	PoSetPowerState(FdoData->Self, DevicePowerState, powerState);

	status = IoGetDeviceProperty(FdoData->UnderlyingPDO,
	                             DevicePropertyAddress,
	                             sizeof(ULONG),
	                             &devAdr,
	                             &propSize);
	if(NT_SUCCESS(status))
	{
		VUSBVHCI_KDPRINT1("device address: %lu\n", devAdr);
	}
	status = IoGetDeviceProperty(FdoData->UnderlyingPDO,
	                             DevicePropertyBusNumber,
	                             sizeof(ULONG),
	                             &busNum,
	                             &propSize);
	if(NT_SUCCESS(status))
	{
		VUSBVHCI_KDPRINT1("bus number: %lu\n", busNum);
	}

	SET_NEW_PNP_STATE(FdoData, Started);

	// Register with WMI
	status = VUsbVhci_WmiRegistration(FdoData);
	if(!NT_SUCCESS(status))
	{
		VUSBVHCI_KDPRINT1("StartFdo: VUsbVhci_WmiRegistration failed 0x%08lx\n", status);
	}

	return STATUS_SUCCESS;
}

NTSTATUS
VUsbVhci_CreatePdo(
	IN OUT PFDO_DEVICE_DATA FdoData
	)
{
	PPDO_DEVICE_DATA pdoData;
	PUSBHUB_CONTEXT  rhub;
	PDEVICE_OBJECT   fdo;
	PDEVICE_OBJECT   pdo;
	ULONG            usbDevNum = 0;
	WCHAR            cDevName[64];
	UNICODE_STRING   devName;
	NTSTATUS         status;

	PAGED_CODE();

	VUSBVHCI_KDPRINT("in VUsbVhci_CreatePdo\n");

	ASSERT(FdoData);
	fdo = FdoData->Self;
	ASSERT(fdo);
	ASSERT(!FdoData->RHubPdo);

	// I got this from the ReactOS EHCI driver. I don't know
	// if it is safe to get the next free UsbDeviceNumber
	// this way on a Windows kernel, or if this will cause
	// other xHCD devices, which may be "plugged-in" after
	// our one, to fail.

	do
	{
		RtlStringCchPrintfW(cDevName,
		                    RTL_NUMBER_OF(cDevName),
		                    PDONameFormatString,
		                    usbDevNum++);
		RtlInitUnicodeString(&devName, cDevName);
		status = IoCreateDevice(fdo->DriverObject,
		                        sizeof(PDO_DEVICE_DATA),
		                        &devName, // \Device\USBPDO-n
		                        FILE_DEVICE_BUS_EXTENDER,
		                        0,
		                        FALSE,
		                        &pdo);
	} while((status == STATUS_OBJECT_NAME_EXISTS) ||
	        (status == STATUS_OBJECT_NAME_COLLISION));
	if(!NT_SUCCESS(status))
	{
		VUSBVHCI_KDPRINT2("failed to create root-hub %ws (0x08lx)\n", cDevName, status);
		return status;
	}
	ASSERT(pdo);
	VUSBVHCI_KDPRINT1("created root-hub %ws\n", cDevName);
	pdoData = (PPDO_DEVICE_DATA)pdo->DeviceExtension;
	RtlZeroMemory(pdoData, sizeof(PDO_DEVICE_DATA));
	pdoData->Self = pdo;
	pdoData->ParentFdo = fdo;
	VUSBVHCI_KDPRINT2("created pdo 0x%p, extension 0x%p\n", pdo, pdoData);

	INITIALIZE_PNP_STATE(pdoData);

	// PDO's usually start their life at D3
	pdoData->DevicePowerState = PowerDeviceD3;
	pdoData->SystemPowerState = PowerSystemWorking;

	pdo->Flags |= DO_POWER_PAGABLE;

	ExInitializeFastMutex(&pdoData->Mutex);

	IoInitializeRemoveLock(&pdoData->RemoveLock, VUSBVHCI_NOT_OWNER_POOL_TAG, 0, 0);

	pdoData->Present = TRUE; // attached to the bus
	pdoData->ReportedMissing = FALSE; // not yet reported missing

	FdoData->RHubPdo = pdo;

	rhub = VUsbVhci_GetRootHubContext(FdoData);
	ASSERT(rhub);

	// Store our PDO in the VirtUsb-drivers hub-context struct
	rhub->HcdContext = pdo;

	// Store our FdoData in the VirtUsb-drivers hub-context struct
	rhub->HcdContext2 = FdoData;

	// This should be the last step in initialization.
	pdo->Flags &= ~DO_DEVICE_INITIALIZING;

	return status;
}

VOID
VUsbVhci_DestroyPdo(
	IN OUT PPDO_DEVICE_DATA PdoData
	)
{
	PDEVICE_OBJECT pdo;

	PAGED_CODE();

	VUSBVHCI_KDPRINT("in VUsbVhci_DestroyPdo\n");

	ASSERT(PdoData);
	pdo = PdoData->Self;
	ASSERT(pdo);

	// Disable the device interface and free the buffer
	if(PdoData->InterfaceName.Buffer)
	{
		IoSetDeviceInterfaceState(&PdoData->InterfaceName, FALSE);

		ExFreePool(PdoData->InterfaceName.Buffer);
		RtlZeroMemory(&PdoData->InterfaceName,
		              sizeof(UNICODE_STRING));
	}
}

NTSTATUS
VUsbVhci_GetVirtUsbInterface(
	IN  PDEVICE_OBJECT                 Pdo,
	OUT PVIRTUSB_BUS_INTERFACE_VHCI_V1 Interface
	)
{
	KEVENT                        event;
	NTSTATUS                      status;
	PIRP                          irp;
	IO_STATUS_BLOCK               ioStatusBlock;
	PIO_STACK_LOCATION            irpStack;
	PDEVICE_OBJECT                targetObject;
	VIRTUSB_BUS_INTERFACE_VHCI_V1 ifc;

	PAGED_CODE();

	VUSBVHCI_KDPRINT("in VUsbVhci_GetVirtUsbInterface\n");

	ASSERT(Pdo);
	ASSERT(Interface);

	KeInitializeEvent(&event, NotificationEvent, FALSE);
	targetObject = IoGetAttachedDeviceReference(Pdo);
	irp = IoBuildSynchronousFsdRequest(IRP_MJ_PNP,
	                                   targetObject,
	                                   NULL,
	                                   0,
	                                   NULL,
	                                   &event,
	                                   &ioStatusBlock);
	if(!irp)
	{
		status = STATUS_INSUFFICIENT_RESOURCES;
		goto End;
	}

	irpStack = IoGetNextIrpStackLocation(irp);
	irpStack->MinorFunction = IRP_MN_QUERY_INTERFACE;
	irpStack->Parameters.QueryInterface.InterfaceType = &VIRTUSB_BUS_INTERFACE_VHCI_GUID;
	irpStack->Parameters.QueryInterface.Size = sizeof(VIRTUSB_BUS_INTERFACE_VHCI_V1);
	irpStack->Parameters.QueryInterface.Version = VIRTUSB_BUSIF_VHCI_VERSION_1;
	irpStack->Parameters.QueryInterface.Interface = (PINTERFACE)&ifc;
	irpStack->Parameters.QueryInterface.InterfaceSpecificData = NULL;
	RtlZeroMemory(&ifc, sizeof(VIRTUSB_BUS_INTERFACE_VHCI_V1));

	irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
	status = IoCallDriver(targetObject, irp);
	if(STATUS_PENDING == status)
	{
		KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, NULL);
		status = ioStatusBlock.Status;
	}
	if(NT_SUCCESS(status))
	{
		RtlCopyMemory(Interface, &ifc, sizeof(VIRTUSB_BUS_INTERFACE_VHCI_V1));
	}
	else
	{
		VUSBVHCI_KDPRINT1("IRP_MN_QUERY_INTERFACE failed: 0x%08lx\n", status);
	}

End:
	ObDereferenceObject(targetObject);
	return status;
}

NTSTATUS
VUsbVhci_GetAndStoreRootHubName(
	IN OUT PPDO_DEVICE_DATA PdoData
	)
{
	NTSTATUS       status = STATUS_SUCCESS;
	HANDLE         key;
	UNICODE_STRING valStr;

	ASSERT(KeGetCurrentIrql() < DISPATCH_LEVEL);

	VUSBVHCI_KDPRINT("in VUsbVhci_GetAndStoreRootHubName\n");

	ExAcquireFastMutex(&PdoData->Mutex);
	// First time we get here?
	if(!PdoData->InterfaceName.Buffer)
	{
		// We need to get the name of the symlink, which belongs
		// to the interface of our root-hub. We do this by registrering
		// the interface.
		status = IoRegisterDeviceInterface(PdoData->Self,
		                                   &GUID_DEVINTERFACE_USB_HUB,
		                                   NULL,
		                                   &PdoData->InterfaceName);
		if(!NT_SUCCESS(status))
		{
			VUSBVHCI_KDPRINT1("IoRegisterDeviceInterface failed (0x%08lx)\n", status);
			goto ErrReleaseLock;
		}

		status = IoOpenDeviceRegistryKey(PdoData->Self,
		                                 PLUGPLAY_REGKEY_DEVICE,
		                                 KEY_ALL_ACCESS,
		                                 &key);
		if(!NT_SUCCESS(status))
		{
			VUSBVHCI_KDPRINT1("IoOpenDeviceRegistryKey failed (0x%08lx)\n", status);
			goto ErrReleaseLock;
		}

		RtlInitUnicodeString(&valStr, L"SymbolicName");

		status = ZwSetValueKey(key,
		                       &valStr,
		                       0,
		                       REG_SZ,
		                       PdoData->InterfaceName.Buffer,
		                       PdoData->InterfaceName.Length);
		if(!NT_SUCCESS(status))
		{
			VUSBVHCI_KDPRINT1("ZwSetValueKey failed (0x%08lx)\n", status);
		}

		ZwClose(key);
	}
ErrReleaseLock:
	ExReleaseFastMutex(&PdoData->Mutex);
	return status;
}

NTSTATUS
VUsbVhci_SendIrpSynchronously(
	IN     PDEVICE_OBJECT DeviceObject,
	IN OUT PIRP           Irp
	)
/*
    Sends the Irp down to lower driver and waits for it to
    come back by setting a completion routine.
*/
{
	KEVENT   event;
	NTSTATUS status;

	PAGED_CODE();

	VUSBVHCI_KDPRINT("in VUsbVhci_SendIrpSynchronously\n");

	KeInitializeEvent(&event, NotificationEvent, FALSE);

	IoCopyCurrentIrpStackLocationToNext(Irp);

	IoSetCompletionRoutine(Irp,
	                       VUsbVhci_CompletionRoutine,
	                       &event,
	                       TRUE,
	                       TRUE,
	                       TRUE);

	status = IoCallDriver(DeviceObject, Irp);

	if(status == STATUS_PENDING)
	{
		KeWaitForSingleObject(&event,
		                      Executive,
		                      KernelMode,
		                      FALSE,
		                      NULL);
		status = Irp->IoStatus.Status;
	}

	return status;
}

NTSTATUS
VUsbVhci_CompletionRoutine(
	IN PDEVICE_OBJECT DeviceObject,
	IN PIRP           Irp,
	IN PVOID          Context
	)
{
	UNREFERENCED_PARAMETER(DeviceObject);

	ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);

	VUSBVHCI_KDPRINT("in VUsbVhci_CompletionRoutine\n");

	// If the lower driver didn't return STATUS_PENDING, we don't need to
	// set the event because we won't be waiting on it.
	if(Irp->PendingReturned)
	{
		KeSetEvent((PKEVENT)Context, IO_NO_INCREMENT, FALSE);
	}
	return STATUS_MORE_PROCESSING_REQUIRED; // Keep this IRP
}

PUSBHUB_CONTEXT
VUsbVhci_GetRootHubContext(
	IN PFDO_DEVICE_DATA FdoData
	)
{
	PVIRTUSB_BUSIFFN_GET_ROOT_HUB_CONTEXT f = FdoData->VirtUsbInterface.GetRootHubContext;
	ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);
	VUSBVHCI_KDPRINT("in VUsbVhci_GetRootHubContext\n");
	if(f)
	{
		return f(FdoData->VirtUsbInterface.Context);
	}
	return NULL;
}

PUSB_HUB_DESCRIPTOR
VUsbVhci_GetRootHubHubDescriptor(
	IN PFDO_DEVICE_DATA FdoData
	)
{
	PVIRTUSB_BUSIFFN_GET_ROOT_HUB_HUB_DESCRIPTOR f = FdoData->VirtUsbInterface.GetRootHubHubDescriptor;
	ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);
	VUSBVHCI_KDPRINT("in VUsbVhci_GetRootHubHubDescriptor\n");
	if(f)
	{
		return f(FdoData->VirtUsbInterface.Context);
	}
	return NULL;
}

NTSTATUS
VUsbVhci_ReferenceUsbDeviceByHandle(
	IN  PFDO_DEVICE_DATA FdoData,
	IN  PVOID            DeviceHandle,
	OUT PUSBDEV_CONTEXT  *DeviceContext
	)
{
	PVIRTUSB_BUSIFFN_REFERENCE_USB_DEVICE_BY_HANDLE f = FdoData->VirtUsbInterface.ReferenceUsbDeviceByHandle;
	ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);
	VUSBVHCI_KDPRINT("in VUsbVhci_ReferenceUsbDeviceByHandle\n");
	if(f)
	{
		return f(FdoData->VirtUsbInterface.Context, DeviceHandle, DeviceContext);
	}
	return STATUS_UNSUCCESSFUL;
}

NTSTATUS
VUsbVhci_ReferenceUsbDevice(
	IN PFDO_DEVICE_DATA FdoData,
	IN PUSBDEV_CONTEXT  DeviceContext
	)
{
	PVIRTUSB_BUSIFFN_REFERENCE_USB_DEVICE f = FdoData->VirtUsbInterface.ReferenceUsbDevice;
	ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);
	VUSBVHCI_KDPRINT("in VUsbVhci_ReferenceUsbDevice\n");
	if(f)
	{
		f(FdoData->VirtUsbInterface.Context, DeviceContext);
		return STATUS_SUCCESS;
	}
	return STATUS_UNSUCCESSFUL;
}

NTSTATUS
VUsbVhci_DereferenceUsbDevice(
	IN PFDO_DEVICE_DATA FdoData,
	IN PUSBDEV_CONTEXT  DeviceContext
	)
{
	PVIRTUSB_BUSIFFN_DEREFERENCE_USB_DEVICE f = FdoData->VirtUsbInterface.DereferenceUsbDevice;
	ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);
	VUSBVHCI_KDPRINT("in VUsbVhci_DereferenceUsbDevice\n");
	if(f)
	{
		f(FdoData->VirtUsbInterface.Context, DeviceContext);
		return STATUS_SUCCESS;
	}
	return STATUS_UNSUCCESSFUL;
}

NTSTATUS
VUsbVhci_CreateUsbDevice(
	IN     PFDO_DEVICE_DATA FdoData,
	IN OUT PVOID            *DeviceHandle,
	IN     PUSBHUB_CONTEXT  HubDeviceContext,
	IN     USHORT           PortStatus,
	IN     USHORT           PortNumber
	)
{
	PVIRTUSB_BUSIFFN_CREATE_USB_DEVICE f = FdoData->VirtUsbInterface.CreateUsbDevice;
	ASSERT(KeGetCurrentIrql() < DISPATCH_LEVEL);
	VUSBVHCI_KDPRINT("in VUsbVhci_CreateUsbDevice\n");
	if(f)
	{
		return f(FdoData->VirtUsbInterface.Context, DeviceHandle, HubDeviceContext, PortStatus, PortNumber);
	}
	return STATUS_UNSUCCESSFUL;
}

NTSTATUS
VUsbVhci_InitializeUsbDevice(
	IN     PFDO_DEVICE_DATA FdoData,
	IN OUT PUSBDEV_CONTEXT  DeviceContext
	)
{
	PVIRTUSB_BUSIFFN_INITIALIZE_USB_DEVICE f = FdoData->VirtUsbInterface.InitializeUsbDevice;
	ASSERT(KeGetCurrentIrql() < DISPATCH_LEVEL);
	VUSBVHCI_KDPRINT("in VUsbVhci_InitializeUsbDevice\n");
	if(f)
	{
		return f(FdoData->VirtUsbInterface.Context, DeviceContext);
	}
	return STATUS_UNSUCCESSFUL;
}

NTSTATUS
VUsbVhci_RemoveUsbDevice(
	IN     PFDO_DEVICE_DATA FdoData,
	IN OUT PUSBDEV_CONTEXT  DeviceContext,
	IN     ULONG            Flags,
	IN     BOOLEAN          Dereference
	)
{
	PVIRTUSB_BUSIFFN_REMOVE_USB_DEVICE f = FdoData->VirtUsbInterface.RemoveUsbDevice;
	ASSERT(KeGetCurrentIrql() < DISPATCH_LEVEL);
	VUSBVHCI_KDPRINT("in VUsbVhci_RemoveUsbDevice\n");
	if(f)
	{
		return f(FdoData->VirtUsbInterface.Context, DeviceContext, Flags, Dereference);
	}
	return STATUS_UNSUCCESSFUL;
}
