#include "virtusb.h"
#include "trace.h"
#include "virtusb.tmh"
#include "power.h"
#include "pnp.h"
#include "wmi.h"
#include "internal_io.h"
#include "user_io.h"
#include "roothub.h"
#include "usbdev.h"
#include "virtusb_ioctl.h"

NTSTATUS
DriverEntry(
	_In_ PDRIVER_OBJECT  DriverObject,
	_In_ PUNICODE_STRING RegistryPath
	);

VOID
VirtUsb_Unload(
	_In_ PDRIVER_OBJECT DriverObject
	);

NTSTATUS
VirtUsb_AddDevice(
	_In_ PDRIVER_OBJECT DriverObject,
	_In_ PDEVICE_OBJECT PhysicalDeviceObject
	);

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(PAGE, VirtUsb_Unload)
#pragma alloc_text(PAGE, VirtUsb_AddDevice)
#pragma alloc_text(PAGE, VirtUsb_FdoInterfaceDown)
#pragma alloc_text(PAGE, VirtUsb_StartFdo)
#pragma alloc_text(PAGE, VirtUsb_CreatePdo)
#pragma alloc_text(PAGE, VirtUsb_InitializePdo)
#pragma alloc_text(PAGE, VirtUsb_DestroyPdo)
#pragma alloc_text(PAGE, VirtUsb_SendIrpSynchronously)
#endif // ALLOC_PRAGMA

// Path to the driver's Services Key in the registry
UNICODE_STRING VirtUsb_RegPath;

CONST WCHAR *CONST HcdText        = L"Virtual USB 2.0 Host Controller";
CONST WCHAR *CONST HcdDeviceID    = L"VIRTUSB\\VHCI";
CONST WCHAR *CONST HcdHardwareIDs = L"VIRTUSB\\VHCI&IOCIFC\0VIRTUSB\\VHCI\0";

NTSTATUS
DriverEntry(
	_In_ PDRIVER_OBJECT  DriverObject,
	_In_ PUNICODE_STRING RegistryPath
	)
{
	// Initialize WPP Tracing
	WPP_INIT_TRACING(DriverObject, RegistryPath);

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_VIRTUSB, ">%!FUNC!");
	ASSERT(KeGetCurrentIrql() < DISPATCH_LEVEL);

	// Save the RegistryPath for WMI.
	VirtUsb_RegPath.MaximumLength = RegistryPath->Length + sizeof(UNICODE_NULL);
	VirtUsb_RegPath.Length = RegistryPath->Length;
	VirtUsb_RegPath.Buffer = ExAllocatePoolWithTag(PagedPool,
	                                       VirtUsb_RegPath.MaximumLength,
	                                       VIRTUSB_POOL_TAG);
	if(!VirtUsb_RegPath.Buffer)
	{
		return STATUS_INSUFFICIENT_RESOURCES;
	}
	RtlCopyUnicodeString(&VirtUsb_RegPath, RegistryPath);

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_VIRTUSB, "VIrtUsb_RegPath: %wZ", &VirtUsb_RegPath);

	DriverObject->DriverUnload                                  = VirtUsb_Unload;
	DriverObject->DriverExtension->AddDevice                    = VirtUsb_AddDevice;
	DriverObject->MajorFunction[IRP_MJ_CREATE]                  =
	DriverObject->MajorFunction[IRP_MJ_CLOSE]                   = VirtUsb_DispatchCreateClose;
	DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL]          = VirtUsb_DispatchDeviceControl;
	DriverObject->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = VirtUsb_DispatchInternalDeviceControl;
	DriverObject->MajorFunction[IRP_MJ_PNP]                     = VirtUsb_DispatchPnP;
	DriverObject->MajorFunction[IRP_MJ_POWER]                   = VirtUsb_DispatchPower;
	DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL]          = VirtUsb_DispatchSystemControl;

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_VIRTUSB, "driver initialization done");

	return STATUS_SUCCESS;
}

VOID
VirtUsb_Unload(
	_In_ PDRIVER_OBJECT DriverObject
	)
{
	PAGED_CODE();

	UNREFERENCED_PARAMETER(DriverObject);

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_VIRTUSB, ">%!FUNC!");

	// All the device objects should be gone.
	ASSERT(!DriverObject->DeviceObject);

	// Here we free all the resources allocated in the DriverEntry
	if(VirtUsb_RegPath.Buffer)
	{
		ExFreePoolWithTag(VirtUsb_RegPath.Buffer, VIRTUSB_POOL_TAG);
	}
}

NTSTATUS
VirtUsb_AddDevice(
	_In_ PDRIVER_OBJECT DriverObject,
	_In_ PDEVICE_OBJECT PhysicalDeviceObject
	)
{
	NTSTATUS         status;
	PDEVICE_OBJECT   fdo = NULL;
	PFDO_DEVICE_DATA fdoData = NULL;
#if DBG
	PWCHAR           deviceName = NULL;
	ULONG            nameLength;
#endif

	PAGED_CODE();

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_VIRTUSB, ">%!FUNC!");
	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_VIRTUSB, "PhysicalDeviceObject: 0x%p", PhysicalDeviceObject);

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_VIRTUSB, "creating FDO");

	status = IoCreateDevice(
		DriverObject,                // Our driver object
		sizeof(FDO_DEVICE_DATA),     // Device object extension size
		NULL,                        // FDOs do not have names
		FILE_DEVICE_BUS_EXTENDER,    // We are a bus
		FILE_DEVICE_SECURE_OPEN,     // Device characteristics
		FALSE,                       // "exclusive" has no effect on FDOs
		&fdo                         // The device object created
		);

	if(!NT_SUCCESS(status))
	{
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_VIRTUSB, "Couldn't create the device object (%!STATUS!)", status);
		goto ErrDev;
	}

	ASSERT(fdo);
	fdoData = (PFDO_DEVICE_DATA)fdo->DeviceExtension;
	RtlZeroMemory(fdoData, sizeof(FDO_DEVICE_DATA));
	fdoData->Self = fdo;
	fdoData->IsFDO = TRUE;
	InitializeListHead(&fdoData->ListOfPdos);
	InitializeListHead(&fdoData->ListOfFiles);

	// Set the PDO for use with Plug&Play functions
	fdoData->UnderlyingPDO = PhysicalDeviceObject;

	// Set the initial state of the FDO
	INITIALIZE_PNP_STATE(fdoData);

	// Set the initial powerstate of the FDO
	fdoData->DevicePowerState = PowerDeviceUnspecified;
	fdoData->SystemPowerState = PowerSystemWorking;

	fdo->Flags |= DO_POWER_PAGABLE;

	ExInitializeFastMutex(&fdoData->Mutex);

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
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_VIRTUSB, "AddDevice: IoGetDeviceProperty #1 failed (%!STATUS!)", status);
		goto ErrDbg;
	}

	deviceName = ExAllocatePoolWithTag(NonPagedPool, nameLength, VIRTUSB_POOL_TAG);
	if(!deviceName)
	{
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_VIRTUSB, "AddDevice: no memory to alloc for deviceName(%lu)", nameLength);
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
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_VIRTUSB, "AddDevice: IoGetDeviceProperty #2 failed (%!STATUS!)", status);
		goto ErrDbg;
	}

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_VIRTUSB,
	            "AddDevice: fdo[0x%p:%ws] to nxtLwr[0x%p]->lwrPdo[0x%p]",
	            fdo,
	            deviceName,
	            fdoData->NextLowerDriver,
	            PhysicalDeviceObject);

	ExFreePoolWithTag(deviceName, VIRTUSB_POOL_TAG);
#endif

	// Tell the Plug & Play system that this device will need a
	// device interface.
	status = IoRegisterDeviceInterface(PhysicalDeviceObject,
	                                   &GUID_DEVINTERFACE_VIRTUSB_BUS,
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
			TraceEvents(TRACE_LEVEL_WARNING, TRACE_VIRTUSB, "IoOpenDeviceRegistryKey failed (%!STATUS!)", status);
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
			TraceEvents(TRACE_LEVEL_WARNING, TRACE_VIRTUSB, "ZwSetValueKey failed (%!STATUS!)", status);
		}

		ZwClose(key);
	}
	else
	{
		TraceEvents(TRACE_LEVEL_WARNING, TRACE_VIRTUSB, "AddDevice: IoRegisterDeviceInterface failed (%!STATUS!)", status);
		// (ignored)
	}
ErrIgnore:

	IoInitializeRemoveLock(&fdoData->RemoveLock, VIRTUSB_NOT_OWNER_POOL_TAG, 0, 0);

	// We are done with initializing, so let's indicate that and return.
	// This should be the final step in the AddDevice process.
	fdo->Flags &= ~DO_DEVICE_INITIALIZING;

	return STATUS_SUCCESS;

#if DBG
ErrDbg:
	if(deviceName)
	{
		ExFreePoolWithTag(deviceName, VIRTUSB_POOL_TAG);
	}

	IoDetachDevice(fdoData->NextLowerDriver);
#endif

ErrDev:
	if(fdo)
	{
		IoDeleteDevice(fdo);
	}

	return status;
}

VOID
VirtUsb_FdoInterfaceDown(
	_Inout_ PFDO_DEVICE_DATA FdoData
	)
{
	PAGED_CODE();

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_VIRTUSB, ">%!FUNC!");

	ASSERT(FdoData);

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
	VirtUsb_WmiDeRegistration(FdoData);
}

NTSTATUS
VirtUsb_StartFdo(
	_Inout_ PFDO_DEVICE_DATA FdoData
	)
{
	NTSTATUS    status;
	POWER_STATE powerState;

	PAGED_CODE();

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_VIRTUSB, ">%!FUNC!");

	ASSERT(FdoData);

	// Enable device interface. If the return status is
	// STATUS_OBJECT_NAME_EXISTS, this means we are enabling the interface
	// that was already enabled, which could happen if the device
	// is stopped and restarted for resource rebalancing.
	if(FdoData->InterfaceName.Buffer)
	{
		status = IoSetDeviceInterfaceState(&FdoData->InterfaceName, TRUE);
		if(!NT_SUCCESS(status))
		{
			TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_VIRTUSB, "%!FUNC!: IoSetDeviceInterfaceState failed: %!STATUS!", status);
		}
	}

	// Set the device power state to fully on.
	FdoData->DevicePowerState = PowerDeviceD0;
	powerState.DeviceState = PowerDeviceD0;
	PoSetPowerState(FdoData->Self, DevicePowerState, powerState);

	SET_NEW_PNP_STATE(FdoData, Started);

	// Register with WMI
	status = VirtUsb_WmiRegistration(FdoData);
	if(!NT_SUCCESS(status))
	{
		TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_VIRTUSB, "%!FUNC!: VirtUsb_WmiRegistration failed %!STATUS!", status);
	}

	return STATUS_SUCCESS;
}

NTSTATUS
VirtUsb_CreatePdo(
	_Inout_ PFILE_CONTEXT File
	)
{
	PLIST_ENTRY         listHead, entry;
	ULONG               i;
	NTSTATUS            status = STATUS_SUCCESS;
	PDEVICE_OBJECT      fdo;
	PDEVICE_OBJECT      pdo;
	PFDO_DEVICE_DATA    fdoData;
	PPDO_DEVICE_DATA    pdoData, otherPdoData;

	PAGED_CODE();

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_VIRTUSB, ">%!FUNC!");

	ASSERT(File);
	fdo = File->ParentFdo;
	ASSERT(fdo);
	fdoData = fdo->DeviceExtension;
	ASSERT(fdoData);
	ASSERT(File->PortCount);

	status = IoCreateDeviceSecure(fdo->DriverObject,
	                              sizeof(PDO_DEVICE_DATA),
	                              NULL,
	                              FILE_DEVICE_UNKNOWN,
	                              FILE_AUTOGENERATED_DEVICE_NAME,
	                              FALSE,
	                              &SDDL_DEVOBJ_SYS_ALL_ADM_ALL,
	                              &GUID_DEVCLASS_USB,
	                              &pdo);
	if(!NT_SUCCESS(status))
	{
		return status;
	}
	ASSERT(pdo);
	pdoData = pdo->DeviceExtension;
	RtlZeroMemory(pdoData, sizeof(PDO_DEVICE_DATA));
	pdoData->Self = pdo;
	pdoData->ParentFdo = fdo;
	pdoData->ParentFile = File;
	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_VIRTUSB, "create pdo 0x%p, extension 0x%p", pdo, pdoData);

	INITIALIZE_PNP_STATE(pdoData);

	// PDO's usually start their life at D3
	pdoData->DevicePowerState = PowerDeviceD3;
	pdoData->SystemPowerState = PowerSystemWorking;

	pdo->Flags |= DO_POWER_PAGABLE;

	InitializeListHead(&pdoData->PendingWorkList);
	InitializeListHead(&pdoData->FetchedWorkList);
	InitializeListHead(&pdoData->CancelWorkList);
	InitializeListHead(&pdoData->CancelingWorkList);
	InitializeListHead(&pdoData->RootHubRequests);
	KeInitializeSpinLock(&pdoData->Lock);

	status = VirtUsb_InitializePdo(pdoData);
	if(!NT_SUCCESS(status))
	{
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_VIRTUSB, "init failed");
		goto Err_DelDev;
	}
	ASSERT(pdoData->RootHub);

	IoInitializeRemoveLock(&pdoData->RemoveLock, VIRTUSB_NOT_OWNER_POOL_TAG, 0, 0);

	ExAcquireFastMutex(&fdoData->Mutex);

	// Find free ID
	for(i = 0; i < 10000; i++)
	{
		listHead = &fdoData->ListOfPdos;
		for(entry = listHead->Flink;
		    entry != listHead;
		    entry = entry->Flink)
		{
			otherPdoData = CONTAINING_RECORD(entry, PDO_DEVICE_DATA, Link);
			ASSERT(otherPdoData);
			if(otherPdoData->Id == i)
			{
				goto outer_continue;
			}
		}
		break;
	outer_continue:
		;
	}
	if(i == 10000)
	{
		ExReleaseFastMutex(&fdoData->Mutex);
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_VIRTUSB, "no free id found");
		status = STATUS_INSUFFICIENT_RESOURCES;
		goto Err_DelDev;
	}
	ASSERT(i < 10000);
	pdoData->Id = i;
	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_VIRTUSB, "ID: %lu", i);
	InsertTailList(&fdoData->ListOfPdos, &pdoData->Link);

	if(!NT_SUCCESS(RtlStringCchPrintfW(pdoData->InstanceID,
	                                   RTL_NUMBER_OF(pdoData->InstanceID),
	                                   L"%lu",
	                                   i)))
	{
		ASSERT(FALSE);
	}

	pdoData->Present = TRUE; // attached to the bus
	pdoData->ReportedMissing = FALSE; // not yet reported missing

	File->HcdPdo = pdo;

	// This should be the last step in initialization.
	pdo->Flags &= ~DO_DEVICE_INITIALIZING;

	ExReleaseFastMutex(&fdoData->Mutex);

	IoInvalidateDeviceRelations(fdoData->UnderlyingPDO, BusRelations);

	return status;

Err_DelDev:
	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_VIRTUSB, "delete pdo");
	IoDeleteDevice(pdo);
	return status;
}

NTSTATUS
VirtUsb_InitializePdo(
	_Inout_ PPDO_DEVICE_DATA PdoData
	)
{
	NTSTATUS            status;
	PDEVICE_OBJECT      pdo;
	PFILE_CONTEXT       file;
	PUSB_HUB_DESCRIPTOR hubDesc;

	PAGED_CODE();

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_VIRTUSB, ">%!FUNC!");

	ASSERT(PdoData);
	pdo = PdoData->Self;
	ASSERT(pdo);
	file = PdoData->ParentFile;
	ASSERT(file);
	ASSERT(file->PortCount);

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_VIRTUSB, "(re)init pdo 0x%p, extension 0x%p", pdo, PdoData);

	ASSERT(!PdoData->ReportedMissing);
	ASSERT(!PdoData->InterfaceRefCount);
	ASSERT(!PdoData->RootHubNotifyWorkItem);
	ASSERT(IsListEmpty(&PdoData->PendingWorkList));
	ASSERT(IsListEmpty(&PdoData->FetchedWorkList));
	ASSERT(IsListEmpty(&PdoData->RootHubRequests));

	// generate hub descriptor for our root-hub
	hubDesc = VirtUsb_GenHubDescriptor(file->PortCount);
	if(!hubDesc)
	{
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	status = VirtUsb_AllocateUsbHub(PdoData, &PdoData->RootHub, NULL, file->PortCount);
	if(!NT_SUCCESS(status))
	{
		ExFreePoolWithTag(hubDesc, VIRTUSB_DEVICE_POOL_TAG);
		return status;
	}
	PdoData->RootHub->HubDescriptor = hubDesc;
	PdoData->RootHub->Speed         = UsbHighSpeed;
	PdoData->RootHub->Type          = Usb20Device;

	status = VirtUsb_InitRootHubDescriptors(PdoData->RootHub, file->PortCount);
	if(!NT_SUCCESS(status))
	{
		TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_VIRTUSB, "destroy&free root-hub");
		VirtUsb_FreeUsbDev((PUSBDEV_CONTEXT)PdoData->RootHub);
		return status;
	}

	return status;
}

VOID
VirtUsb_DestroyPdo(
	_Inout_ PPDO_DEVICE_DATA PdoData
	)
{
	PDEVICE_OBJECT pdo;

	PAGED_CODE();

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_VIRTUSB, ">%!FUNC!");

	ASSERT(PdoData);
	pdo = PdoData->Self;
	ASSERT(pdo);

	ASSERT(PdoData->RootHub);
	ASSERT(!PdoData->InterfaceRefCount);
	ASSERT(!PdoData->RootHubNotifyWorkItem);
	ASSERT(IsListEmpty(&PdoData->PendingWorkList));
	ASSERT(IsListEmpty(&PdoData->FetchedWorkList));
	ASSERT(IsListEmpty(&PdoData->RootHubRequests));
	// There still may be some cancel routines running, so we won't check these two:
	//ASSERT(IsListEmpty(&PdoData->CancelWorkList));
	//ASSERT(IsListEmpty(&PdoData->CancelingWorkList));

	VirtUsb_FreeUsbDev((PUSBDEV_CONTEXT)PdoData->RootHub);
}

NTSTATUS
VirtUsb_SendIrpSynchronously(
	_In_    PDEVICE_OBJECT DeviceObject,
	_Inout_ PIRP           Irp
	)
/*
    Sends the Irp down to lower driver and waits for it to
    come back by setting a completion routine.
*/
{
	KEVENT   event;
	NTSTATUS status;

	PAGED_CODE();

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_VIRTUSB, ">%!FUNC!");

	KeInitializeEvent(&event, NotificationEvent, FALSE);

	IoCopyCurrentIrpStackLocationToNext(Irp);

	IoSetCompletionRoutine(Irp,
	                       VirtUsb_CompletionRoutine,
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
VirtUsb_CompletionRoutine(
	_In_ PDEVICE_OBJECT DeviceObject,
	_In_ PIRP           Irp,
	_In_ PVOID          Context
	)
{
	UNREFERENCED_PARAMETER(DeviceObject);

	ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_VIRTUSB, ">%!FUNC!");

	// If the lower driver didn't return STATUS_PENDING, we don't need to
	// set the event because we won't be waiting on it.
	if(Irp->PendingReturned)
	{
		KeSetEvent((PKEVENT)Context, IO_NO_INCREMENT, FALSE);
	}
	return STATUS_MORE_PROCESSING_REQUIRED; // Keep this IRP
}
