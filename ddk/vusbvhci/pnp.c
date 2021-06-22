#include "vusbvhci.h"
#include "pnp.h"
#include "busif.h"
#include "user_io.h"

NTSTATUS
VUsbVhci_FDO_PnP(
	IN PFDO_DEVICE_DATA FdoData,
	IN PIRP             Irp
	);

NTSTATUS
VUsbVhci_PDO_PnP(
	IN PPDO_DEVICE_DATA PdoData,
	IN PIRP             Irp
	);

NTSTATUS
VUsbVhci_PDO_QueryDeviceCaps(
	IN PPDO_DEVICE_DATA PdoData,
	IN PIRP             Irp
	);

NTSTATUS
VUsbVhci_PDO_QueryDeviceId(
	IN PPDO_DEVICE_DATA PdoData,
	IN PIRP             Irp
	);

NTSTATUS
VUsbVhci_PDO_QueryDeviceText(
	IN PPDO_DEVICE_DATA PdoData,
	IN PIRP             Irp
	);

NTSTATUS
VUsbVhci_PDO_QueryResources(
	IN PPDO_DEVICE_DATA PdoData,
	IN PIRP             Irp
	);

NTSTATUS
VUsbVhci_PDO_QueryResourceRequirements(
	IN PPDO_DEVICE_DATA PdoData,
	IN PIRP             Irp
	);

NTSTATUS
VUsbVhci_PDO_QueryDeviceRelations(
	IN PPDO_DEVICE_DATA PdoData,
	IN PIRP             Irp
	);

NTSTATUS
VUsbVhci_PDO_QueryBusInformation(
	IN PPDO_DEVICE_DATA PdoData,
	IN PIRP             Irp
	);

NTSTATUS
VUsbVhci_PDO_QueryInterface(
	IN PPDO_DEVICE_DATA PdoData,
	IN PIRP             Irp
	);

NTSTATUS
VUsbVhci_GetDeviceCapabilities(
	IN PDEVICE_OBJECT       DeviceObject,
	IN PDEVICE_CAPABILITIES DeviceCapabilities
	);

#if DBG
PCHAR
PnPMinorFunctionString(
	IN UCHAR MinorFunction
	);

PCHAR
DbgDeviceRelationString(
	IN DEVICE_RELATION_TYPE Type
	);

PCHAR
DbgDeviceIDString(
	IN BUS_QUERY_ID_TYPE Type
	);
#endif // DBG

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, VUsbVhci_DispatchPnP)
#pragma alloc_text(PAGE, VUsbVhci_FDO_PnP)
#pragma alloc_text(PAGE, VUsbVhci_PDO_PnP)
#pragma alloc_text(PAGE, VUsbVhci_PDO_QueryDeviceCaps)
#pragma alloc_text(PAGE, VUsbVhci_PDO_QueryDeviceId)
#pragma alloc_text(PAGE, VUsbVhci_PDO_QueryDeviceText)
#pragma alloc_text(PAGE, VUsbVhci_PDO_QueryResources)
#pragma alloc_text(PAGE, VUsbVhci_PDO_QueryResourceRequirements)
#pragma alloc_text(PAGE, VUsbVhci_PDO_QueryDeviceRelations)
#pragma alloc_text(PAGE, VUsbVhci_PDO_QueryBusInformation)
#pragma alloc_text(PAGE, VUsbVhci_PDO_QueryInterface)
#pragma alloc_text(PAGE, VUsbVhci_GetDeviceCapabilities)
#endif // ALLOC_PRAGMA

NTSTATUS
VUsbVhci_DispatchPnP(
	IN PDEVICE_OBJECT DeviceObject,
	IN PIRP           Irp
	)
/*
    Handles PnP Irps sent to both FDO and child PDO.
*/
{
	PIO_STACK_LOCATION  irpStack;
	NTSTATUS            status;
	PCOMMON_DEVICE_DATA commonData;

	PAGED_CODE();

	VUSBVHCI_KDPRINT("in VUsbVhci_DispatchPnP\n");

	irpStack = IoGetCurrentIrpStackLocation(Irp);
	ASSERT(IRP_MJ_PNP == irpStack->MajorFunction);

	commonData = (PCOMMON_DEVICE_DATA)DeviceObject->DeviceExtension;

	// If the device has been removed, the driver should
	// not pass the IRP down to the next lower driver.
	if(commonData->DevicePnPState == Deleted)
	{
		Irp->IoStatus.Status = status = STATUS_NO_SUCH_DEVICE;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		return status;
	}

	if(commonData->IsFDO)
	{
		VUSBVHCI_KDPRINT2("FDO %s IRP: 0x%p\n",
		                  PnPMinorFunctionString(irpStack->MinorFunction),
		                  Irp);

		// Request is for the bus FDO
		status = VUsbVhci_FDO_PnP((PFDO_DEVICE_DATA)commonData, Irp);
	}
	else
	{
		VUSBVHCI_KDPRINT2("PDO %s IRP: 0x%p\n",
		                  PnPMinorFunctionString(irpStack->MinorFunction),
		                  Irp);

		// Request is for the child PDO.
		status = VUsbVhci_PDO_PnP((PPDO_DEVICE_DATA)commonData, Irp);
	}

	return status;
}

#if DBG
PCHAR
PnPMinorFunctionString(
	IN UCHAR MinorFunction
	)
{
	switch(MinorFunction)
	{
	case IRP_MN_START_DEVICE:
		return "IRP_MN_START_DEVICE";
	case IRP_MN_QUERY_REMOVE_DEVICE:
		return "IRP_MN_QUERY_REMOVE_DEVICE";
	case IRP_MN_REMOVE_DEVICE:
		return "IRP_MN_REMOVE_DEVICE";
	case IRP_MN_CANCEL_REMOVE_DEVICE:
		return "IRP_MN_CANCEL_REMOVE_DEVICE";
	case IRP_MN_STOP_DEVICE:
		return "IRP_MN_STOP_DEVICE";
	case IRP_MN_QUERY_STOP_DEVICE:
		return "IRP_MN_QUERY_STOP_DEVICE";
	case IRP_MN_CANCEL_STOP_DEVICE:
		return "IRP_MN_CANCEL_STOP_DEVICE";
	case IRP_MN_QUERY_DEVICE_RELATIONS:
		return "IRP_MN_QUERY_DEVICE_RELATIONS";
	case IRP_MN_QUERY_INTERFACE:
		return "IRP_MN_QUERY_INTERFACE";
	case IRP_MN_QUERY_CAPABILITIES:
		return "IRP_MN_QUERY_CAPABILITIES";
	case IRP_MN_QUERY_RESOURCES:
		return "IRP_MN_QUERY_RESOURCES";
	case IRP_MN_QUERY_RESOURCE_REQUIREMENTS:
		return "IRP_MN_QUERY_RESOURCE_REQUIREMENTS";
	case IRP_MN_QUERY_DEVICE_TEXT:
		return "IRP_MN_QUERY_DEVICE_TEXT";
	case IRP_MN_FILTER_RESOURCE_REQUIREMENTS:
		return "IRP_MN_FILTER_RESOURCE_REQUIREMENTS";
	case IRP_MN_READ_CONFIG:
		return "IRP_MN_READ_CONFIG";
	case IRP_MN_WRITE_CONFIG:
		return "IRP_MN_WRITE_CONFIG";
	case IRP_MN_EJECT:
		return "IRP_MN_EJECT";
	case IRP_MN_SET_LOCK:
		return "IRP_MN_SET_LOCK";
	case IRP_MN_QUERY_ID:
		return "IRP_MN_QUERY_ID";
	case IRP_MN_QUERY_PNP_DEVICE_STATE:
		return "IRP_MN_QUERY_PNP_DEVICE_STATE";
	case IRP_MN_QUERY_BUS_INFORMATION:
		return "IRP_MN_QUERY_BUS_INFORMATION";
	case IRP_MN_DEVICE_USAGE_NOTIFICATION:
		return "IRP_MN_DEVICE_USAGE_NOTIFICATION";
	case IRP_MN_SURPRISE_REMOVAL:
		return "IRP_MN_SURPRISE_REMOVAL";
	case IRP_MN_QUERY_LEGACY_BUS_INFORMATION:
		return "IRP_MN_QUERY_LEGACY_BUS_INFORMATION";
	default:
		return "unknown_pnp_irp";
	}
}

PCHAR
DbgDeviceRelationString(
	IN DEVICE_RELATION_TYPE Type
	)
{
	switch(Type)
	{
	case BusRelations:
		return "BusRelations";
	case EjectionRelations:
		return "EjectionRelations";
	case RemovalRelations:
		return "RemovalRelations";
	case TargetDeviceRelation:
		return "TargetDeviceRelation";
	default:
		return "Unknown Relation";
	}
}

PCHAR
DbgDeviceIDString(
	IN BUS_QUERY_ID_TYPE Type
	)
{
	switch(Type)
	{
	case BusQueryDeviceID:
		return "BusQueryDeviceID";
	case BusQueryHardwareIDs:
		return "BusQueryHardwareIDs";
	case BusQueryCompatibleIDs:
		return "BusQueryCompatibleIDs";
	case BusQueryInstanceID:
		return "BusQueryInstanceID";
	case BusQueryDeviceSerialNumber:
		return "BusQueryDeviceSerialNumber";
	default:
		return "Unknown ID";
	}
}
#endif // DBG

NTSTATUS
VUsbVhci_FDO_PnP(
	IN PFDO_DEVICE_DATA FdoData,
	IN PIRP             Irp
	)
/*
    Handle requests from the Plug & Play system for the host controller itself
*/
{
	NTSTATUS               status;
	ULONG                  length, prevcount;
	PDEVICE_OBJECT         fdo = FdoData->Self;
	PPDO_DEVICE_DATA       pdoData;
	PIO_STACK_LOCATION     irpStack = IoGetCurrentIrpStackLocation(Irp);
	PDEVICE_RELATIONS      relations, oldRelations;
	BOOLEAN                pdoPresent;
	PINTERFACE_DEREFERENCE deref;
	PVOID                  context;

	PAGED_CODE();

	VUSBVHCI_KDPRINT("in VUsbVhci_FDO_PnP\n");

	if(!NT_SUCCESS(IoAcquireRemoveLock(&FdoData->RemoveLock, Irp)))
	{
		Irp->IoStatus.Status = status = STATUS_NO_SUCH_DEVICE;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		return status;
	}

	switch(irpStack->MinorFunction)
	{
	case IRP_MN_START_DEVICE:
		// Send the Irp down and wait for it to come back.
		status = VUsbVhci_SendIrpSynchronously(FdoData->NextLowerDriver, Irp);
		if(NT_SUCCESS(status))
		{
			status = VUsbVhci_StartFdo(FdoData);
		}

		// If the FDO was started successful and we do not have a root-hub -- that is
		// the case, if we are started the first time -- then we'll create it now.
		if(NT_SUCCESS(status) && !FdoData->RHubPdo)
		{
			VUsbVhci_CreatePdo(FdoData);
			// (ignore hub failure)

			// We don't need to call IoInvalidateDeviceRelations.
			// The IO manager always asks for our bus relations
			// after he started us.

			// We register the interface for the root-hub when
			// someone asks for the name of its symlink. The
			// interface registration will fail, if we do it here.
			// I don't know exactly why. Maybe the IO manager
			// is not able to fully initialize our PDO while we are
			// stucked here -- so IoRegisterDeviceInterface can't find
			// the PDO.
		}

		// We must now complete the IRP, since we stopped it in the
		// completion routine with MORE_PROCESSING_REQUIRED.
		Irp->IoStatus.Status = status;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		IoReleaseRemoveLock(&FdoData->RemoveLock, Irp);
		return status;

	case IRP_MN_QUERY_STOP_DEVICE:
		// The PnP manager is trying to stop the device
		// for resource rebalancing. Since we do not use any resources,
		// this is useless.
		Irp->IoStatus.Status = status = STATUS_UNSUCCESSFUL;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		IoReleaseRemoveLock(&FdoData->RemoveLock, Irp);
		return status;

	case IRP_MN_CANCEL_STOP_DEVICE:
		if(StopPending == FdoData->DevicePnPState)
		{
			// We did receive a query-stop, so restore.
			RESTORE_PREVIOUS_PNP_STATE(FdoData);
			ASSERT(FdoData->DevicePnPState == Started);
		}
		Irp->IoStatus.Status = STATUS_SUCCESS;
		break;

	case IRP_MN_STOP_DEVICE:
		SET_NEW_PNP_STATE(FdoData, Stopped);
		Irp->IoStatus.Status = STATUS_SUCCESS; // We must not fail the IRP.
		break;

	case IRP_MN_QUERY_REMOVE_DEVICE:
		SET_NEW_PNP_STATE(FdoData, RemovePending);
		Irp->IoStatus.Status = STATUS_SUCCESS;
		break;

	case IRP_MN_CANCEL_REMOVE_DEVICE:
		// First check to see whether you have received cancel-remove
		// without first receiving a query-remove. This could happen if
		// someone above us fails a query-remove and passes down the
		// subsequent cancel-remove.
		if(RemovePending == FdoData->DevicePnPState)
		{
			// We did receive a query-remove, so restore.
			RESTORE_PREVIOUS_PNP_STATE(FdoData);
		}
		Irp->IoStatus.Status = STATUS_SUCCESS;
		break;

	case IRP_MN_SURPRISE_REMOVAL:
		// The device has been unexpectedly removed from the machine
		// and is no longer available for I/O.

		SET_NEW_PNP_STATE(FdoData, SurpriseRemovePending);

		VUsbVhci_FdoInterfaceDown(FdoData);

		if(FdoData->RHubPdo)
		{
			pdoData = PDO_FROM_FDO(FdoData);
			ExAcquireFastMutex(&pdoData->Mutex);
			pdoData->ReportedMissing = TRUE;
			ExReleaseFastMutex(&pdoData->Mutex);
		}

		Irp->IoStatus.Status = STATUS_SUCCESS; // We must not fail the IRP.
		break;

	case IRP_MN_REMOVE_DEVICE:
		// The Plug & Play system has dictated the removal of this device.
		// We have no choice but to detach and delete the device object.

		// Typically the system removes all the children before
		// removing the parent FDO. If for any reason child Pdos are
		// still present we will destroy them explicitly, with one exception --
		// we will not delete the PDOs that are in SurpriseRemovePending state.

		if(FdoData->RHubPdo)
		{
			BOOLEAN destroyPdo = TRUE;
			pdoData = PDO_FROM_FDO(FdoData);
			ExAcquireFastMutex(&pdoData->Mutex);
			if(SurpriseRemovePending == pdoData->DevicePnPState)
			{
				destroyPdo = FALSE;
				VUSBVHCI_KDPRINT1("Root hub pdo was surprise removed: 0x%p\n", pdoData->Self);
				// We set ReportedMissing to TRUE, because this will trigger
				// the destruction of the PDO if the PnP manager removes it.
				pdoData->ReportedMissing = TRUE;
			}
			ExReleaseFastMutex(&pdoData->Mutex);
			if(destroyPdo)
			{
				VUsbVhci_DestroyPdo(pdoData);
				VUSBVHCI_KDPRINT1("Deleting PDO: 0x%p\n", pdoData->Self);
				IoDeleteDevice(pdoData->Self);
			}
		}

		if(FdoData->DevicePnPState != SurpriseRemovePending)
		{
			VUsbVhci_FdoInterfaceDown(FdoData);
		}

		// Dereference (and wipe) VirtUsb-interface
		deref = FdoData->VirtUsbInterface.InterfaceDereference;
		context = FdoData->VirtUsbInterface.Context;
		RtlZeroMemory(&FdoData->VirtUsbInterface, sizeof(FdoData->VirtUsbInterface));
		if(deref)
		{
			KeMemoryBarrier();
			deref(context);
		}

		SET_NEW_PNP_STATE(FdoData, Deleted);

		IoReleaseRemoveLockAndWait(&FdoData->RemoveLock, Irp);

		// We need to send the remove down the stack before we detach,
		// but we don't need to wait for the completion of this operation
		// (and to register a completion routine).
		Irp->IoStatus.Status = STATUS_SUCCESS;
		IoSkipCurrentIrpStackLocation(Irp);
		status = IoCallDriver(FdoData->NextLowerDriver, Irp);

		// Detach from the underlying devices.
		IoDetachDevice(FdoData->NextLowerDriver);

		if(!FdoData->SymlinkFailed)
		{
			WCHAR          cLnkName[64];
			UNICODE_STRING lnkName;
			RtlStringCchPrintfW(cLnkName,
			                    RTL_NUMBER_OF(cLnkName),
			                    FDOSymlinkFormatString, // \DosDevices\HCDn
			                    FdoData->UsbDeviceNumber);
			RtlInitUnicodeString(&lnkName, cLnkName);
			IoDeleteSymbolicLink(&lnkName);
		}

		VUSBVHCI_KDPRINT1("Deleting FDO: 0x%p\n", fdo);
		IoDeleteDevice(fdo);

		return status;

	case IRP_MN_QUERY_DEVICE_RELATIONS:
		VUSBVHCI_KDPRINT1("QueryDeviceRelation Type: %s\n",
		                  DbgDeviceRelationString(irpStack->Parameters.QueryDeviceRelations.Type));

		if(BusRelations != irpStack->Parameters.QueryDeviceRelations.Type)
		{
			// We don't support any other Device Relations
			break;
		}

		// Tell the plug and play system about all the PDOs.
		//
		// There might also be device relations below and above this FDO,
		// so, be sure to propagate the relations from the upper drivers.
		//
		// No Completion routine is needed so long as the status is preset
		// to success. (PDOs complete plug and play irps with the current
		// IoStatus.Status and IoStatus.Information as the default.)

		oldRelations = (PDEVICE_RELATIONS)Irp->IoStatus.Information;

		if(!FdoData->RHubPdo)
		{
			if(!oldRelations)
			{
				length = FIELD_OFFSET(DEVICE_RELATIONS, Objects[0]);
				relations = (PDEVICE_RELATIONS)ExAllocatePoolWithTag(PagedPool,
				                                                     length,
				                                                     VUSBVHCI_NOT_OWNER_POOL_TAG);
				if(!relations)
				{
					// Fail the IRP
					Irp->IoStatus.Status = status = STATUS_INSUFFICIENT_RESOURCES;
					IoCompleteRequest(Irp, IO_NO_INCREMENT);
					IoReleaseRemoveLock(&FdoData->RemoveLock, Irp);
					return status;
				}
				relations->Count = 0;
				Irp->IoStatus.Information = (ULONG_PTR)relations;
				Irp->IoStatus.Status = STATUS_SUCCESS;
			}
			break;
		}

		pdoData = PDO_FROM_FDO(FdoData);
		ExAcquireFastMutex(&pdoData->Mutex);

		pdoPresent = pdoData->Present;

		if(oldRelations)
		{
			prevcount = oldRelations->Count;
			if(!pdoPresent)
			{
				// There is a device relations struct already present and we have
				// nothing to add to it, so just call IoSkip and IoCall
				ExReleaseFastMutex(&pdoData->Mutex);
				break;
			}
		}
		else
		{
			prevcount = 0;
		}

		// Need to allocate a new relations structure and add our
		// PDO to it.
		length = FIELD_OFFSET(DEVICE_RELATIONS, Objects[prevcount + !!pdoPresent]);
		relations = (PDEVICE_RELATIONS)ExAllocatePoolWithTag(PagedPool,
		                                                     length,
		                                                     VUSBVHCI_NOT_OWNER_POOL_TAG);

		if(!relations)
		{
			// Fail the IRP
			ExReleaseFastMutex(&pdoData->Mutex);
			Irp->IoStatus.Status = status = STATUS_INSUFFICIENT_RESOURCES;
			IoCompleteRequest(Irp, IO_NO_INCREMENT);
			IoReleaseRemoveLock(&FdoData->RemoveLock, Irp);
			return status;
		}

		// Copy in the device objects so far
		if(prevcount)
		{
			RtlCopyMemory(relations->Objects,
			              oldRelations->Objects,
			              prevcount * sizeof(PDEVICE_OBJECT));
		}

		relations->Count = prevcount + !!pdoPresent;

		if(pdoPresent)
		{
			relations->Objects[prevcount] = pdoData->Self;
			ObReferenceObject(pdoData->Self);
		}
		else
		{
			pdoData->ReportedMissing = TRUE;
		}

		ExReleaseFastMutex(&pdoData->Mutex);

		VUSBVHCI_KDPRINT1("#PDOS present = %lu\n", (ULONG)!!pdoPresent);
		VUSBVHCI_KDPRINT1("#PDOs reported = %lu\n", relations->Count);

		// Replace the relations structure in the IRP with the new
		// one.
		if(oldRelations)
		{
			ExFreePool(oldRelations);
		}
		Irp->IoStatus.Information = (ULONG_PTR)relations;

		// Set up and pass the IRP further down the stack
		Irp->IoStatus.Status = STATUS_SUCCESS;
		break;

	default:
		// In the default case we merely call the next driver.
		// We must not modify Irp->IoStatus.Status or complete the IRP.
		break;
	}

	IoSkipCurrentIrpStackLocation(Irp);
	status = IoCallDriver(FdoData->NextLowerDriver, Irp);
	IoReleaseRemoveLock(&FdoData->RemoveLock, Irp);
	return status;
}

NTSTATUS
VUsbVhci_PDO_PnP(
	IN PPDO_DEVICE_DATA PdoData,
	IN PIRP             Irp
	)
/*
    Handle requests from the Plug & Play system for the devices on the BUS
*/
{
	PIO_STACK_LOCATION irpStack = IoGetCurrentIrpStackLocation(Irp);
	NTSTATUS           status;

	PAGED_CODE();

	VUSBVHCI_KDPRINT("in VUsbVhci_PDO_PnP\n");

	if(!NT_SUCCESS(IoAcquireRemoveLock(&PdoData->RemoveLock, Irp)))
	{
		Irp->IoStatus.Status = status = STATUS_NO_SUCH_DEVICE;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		return status;
	}

	// NB: Because we are a bus enumerator, we have no one to whom we could
	// defer these irps. Therefore we do not pass them down but merely
	// return them.

	switch(irpStack->MinorFunction)
	{
	case IRP_MN_START_DEVICE:
		// Here we do what ever initialization and ``turning on'' that is
		// required to allow others to access this device.
		// Power up the device.
		PdoData->DevicePowerState = PowerDeviceD0;
		SET_NEW_PNP_STATE(PdoData, Started);
		status = VUsbVhci_GetAndStoreRootHubName(PdoData);
		if(NT_SUCCESS(status))
		{
			status = IoSetDeviceInterfaceState(&PdoData->InterfaceName, TRUE);
			if(!NT_SUCCESS(status))
			{
				VUSBVHCI_KDPRINT1("IoSetDeviceInterfaceState failed: 0x%08lx\n", status);
			}
		}
		else
		{
			VUSBVHCI_KDPRINT1("VUsbVhci_GetAndStoreRootHubName failed: 0x%08lx\n", status);
		}
		status = STATUS_SUCCESS;
		break;

	case IRP_MN_STOP_DEVICE:
		SET_NEW_PNP_STATE(PdoData, Stopped);
		status = STATUS_SUCCESS; // We must not fail this IRP.
		break;

	case IRP_MN_QUERY_STOP_DEVICE:
		// No reason here why we can't stop the device.
		SET_NEW_PNP_STATE(PdoData, StopPending);
		status = STATUS_SUCCESS;
		break;

	case IRP_MN_CANCEL_STOP_DEVICE:
		if(StopPending == PdoData->DevicePnPState)
		{
			// We did receive a query-stop, so restore.
			RESTORE_PREVIOUS_PNP_STATE(PdoData);
		}
		status = STATUS_SUCCESS;
		break;

	case IRP_MN_QUERY_REMOVE_DEVICE:
		// Check to see whether the device can be removed safely.
		// If not fail this request. This is the last opportunity
		// to do so.
		SET_NEW_PNP_STATE(PdoData, RemovePending);
		status = STATUS_SUCCESS;
		break;

	case IRP_MN_CANCEL_REMOVE_DEVICE:
		if(RemovePending == PdoData->DevicePnPState)
		{
			// We did receive a query-remove, so restore.
			RESTORE_PREVIOUS_PNP_STATE(PdoData);
		}
		status = STATUS_SUCCESS;
		break;

	case IRP_MN_SURPRISE_REMOVAL:
		SET_NEW_PNP_STATE(PdoData, SurpriseRemovePending);
		if(PdoData->InterfaceName.Buffer)
		{
			status = IoSetDeviceInterfaceState(&PdoData->InterfaceName, FALSE);
			if(!NT_SUCCESS(status))
			{
				VUSBVHCI_KDPRINT1("IoSetDeviceInterfaceState failed: 0x%08lx\n", status);
			}
		}
		status = STATUS_SUCCESS; // We must not fail this IRP.
		break;

	case IRP_MN_REMOVE_DEVICE:
		// We will delete the PDO only after we have reported to the
		// Plug and Play manager that it's missing.
		if(PdoData->DevicePnPState != SurpriseRemovePending &&
		   PdoData->InterfaceName.Buffer)
		{
			status = IoSetDeviceInterfaceState(&PdoData->InterfaceName, FALSE);
			if(!NT_SUCCESS(status))
			{
				VUSBVHCI_KDPRINT1("IoSetDeviceInterfaceState failed: 0x%08lx\n", status);
			}
		}
		if(PdoData->ReportedMissing)
		{
			PFDO_DEVICE_DATA fdoData;

			SET_NEW_PNP_STATE(PdoData, Deleted);
			IoReleaseRemoveLockAndWait(&PdoData->RemoveLock, Irp);

			if(PdoData->ParentFdo)
			{
				fdoData = FDO_FROM_PDO(PdoData);
				ASSERT(fdoData->RHubPdo);
				fdoData->RHubPdo = NULL;
			}

			// Free up resources associated with PDO and delete it.
			VUsbVhci_DestroyPdo(PdoData);
			VUSBVHCI_KDPRINT1("Deleting PDO: 0x%p\n", PdoData->Self);
			IoDeleteDevice(PdoData->Self);

			Irp->IoStatus.Status = status = STATUS_SUCCESS; // We must not fail this IRP.
			IoCompleteRequest(Irp, IO_NO_INCREMENT);
			return status;
		}
		if(PdoData->Present)
		{
			// When the device is disabled, the PDO transitions from
			// RemovePending to NotStarted. We shouldn't delete
			// the PDO because a) the device is still present on the bus,
			// b) we haven't reported missing to the PnP manager.
			SET_NEW_PNP_STATE(PdoData, NotStarted);
			status = STATUS_SUCCESS; // We must not fail this IRP.
		}
		else
		{
			ASSERT(PdoData->Present);
			status = STATUS_SUCCESS; // We must not fail this IRP.
		}
		break;

	case IRP_MN_QUERY_CAPABILITIES:
		// Return the capabilities of a device, such as whether the device
		// can be locked or ejected..etc
		status = VUsbVhci_PDO_QueryDeviceCaps(PdoData, Irp);
		break;

	case IRP_MN_QUERY_ID:
		// Query the IDs of the device
		VUSBVHCI_KDPRINT1("QueryId Type: %s\n",
			DbgDeviceIDString(irpStack->Parameters.QueryId.IdType));
		status = VUsbVhci_PDO_QueryDeviceId(PdoData, Irp);
		break;

	case IRP_MN_QUERY_DEVICE_RELATIONS:
		VUSBVHCI_KDPRINT1("QueryDeviceRelation Type: %s\n",
			DbgDeviceRelationString(irpStack->Parameters.QueryDeviceRelations.Type));
		status = VUsbVhci_PDO_QueryDeviceRelations(PdoData, Irp);
		break;

	case IRP_MN_QUERY_DEVICE_TEXT:
		status = VUsbVhci_PDO_QueryDeviceText(PdoData, Irp);
		break;

	case IRP_MN_QUERY_RESOURCES:
		status = VUsbVhci_PDO_QueryResources(PdoData, Irp);
		break;

	case IRP_MN_QUERY_RESOURCE_REQUIREMENTS:
		status = VUsbVhci_PDO_QueryResourceRequirements(PdoData, Irp);
		break;

	case IRP_MN_QUERY_BUS_INFORMATION:
		status = VUsbVhci_PDO_QueryBusInformation(PdoData, Irp);
		break;

	case IRP_MN_DEVICE_USAGE_NOTIFICATION:
		// OPTIONAL for bus drivers.
		// This bus drivers any of the bus's descendants
		// (child device, child of a child device, etc.) do not
		// contain a memory file namely paging file, dump file,
		// or hibernation file. So we fail this Irp.
		status = STATUS_UNSUCCESSFUL;
		break;

	case IRP_MN_EJECT:
		// For the device to be ejected, the device must be in the D3
		// device power state (off) and must be unlocked
		// (if the device supports locking). Any driver that returns success
		// for this IRP must wait until the device has been ejected before
		// completing the IRP.
		ExAcquireFastMutex(&PdoData->Mutex);
		PdoData->Present = FALSE;
		ExReleaseFastMutex(&PdoData->Mutex);
		status = STATUS_SUCCESS;
		break;

	case IRP_MN_QUERY_INTERFACE:
		// This request enables a driver to export a direct-call
		// interface to other drivers. A bus driver that exports
		// an interface must handle this request for its child
		// devices (child PDOs).
		status = VUsbVhci_PDO_QueryInterface(PdoData, Irp);
		break;

	//case IRP_MN_FILTER_RESOURCE_REQUIREMENTS:
		// OPTIONAL for bus drivers.
		// The PnP Manager sends this IRP to a device
		// stack so filter and function drivers can adjust the
		// resources required by the device, if appropriate.
		//break;

	//case IRP_MN_QUERY_PNP_DEVICE_STATE:
		// OPTIONAL for bus drivers.
		// The PnP Manager sends this IRP after the drivers for
		// a device return success from the IRP_MN_START_DEVICE
		// request. The PnP Manager also sends this IRP when a
		// driver for the device calls IoInvalidateDeviceState.
		//break;

	//case IRP_MN_READ_CONFIG:
	//case IRP_MN_WRITE_CONFIG:
		// Bus drivers for buses with configuration space must handle
		// this request for their child devices. Our devices don't
		// have a config space.
		//break;

	//case IRP_MN_SET_LOCK:
		// Our device is not a lockable device
		// so we don't support this Irp.
		//break;

	default:
		//VUSBVHCI_KDPRINT("Not handled\n");
		// For PnP requests to the PDO that we do not understand we should
		// return the IRP WITHOUT setting the status or information fields.
		// These fields may have already been set by a filter (eg acpi).
		status = Irp->IoStatus.Status;
		break;
	}

	Irp->IoStatus.Status = status;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	IoReleaseRemoveLock(&PdoData->RemoveLock, Irp);
	return status;
}

NTSTATUS
VUsbVhci_PDO_QueryDeviceCaps(
	IN PPDO_DEVICE_DATA PdoData,
	IN PIRP             Irp
	)
/*
    When a device is enumerated, but before the function and
    filter drivers are loaded for the device, the PnP Manager
    sends an IRP_MN_QUERY_CAPABILITIES request to the parent
    bus driver for the device. The bus driver must set any
    relevant values in the DEVICE_CAPABILITIES structure and
    return it to the PnP Manager.
*/
{
	PIO_STACK_LOCATION   stack;
	PDEVICE_CAPABILITIES deviceCapabilities;
	DEVICE_CAPABILITIES  parentCapabilities;
	NTSTATUS             status;

	PAGED_CODE();

	VUSBVHCI_KDPRINT("in VUsbVhci_PDO_QueryDeviceCaps\n");

	ASSERT(PdoData);

	stack = IoGetCurrentIrpStackLocation(Irp);

	// Get the packet.
	deviceCapabilities = stack->Parameters.DeviceCapabilities.Capabilities;

	// Set the capabilities.
	if(deviceCapabilities->Version != 1 ||
	   deviceCapabilities->Size < sizeof(DEVICE_CAPABILITIES))
	{
		return STATUS_UNSUCCESSFUL;
	}

	// Get the device capabilities of the parent
	status = VUsbVhci_GetDeviceCapabilities(
		FDO_FROM_PDO(PdoData)->NextLowerDriver, &parentCapabilities);
	if(!NT_SUCCESS(status))
	{
		VUSBVHCI_KDPRINT("QueryDeviceCaps failed\n");
		return status;
	}

	// The entries in the DeviceState array are based on the capabilities
	// of the parent devnode. These entries signify the highest-powered
	// state that the device can support for the corresponding system
	// state. A driver can specify a lower (less-powered) state than the
	// bus driver.  For eg: Suppose the toaster bus controller supports
	// D0, D2, and D3; and the Toaster Device supports D0, D1, D2, and D3.
	// Following the above rule, the device cannot specify D1 as one of
	// it's power state. A driver can make the rules more restrictive
	// but cannot loosen them.
	// First copy the parent's S to D state mapping
	RtlCopyMemory(deviceCapabilities->DeviceState,
	              parentCapabilities.DeviceState,
	              (PowerSystemShutdown + 1) * sizeof(DEVICE_POWER_STATE));

	// Adjust the caps to what your device supports.
	// Our device just supports D0 and D3.
	deviceCapabilities->DeviceState[PowerSystemWorking] = PowerDeviceD0;

	if(deviceCapabilities->DeviceState[PowerSystemSleeping1] != PowerDeviceD0)
		deviceCapabilities->DeviceState[PowerSystemSleeping1] = PowerDeviceD1;

	if(deviceCapabilities->DeviceState[PowerSystemSleeping2] != PowerDeviceD0)
		deviceCapabilities->DeviceState[PowerSystemSleeping2] = PowerDeviceD3;

	if(deviceCapabilities->DeviceState[PowerSystemSleeping3] != PowerDeviceD0)
		deviceCapabilities->DeviceState[PowerSystemSleeping3] = PowerDeviceD3;

	// We can wake the system from D1
	deviceCapabilities->DeviceWake = PowerDeviceD1;

	// Specifies whether the device hardware supports the D1 and D2
	// power state. Set these bits explicitly.
	deviceCapabilities->DeviceD1 = TRUE; // Yes we can
	deviceCapabilities->DeviceD2 = FALSE;

	// Specifies whether the device can respond to an external wake
	// signal while in the D0, D1, D2, and D3 state.
	// Set these bits explicitly.
	deviceCapabilities->WakeFromD0 = FALSE;
	deviceCapabilities->WakeFromD1 = TRUE; //Yes we can
	deviceCapabilities->WakeFromD2 = FALSE;
	deviceCapabilities->WakeFromD3 = FALSE;


	// We have no latencies
	deviceCapabilities->D1Latency = 0;
	deviceCapabilities->D2Latency = 0;
	deviceCapabilities->D3Latency = 0;

	// Ejection supported
	deviceCapabilities->EjectSupported = FALSE;

	// This flag specifies whether the device's hardware is disabled.
	// The PnP Manager only checks this bit right after the device is
	// enumerated. Once the device is started, this bit is ignored.
	deviceCapabilities->HardwareDisabled = FALSE;

	// Our simulated device can be physically removed.
	deviceCapabilities->Removable = TRUE;

	// Setting it to TURE prevents the warning dialog from appearing
	// whenever the device is surprise removed.
	deviceCapabilities->SurpriseRemovalOK = TRUE;

	// We don't support system-wide unique IDs.
	deviceCapabilities->UniqueID = FALSE;

	// Specify whether the Device Manager should suppress all
	// installation pop-ups except required pop-ups such as
	// "no compatible drivers found."
	deviceCapabilities->SilentInstall = FALSE; //TRUE;  **********

	// Specifies an address indicating where the device is located
	// on its underlying bus. The interpretation of this number is
	// bus-specific. If the address is unknown or the bus driver
	// does not support an address, the bus driver leaves this
	// member at its default value of 0xFFFFFFFF. In this example
	// the location address is same as instance id.
	deviceCapabilities->Address = 1; // TODO: get address from USB device? is this the right value for this field? what are "real" root-hubs doing here?

	// UINumber specifies a number associated with the device that can
	// be displayed in the user interface.
	deviceCapabilities->UINumber = 0; // "Real" root-hubs show 0 here in device manager -- so do we

	return STATUS_SUCCESS;
}

NTSTATUS
VUsbVhci_PDO_QueryDeviceId(
	IN PPDO_DEVICE_DATA PdoData,
	IN PIRP             Irp
	)
/*
    Bus drivers must handle BusQueryDeviceID requests for their
    child devices (child PDOs). Bus drivers can handle requests
    BusQueryHardwareIDs, BusQueryCompatibleIDs, and BusQueryInstanceID
    for their child devices.

    When returning more than one ID for hardware IDs or compatible IDs,
    a driver should list the IDs in the order of most specific to most
    general to facilitate choosing the best driver match for the device.

    Bus drivers should be prepared to handle this IRP for a child device
    immediately after the device is enumerated.
*/
{
	PIO_STACK_LOCATION stack;
	PWCHAR             buffer;
	ULONG              length;
	NTSTATUS           status = STATUS_SUCCESS;

	PAGED_CODE();

	UNREFERENCED_PARAMETER(PdoData);

	VUSBVHCI_KDPRINT("in VUsbVhci_PDO_QueryDeviceId\n");

	stack = IoGetCurrentIrpStackLocation(Irp);

	switch(stack->Parameters.QueryId.IdType)
	{
	case BusQueryDeviceID:
		// The DeviceID is an unique string to identify identical devices.

		buffer = (PWCHAR)RootHubDeviceID;
		while(*(buffer++));
		length = (ULONG)(buffer - RootHubDeviceID) * sizeof(WCHAR);
		ASSERT(length >= sizeof(WCHAR));
		ASSERT(length ^ 1);

		buffer = ExAllocatePoolWithTag(PagedPool, length, VUSBVHCI_NOT_OWNER_POOL_TAG);
		if(!buffer)
		{
			status = STATUS_INSUFFICIENT_RESOURCES;
			break;
		}

		RtlCopyMemory(buffer, RootHubDeviceID, length);
		ASSERT(!buffer[length / sizeof(WCHAR) - 1]);
		Irp->IoStatus.Information = (ULONG_PTR)buffer;
		VUSBVHCI_KDPRINT2("DeviceId: (%lu)\"%ws\"\n", length / sizeof(WCHAR) - 1, buffer);
		break;

	case BusQueryHardwareIDs:
		// A device has at least one hardware id.
		// In a list of hardware IDs (multi_sz string) for a device,
		// DeviceId is the most specific and should be first in the list.

		buffer = (PWCHAR)RootHubHardwareIDs;
		do
		{
			while(*(buffer++));
		} while(*(buffer++));
		length = (ULONG)(buffer - RootHubHardwareIDs) * sizeof(WCHAR);
		ASSERT(length >= 2 * sizeof(WCHAR));
		ASSERT(length ^ 1);

		buffer = ExAllocatePoolWithTag(PagedPool, length, VUSBVHCI_NOT_OWNER_POOL_TAG);
		if(!buffer)
		{
			status = STATUS_INSUFFICIENT_RESOURCES;
			break;
		}

		RtlCopyMemory(buffer, RootHubHardwareIDs, length);
		ASSERT(!buffer[length / sizeof(WCHAR) - 2]);
		ASSERT(!buffer[length / sizeof(WCHAR) - 1]);
		Irp->IoStatus.Information = (ULONG_PTR)buffer;

#if DBG
		{
			ULONG cnt = 0;
			do
			{
				PWCHAR start = buffer;
				while(*(buffer++));
				if(*start)
				{
					VUSBVHCI_KDPRINT3("HardwareId[%lu]: (%lu)\"%ws\"\n", cnt, (ULONG)(ULONG_PTR)(buffer - start) - 1, start);
					cnt++;
				}
			} while(*buffer);
			VUSBVHCI_KDPRINT1("#HardwareIds: %lu\n", cnt);
		}
#endif
		break;

	case BusQueryInstanceID:
	case BusQueryCompatibleIDs:
		Irp->IoStatus.Information = 0;
		break;

	default:
		status = Irp->IoStatus.Status;
		break;
	}

	return status;
}

NTSTATUS
VUsbVhci_PDO_QueryDeviceText(
	IN PPDO_DEVICE_DATA PdoData,
	IN PIRP             Irp
	)
/*
    The PnP Manager uses this IRP to get a device's
    description or location information. This string
    is displayed in the "found new hardware" pop-up
    window if no INF match is found for the device.
    Bus drivers are also encouraged to return location
    information for their child devices, but this information
    is optional.
*/
{
	PWCHAR             buffer;
	ULONG              length;
	PIO_STACK_LOCATION stack;
	NTSTATUS           status;

	PAGED_CODE();

	UNREFERENCED_PARAMETER(PdoData);

	VUSBVHCI_KDPRINT("in VUsbVhci_PDO_QueryDeviceText\n");

	stack = IoGetCurrentIrpStackLocation(Irp);

	switch(stack->Parameters.QueryDeviceText.DeviceTextType)
	{
	case DeviceTextDescription:
		// Check to see if any filter driver has set any information.
		// If so then remain silent otherwise add your description.
		// This string must be localized to support various languages.
		switch(stack->Parameters.QueryDeviceText.LocaleId)
		{
		case 0x00000407: // German
			// Localize the device text.
			// Until we implement let us fallthru to English
		default: // for all other languages, fallthru to English
		case 0x00000409: // English
			if(!Irp->IoStatus.Information)
			{
				buffer = (PWCHAR)RootHubText;
				while(*(buffer++));
				length = (ULONG)(buffer - RootHubText) * sizeof(WCHAR);
				ASSERT(length >= sizeof(WCHAR));
				ASSERT(length ^ 1);

				buffer = ExAllocatePoolWithTag(PagedPool, length, VUSBVHCI_NOT_OWNER_POOL_TAG);
				if(!buffer)
				{
					status = STATUS_INSUFFICIENT_RESOURCES;
					break;
				}

				RtlCopyMemory(buffer, RootHubText, length);
				ASSERT(!buffer[length / sizeof(WCHAR) - 1]);
				Irp->IoStatus.Information = (ULONG_PTR)buffer;
				VUSBVHCI_KDPRINT2("Text: (%lu)\"%ws\"\n", length / sizeof(WCHAR) - 1, buffer);
			}
			status = STATUS_SUCCESS;
			break;
		}
		break;

	case DeviceTextLocationInformation:
	default:
		status = Irp->IoStatus.Status;
		break;
	}

	return status;
}

NTSTATUS
VUsbVhci_PDO_QueryResources(
	IN PPDO_DEVICE_DATA PdoData,
	IN PIRP             Irp
	)
/*
    The PnP Manager uses this IRP to get a device's
    boot configuration resources. The bus driver returns
    a resource list in response to this IRP, it allocates
    a CM_RESOURCE_LIST from paged memory. The PnP Manager
    frees the buffer when it is no longer needed.
*/
{
	PAGED_CODE();

	UNREFERENCED_PARAMETER(PdoData);

	VUSBVHCI_KDPRINT("in VUsbVhci_PDO_QueryResources\n");

	// we don't have any boot configuration resources
	return Irp->IoStatus.Status;
}

NTSTATUS
VUsbVhci_PDO_QueryResourceRequirements(
	IN PPDO_DEVICE_DATA PdoData,
	IN PIRP             Irp
	)
/*
    The PnP Manager uses this IRP to get a device's alternate
    resource requirements list. The bus driver returns a resource
    requirements list in response to this IRP, it allocates an
    IO_RESOURCE_REQUIREMENTS_LIST from paged memory. The PnP
    Manager frees the buffer when it is no longer needed.
*/
{
	PAGED_CODE();

	UNREFERENCED_PARAMETER(PdoData);

	VUSBVHCI_KDPRINT("in VUsbVhci_PDO_QueryResourceRequirements\n");

	// we don't have any boot configuration resources
	return Irp->IoStatus.Status;
}

NTSTATUS
VUsbVhci_PDO_QueryDeviceRelations(
	IN PPDO_DEVICE_DATA PdoData,
	IN PIRP             Irp
	)
/*
    The PnP Manager sends this IRP to gather information about
    devices with a relationship to the specified device.
    Bus drivers must handle this request for TargetDeviceRelation
    for their child devices (child PDOs).

    If a driver returns relations in response to this IRP,
    it allocates a DEVICE_RELATIONS structure from paged
    memory containing a count and the appropriate number of
    device object pointers. The PnP Manager frees the structure
    when it is no longer needed. If a driver replaces a
    DEVICE_RELATIONS structure allocated by another driver,
    it must free the previous structure.

    A driver must reference the PDO of any device that it
    reports in this IRP (ObReferenceObject). The PnP Manager
    removes the reference when appropriate.
*/
{
	PIO_STACK_LOCATION stack;
	PDEVICE_RELATIONS  deviceRelations;
	NTSTATUS           status;

	PAGED_CODE();

	VUSBVHCI_KDPRINT("in VUsbVhci_PDO_QueryDeviceRelations\n");

	ASSERT(PdoData);
	ASSERT(Irp);

	stack = IoGetCurrentIrpStackLocation(Irp);

	switch(stack->Parameters.QueryDeviceRelations.Type)
	{
	case TargetDeviceRelation:
		deviceRelations = (PDEVICE_RELATIONS)Irp->IoStatus.Information;
		if(deviceRelations)
		{
			// Only PDO can handle this request. Somebody above
			// is not playing by rule.
			ASSERTMSG("Someone above is handling TargetDeviceRelation", !deviceRelations);
		}

		deviceRelations = (PDEVICE_RELATIONS)
			ExAllocatePoolWithTag(PagedPool,
			                      FIELD_OFFSET(DEVICE_RELATIONS, Objects[1]),
			                      VUSBVHCI_NOT_OWNER_POOL_TAG);
		if(!deviceRelations)
		{
			status = STATUS_INSUFFICIENT_RESOURCES;
			break;
		}

		// There is only one PDO pointer in the structure
		// for this relation type. The PnP Manager removes
		// the reference to the PDO when the driver or application
		// un-registers for notification on the device.
		deviceRelations->Count = 1;
		deviceRelations->Objects[0] = PdoData->Self;
		ObReferenceObject(PdoData->Self);

		status = STATUS_SUCCESS;
		Irp->IoStatus.Information = (ULONG_PTR)deviceRelations;
		break;

	case BusRelations:      // Not handled by PDO
	case EjectionRelations: // optional for PDO
	case RemovalRelations:  // optional for PDO
	default:
		status = Irp->IoStatus.Status;
	}

	return status;
}

NTSTATUS
VUsbVhci_PDO_QueryBusInformation(
	IN PPDO_DEVICE_DATA PdoData,
	IN PIRP             Irp
	)
/*
    The PnP Manager uses this IRP to request the type and
    instance number of a device's parent bus. Bus drivers
    should handle this request for their child devices (PDOs).
*/
{
	PPNP_BUS_INFORMATION busInfo;

	PAGED_CODE();

	UNREFERENCED_PARAMETER(PdoData);

	VUSBVHCI_KDPRINT("in VUsbVhci_PDO_QueryBusInformation\n");

	ASSERT(PdoData);
	ASSERT(Irp);

	busInfo = ExAllocatePoolWithTag(PagedPool, sizeof(PNP_BUS_INFORMATION), VUSBVHCI_NOT_OWNER_POOL_TAG);
	if(!busInfo)
	{
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	busInfo->BusTypeGuid = GUID_DEVCLASS_USB;

	// Some buses have a specific INTERFACE_TYPE value,
	// such as PCMCIABus, PCIBus, or PNPISABus.
	// For other buses, especially newer buses like USB, the bus
	// driver sets this member to PNPBus.
	busInfo->LegacyBusType = PNPBus;

	// TODO: bus number of parent (= our FDO)?
	busInfo->BusNumber = 0;

	Irp->IoStatus.Information = (ULONG_PTR)busInfo;

	return STATUS_SUCCESS;
}

NTSTATUS
VUsbVhci_PDO_QueryInterface(
	IN PPDO_DEVICE_DATA PdoData,
	IN PIRP             Irp
	)
{
#ifndef TARGETING_Win2K
	PIO_STACK_LOCATION          irpStack;
	LPGUID                      interfaceType;
	PUSB_BUS_INTERFACE_HUB_V5   ifcHub;
	PUSB_BUS_INTERFACE_USBDI_V2 ifcDev;
	USHORT                      size, ver;
	PFDO_DEVICE_DATA            fdoData = FDO_FROM_PDO(PdoData);
#endif

	PAGED_CODE();

	VUSBVHCI_KDPRINT("in VUsbVhci_PDO_QueryInterface\n");

	if(PdoData->DevicePnPState & Deleted)
	{
		NTSTATUS status;
		Irp->IoStatus.Status = status = STATUS_NO_SUCH_DEVICE;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		return status;
	}

#ifndef TARGETING_Win2K
	irpStack = IoGetCurrentIrpStackLocation(Irp);
	interfaceType = (LPGUID)irpStack->Parameters.QueryInterface.InterfaceType;
	if(IsEqualGUID(interfaceType, &USB_BUS_INTERFACE_HUB_GUID))
	{
		size = irpStack->Parameters.QueryInterface.Size;
		ver = irpStack->Parameters.QueryInterface.Version;
		if((size < sizeof(USB_BUS_INTERFACE_HUB_V0)) ||
		   (ver != USB_BUSIF_HUB_VERSION_0 &&
		    ver != USB_BUSIF_HUB_VERSION_1 &&
		    ver != USB_BUSIF_HUB_VERSION_2 &&
		    ver != USB_BUSIF_HUB_VERSION_3 &&
		    ver != USB_BUSIF_HUB_VERSION_4 &&
		    ver != USB_BUSIF_HUB_VERSION_5) ||
		   (size < sizeof(USB_BUS_INTERFACE_HUB_V1) &&
		    ver == USB_BUSIF_HUB_VERSION_1) ||
		   (size < sizeof(USB_BUS_INTERFACE_HUB_V2) &&
		    ver == USB_BUSIF_HUB_VERSION_2) ||
		   (size < sizeof(USB_BUS_INTERFACE_HUB_V3) &&
		    ver == USB_BUSIF_HUB_VERSION_3) ||
		   (size < sizeof(USB_BUS_INTERFACE_HUB_V4) &&
		    ver == USB_BUSIF_HUB_VERSION_4) ||
		   (size < sizeof(USB_BUS_INTERFACE_HUB_V5) &&
		    ver == USB_BUSIF_HUB_VERSION_5))
		{
			return STATUS_INVALID_PARAMETER;
		}

		ifcHub = (PUSB_BUS_INTERFACE_HUB_V5)irpStack->Parameters.QueryInterface.Interface;
		ifcHub->Size = size;
		ifcHub->Version = ver;
		ifcHub->BusContext = VUsbVhci_GetRootHubContext(fdoData);
		ifcHub->InterfaceReference   = VUsbVhci_InterfaceReference;
		ifcHub->InterfaceDereference = VUsbVhci_InterfaceDereference;

		if(ver == USB_BUSIF_HUB_VERSION_0)
		{
			goto IfcHubDone;
		}

		// V1:
		ifcHub->CreateUsbDevice        = VUsbVhci_BUSIF_CreateUsbDevice;
		ifcHub->InitializeUsbDevice    = VUsbVhci_BUSIF_InitializeUsbDevice;
		ifcHub->GetUsbDescriptors      = VUsbVhci_BUSIF_GetUsbDescriptors;
		ifcHub->RemoveUsbDevice        = VUsbVhci_BUSIF_RemoveUsbDevice;
		ifcHub->RestoreUsbDevice       = VUsbVhci_BUSIF_RestoreUsbDevice;
		ifcHub->GetPortHackFlags       = VUsbVhci_BUSIF_GetPortHackFlags;
		ifcHub->QueryDeviceInformation = VUsbVhci_BUSIF_QueryDeviceInformation;

		if(ver == USB_BUSIF_HUB_VERSION_1)
		{
			goto IfcHubDone;
		}

		// V2:
		ifcHub->GetControllerInformation   = VUsbVhci_BUSIF_GetControllerInformation;
		ifcHub->ControllerSelectiveSuspend = VUsbVhci_BUSIF_ControllerSelectiveSuspend;
		ifcHub->GetExtendedHubInformation  = VUsbVhci_BUSIF_GetExtendedHubInformation;
		ifcHub->GetRootHubSymbolicName     = VUsbVhci_BUSIF_GetRootHubSymbolicName;
		ifcHub->GetDeviceBusContext        = VUsbVhci_BUSIF_GetDeviceBusContext;
		ifcHub->Initialize20Hub            = VUsbVhci_BUSIF_Initialize20Hub;

		if(ver == USB_BUSIF_HUB_VERSION_2)
		{
			goto IfcHubDone;
		}

		// V3:
		ifcHub->RootHubInitNotification = VUsbVhci_BUSIF_RootHubInitNotification;

		if(ver == USB_BUSIF_HUB_VERSION_3)
		{
			goto IfcHubDone;
		}

		// V4:
		ifcHub->FlushTransfers = VUsbVhci_BUSIF_FlushTransfers;

		if(ver == USB_BUSIF_HUB_VERSION_4)
		{
			goto IfcHubDone;
		}

		// V5:
		ifcHub->SetDeviceHandleData = VUsbVhci_BUSIF_SetDeviceHandleData;

IfcHubDone:
		// Must take a reference before returning
		ifcHub->InterfaceReference(ifcHub->BusContext);
		return STATUS_SUCCESS;
	}
	else if(IsEqualGUID(interfaceType, &USB_BUS_INTERFACE_USBDI_GUID))
	{
		size = irpStack->Parameters.QueryInterface.Size;
		ver = irpStack->Parameters.QueryInterface.Version;
		if((size < sizeof(USB_BUS_INTERFACE_USBDI_V0)) ||
		   (ver != USB_BUSIF_USBDI_VERSION_0 &&
		    ver != USB_BUSIF_USBDI_VERSION_1 &&
		    ver != USB_BUSIF_USBDI_VERSION_2) ||
		   (size < sizeof(USB_BUS_INTERFACE_USBDI_V1) &&
		    ver == USB_BUSIF_USBDI_VERSION_1) ||
		   (size < sizeof(USB_BUS_INTERFACE_USBDI_V2) &&
		    ver == USB_BUSIF_USBDI_VERSION_2))
		{
			return STATUS_INVALID_PARAMETER;
		}

		ifcDev = (PUSB_BUS_INTERFACE_USBDI_V2)irpStack->Parameters.QueryInterface.Interface;
		ifcDev->Size = size;
		ifcDev->Version = ver;
		ifcDev->BusContext = VUsbVhci_GetRootHubContext(fdoData);
		ifcDev->InterfaceReference   = VUsbVhci_InterfaceReference;
		ifcDev->InterfaceDereference = VUsbVhci_InterfaceDereference;

		// V0:
		ifcDev->GetUSBDIVersion     = VUsbVhci_BUSIF_GetUSBDIVersion;
		ifcDev->QueryBusTime        = VUsbVhci_BUSIF_QueryBusTime;
		ifcDev->SubmitIsoOutUrb     = VUsbVhci_BUSIF_SubmitIsoOutUrb;
		ifcDev->QueryBusInformation = VUsbVhci_BUSIF_QueryBusInformation;

		if(ver == USB_BUSIF_USBDI_VERSION_0)
		{
			goto IfcDevDone;
		}

		// V1:
		ifcDev->IsDeviceHighSpeed = VUsbVhci_BUSIF_IsDeviceHighSpeed;

		if(ver == USB_BUSIF_USBDI_VERSION_1)
		{
			goto IfcDevDone;
		}

		// V2:
		ifcDev->EnumLogEntry = VUsbVhci_BUSIF_EnumLogEntry;

IfcDevDone:
		// Must take a reference before returning
		ifcDev->InterfaceReference(ifcDev->BusContext);
		return STATUS_SUCCESS;
	}
#endif // !win2k

	// Interface type not supported
	return Irp->IoStatus.Status;
}

NTSTATUS
VUsbVhci_GetDeviceCapabilities(
	IN PDEVICE_OBJECT       DeviceObject,
	IN PDEVICE_CAPABILITIES DeviceCapabilities
	)
/*
    This routine sends the get capabilities irp to the given stack
*/
{
	IO_STATUS_BLOCK    ioStatus;
	KEVENT             pnpEvent;
	NTSTATUS           status;
	PDEVICE_OBJECT     targetObject;
	PIO_STACK_LOCATION irpStack;
	PIRP               pnpIrp;

	PAGED_CODE();

	VUSBVHCI_KDPRINT("in VUsbVhci_GetDeviceCapabilities\n");

	// Initialize the capabilities that we will send down
	RtlZeroMemory(DeviceCapabilities, sizeof(DEVICE_CAPABILITIES));
	DeviceCapabilities->Size = sizeof(DEVICE_CAPABILITIES);
	DeviceCapabilities->Version = 1;
	DeviceCapabilities->Address = MAXULONG;
	DeviceCapabilities->UINumber = MAXULONG;

	// Initialize the event
	KeInitializeEvent(&pnpEvent, NotificationEvent, FALSE);

	targetObject = IoGetAttachedDeviceReference(DeviceObject);

	// Build an Irp
	pnpIrp = IoBuildSynchronousFsdRequest(
		IRP_MJ_PNP,
		targetObject,
		NULL,
		0,
		NULL,
		&pnpEvent,
		&ioStatus);
	if(!pnpIrp)
	{
		status = STATUS_INSUFFICIENT_RESOURCES;
		goto GetDeviceCapabilitiesExit;
	}

	// Pnp Irps all begin life as STATUS_NOT_SUPPORTED;
	pnpIrp->IoStatus.Status = STATUS_NOT_SUPPORTED;

	// Get the top of stack
	irpStack = IoGetNextIrpStackLocation(pnpIrp);

	// Set the top of stack
	RtlZeroMemory(irpStack, sizeof(IO_STACK_LOCATION));
	irpStack->MajorFunction = IRP_MJ_PNP;
	irpStack->MinorFunction = IRP_MN_QUERY_CAPABILITIES;
	irpStack->Parameters.DeviceCapabilities.Capabilities = DeviceCapabilities;

	// Call the driver
	status = IoCallDriver(targetObject, pnpIrp);
	if(status == STATUS_PENDING)
	{
		// Block until the irp comes back.
		// Important thing to note here is when you allocate
		// the memory for an event in the stack you must do a
		// KernelMode wait instead of UserMode to prevent
		// the stack from getting paged out.
		KeWaitForSingleObject(
			&pnpEvent,
			Executive,
			KernelMode,
			FALSE,
			NULL);
		status = ioStatus.Status;
	}

GetDeviceCapabilitiesExit:
	// Done with reference
	ObDereferenceObject(targetObject);

	return status;
}
