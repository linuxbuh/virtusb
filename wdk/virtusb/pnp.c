#include "virtusb.h"
#include "trace.h"
#include "pnp.tmh"
#include "pnp.h"
#include "busif.h"
#include "user_io.h"
#include "proc_urb.h"

NTSTATUS
VirtUsb_FDO_PnP(
	_In_ PFDO_DEVICE_DATA FdoData,
	_In_ PIRP             Irp
	);

NTSTATUS
VirtUsb_PDO_PnP(
	_In_ PPDO_DEVICE_DATA PdoData,
	_In_ PIRP             Irp
	);

NTSTATUS
VirtUsb_PDO_QueryDeviceCaps(
	_In_ PPDO_DEVICE_DATA PdoData,
	_In_ PIRP             Irp
	);

NTSTATUS
VirtUsb_PDO_QueryDeviceId(
	_In_ PPDO_DEVICE_DATA PdoData,
	_In_ PIRP             Irp
	);

NTSTATUS
VirtUsb_PDO_QueryDeviceText(
	_In_ PPDO_DEVICE_DATA PdoData,
	_In_ PIRP             Irp
	);

NTSTATUS
VirtUsb_PDO_QueryResources(
	_In_ PPDO_DEVICE_DATA PdoData,
	_In_ PIRP             Irp
	);

NTSTATUS
VirtUsb_PDO_QueryResourceRequirements(
	_In_ PPDO_DEVICE_DATA PdoData,
	_In_ PIRP             Irp
	);

NTSTATUS
VirtUsb_PDO_QueryDeviceRelations(
	_In_ PPDO_DEVICE_DATA PdoData,
	_In_ PIRP             Irp
	);

NTSTATUS
VirtUsb_PDO_QueryBusInformation(
	_In_ PPDO_DEVICE_DATA PdoData,
	_In_ PIRP             Irp
	);

NTSTATUS
VirtUsb_PDO_QueryInterface(
	_In_ PPDO_DEVICE_DATA PdoData,
	_In_ PIRP             Irp
	);

NTSTATUS
VirtUsb_GetDeviceCapabilities(
	_In_ PDEVICE_OBJECT       DeviceObject,
	_In_ PDEVICE_CAPABILITIES DeviceCapabilities
	);

PCHAR
PnPMinorFunctionString(
	_In_ UCHAR MinorFunction
	);

PCHAR
DbgDeviceRelationString(
	_In_ DEVICE_RELATION_TYPE Type
	);

PCHAR
DbgDeviceIDString(
	_In_ BUS_QUERY_ID_TYPE Type
	);

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, VirtUsb_DispatchPnP)
#pragma alloc_text(PAGE, VirtUsb_FDO_PnP)
#pragma alloc_text(PAGE, VirtUsb_PDO_PnP)
#pragma alloc_text(PAGE, VirtUsb_PDO_QueryDeviceCaps)
#pragma alloc_text(PAGE, VirtUsb_PDO_QueryDeviceId)
#pragma alloc_text(PAGE, VirtUsb_PDO_QueryDeviceText)
#pragma alloc_text(PAGE, VirtUsb_PDO_QueryResources)
#pragma alloc_text(PAGE, VirtUsb_PDO_QueryResourceRequirements)
#pragma alloc_text(PAGE, VirtUsb_PDO_QueryDeviceRelations)
#pragma alloc_text(PAGE, VirtUsb_PDO_QueryBusInformation)
#pragma alloc_text(PAGE, VirtUsb_PDO_QueryInterface)
#pragma alloc_text(PAGE, VirtUsb_GetDeviceCapabilities)
#endif // ALLOC_PRAGMA

NTSTATUS
VirtUsb_DispatchPnP(
	_In_ PDEVICE_OBJECT DeviceObject,
	_In_ PIRP           Irp
	)
/*
    Handles PnP Irps sent to both FDO and child PDOs.
*/
{
	PIO_STACK_LOCATION  irpStack;
	NTSTATUS            status;
	PCOMMON_DEVICE_DATA commonData;

	PAGED_CODE();

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_PNP, ">%!FUNC!");

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
		TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_PNP, "FDO %s IRP: 0x%p",
		            PnPMinorFunctionString(irpStack->MinorFunction),
		            Irp);

		// Request is for the bus FDO
		status = VirtUsb_FDO_PnP((PFDO_DEVICE_DATA)commonData, Irp);
	}
	else
	{
		TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_PNP, "PDO %s IRP: 0x%p",
		            PnPMinorFunctionString(irpStack->MinorFunction),
		            Irp);

		// Request is for the child PDO.
		status = VirtUsb_PDO_PnP((PPDO_DEVICE_DATA)commonData, Irp);
	}

	return status;
}

PCHAR
PnPMinorFunctionString(
	_In_ UCHAR MinorFunction
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
	_In_ DEVICE_RELATION_TYPE Type
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
	_In_ BUS_QUERY_ID_TYPE Type
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

NTSTATUS
VirtUsb_FDO_PnP(
	_In_ PFDO_DEVICE_DATA FdoData,
	_In_ PIRP             Irp
	)
/*
    Handle requests from the Plug & Play system for the BUS itself
*/
{
	NTSTATUS           status;
	ULONG              length, prevcount, numPdosPresent;
	PLIST_ENTRY        entry, listHead, nextEntry;
	PDEVICE_OBJECT     fdo = FdoData->Self;
	PPDO_DEVICE_DATA   pdoData;
	PFILE_CONTEXT      file;
	PDEVICE_RELATIONS  relations, oldRelations;
	PIO_STACK_LOCATION irpStack = IoGetCurrentIrpStackLocation(Irp);

	PAGED_CODE();

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_PNP, ">%!FUNC!");

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
		status = VirtUsb_SendIrpSynchronously(FdoData->NextLowerDriver, Irp);
		if(NT_SUCCESS(status))
		{
			status = VirtUsb_StartFdo(FdoData);
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
		ExAcquireFastMutex(&FdoData->Mutex);
		if(IsListEmpty(&FdoData->ListOfFiles))
		{
			ExReleaseFastMutex(&FdoData->Mutex);
			SET_NEW_PNP_STATE(FdoData, StopPending);
			Irp->IoStatus.Status = STATUS_SUCCESS;
		}
		else
		{
			ExReleaseFastMutex(&FdoData->Mutex);
			Irp->IoStatus.Status = status = STATUS_UNSUCCESSFUL;
			IoCompleteRequest(Irp, IO_NO_INCREMENT);
			IoReleaseRemoveLock(&FdoData->RemoveLock, Irp);
			return status;
		}
		break;

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
		ExAcquireFastMutex(&FdoData->Mutex);
		if(IsListEmpty(&FdoData->ListOfFiles))
		{
			ExReleaseFastMutex(&FdoData->Mutex);
			SET_NEW_PNP_STATE(FdoData, RemovePending);
			Irp->IoStatus.Status = STATUS_SUCCESS;
		}
		else
		{
			ExReleaseFastMutex(&FdoData->Mutex);
			Irp->IoStatus.Status = status = STATUS_UNSUCCESSFUL;
			IoCompleteRequest(Irp, IO_NO_INCREMENT);
			IoReleaseRemoveLock(&FdoData->RemoveLock, Irp);
			return status;
		}
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

		ExAcquireFastMutex(&FdoData->Mutex);

		// fail outstanding IO
		listHead = &FdoData->ListOfFiles;
		for(entry = listHead->Flink, nextEntry = entry->Flink;
		    entry != listHead;
		    entry = nextEntry, nextEntry = entry->Flink)
		{
			file = CONTAINING_RECORD(entry, FILE_CONTEXT, Link);
			VirtUsb_FailFileIO(file, STATUS_NO_SUCH_DEVICE);
		}

		VirtUsb_FdoInterfaceDown(FdoData);

		listHead = &FdoData->ListOfPdos;
		for(entry = listHead->Flink, nextEntry = entry->Flink;
		    entry != listHead;
		    entry = nextEntry, nextEntry = entry->Flink)
		{
			RemoveEntryList(entry);
			InitializeListHead(entry);
			pdoData = CONTAINING_RECORD(entry, PDO_DEVICE_DATA, Link);
			pdoData->ReportedMissing = TRUE;
		}
		ASSERT(IsListEmpty(&FdoData->ListOfPdos));

		ExReleaseFastMutex(&FdoData->Mutex);

		Irp->IoStatus.Status = STATUS_SUCCESS; // We must not fail the IRP.
		break;

	case IRP_MN_REMOVE_DEVICE:
		// The Plug & Play system has dictated the removal of this device.
		// We have no choice but to detach and delete the device object.

		ExAcquireFastMutex(&FdoData->Mutex);

		// Typically the system removes all the children before
		// removing the parent FDO. If for any reason child Pdos are
		// still present we will destroy them explicitly, with one exception --
		// we will not delete the PDOs that are in SurpriseRemovePending state.
		listHead = &FdoData->ListOfPdos;
		for(entry = listHead->Flink, nextEntry = entry->Flink;
		    entry != listHead;
		    entry = nextEntry, nextEntry = entry->Flink)
		{
			RemoveEntryList(entry);
			pdoData = CONTAINING_RECORD(entry, PDO_DEVICE_DATA, Link);
			if(SurpriseRemovePending == pdoData->DevicePnPState)
			{
				TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_PNP,
				            "Found a surprise removed pdo: 0x%p", pdoData->Self);
				InitializeListHead(entry);
				// We set ReportedMissing to TRUE, because this will trigger
				// the destruction of the PDO if the PnP manager removes it.
				pdoData->ReportedMissing = TRUE;
				continue;
			}
			VirtUsb_DestroyPdo(pdoData);
			TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_PNP,
			            "Deleting PDO: 0x%p  ID: %lu", pdoData->Self, pdoData->Id);
			IoDeleteDevice(pdoData->Self);
		}
		ASSERT(IsListEmpty(&FdoData->ListOfPdos));

		if(FdoData->DevicePnPState != SurpriseRemovePending)
		{
			// fail outstanding IO
			listHead = &FdoData->ListOfFiles;
			for(entry = listHead->Flink, nextEntry = entry->Flink;
			    entry != listHead;
			    entry = nextEntry, nextEntry = entry->Flink)
			{
				file = CONTAINING_RECORD(entry, FILE_CONTEXT, Link);
				VirtUsb_FailFileIO(file, STATUS_NO_SUCH_DEVICE);
			}

			VirtUsb_FdoInterfaceDown(FdoData);
		}

		ExReleaseFastMutex(&FdoData->Mutex);

		SET_NEW_PNP_STATE(FdoData, Deleted);

		IoReleaseRemoveLockAndWait(&FdoData->RemoveLock, Irp);
		ASSERT(IsListEmpty(&FdoData->ListOfFiles));

		// We need to send the remove down the stack before we detach,
		// but we don't need to wait for the completion of this operation
		// (and to register a completion routine).
		Irp->IoStatus.Status = STATUS_SUCCESS;
		IoSkipCurrentIrpStackLocation(Irp);
		status = IoCallDriver(FdoData->NextLowerDriver, Irp);

		// Detach from the underlying devices.
		IoDetachDevice(FdoData->NextLowerDriver);

		TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_PNP, "Deleting FDO: 0x%p", fdo);
		IoDeleteDevice(fdo);

		return status;

	case IRP_MN_QUERY_DEVICE_RELATIONS:
		TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_PNP, "QueryDeviceRelation Type: %s",
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

		ExAcquireFastMutex(&FdoData->Mutex);

		// Calculate the number of PDOs actually present on the bus
		listHead = &FdoData->ListOfPdos;
		numPdosPresent = 0;
		for(entry = listHead->Flink;
		    entry != listHead;
		    entry = entry->Flink)
		{
			pdoData = CONTAINING_RECORD(entry, PDO_DEVICE_DATA, Link);
			if(pdoData->Present)
			{
				numPdosPresent++;
			}
		}

		oldRelations = (PDEVICE_RELATIONS)Irp->IoStatus.Information;
		if(oldRelations)
		{
			prevcount = oldRelations->Count;
			if(!numPdosPresent)
			{
				// There is a device relations struct already present and we have
				// nothing to add to it, so just call IoSkip and IoCall
				ExReleaseFastMutex(&FdoData->Mutex);
				break;
			}
		}
		else
		{
			prevcount = 0;
		}

		// Need to allocate a new relations structure and add our
		// PDOs to it.
		length = FIELD_OFFSET(DEVICE_RELATIONS, Objects[numPdosPresent + prevcount]);
		relations = (PDEVICE_RELATIONS)ExAllocatePoolWithTag(PagedPool,
		                                                     length,
		                                                     VIRTUSB_NOT_OWNER_POOL_TAG);

		if(!relations)
		{
			// Fail the IRP
			ExReleaseFastMutex(&FdoData->Mutex);
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

		relations->Count = prevcount + numPdosPresent;

		// For each PDO present on this bus add a pointer to the device relations
		// buffer, being sure to take out a reference to that object.
		// The Plug & Play system will dereference the object when it is done
		// with it and free the device relations buffer.
		listHead = &FdoData->ListOfPdos;
		for(entry = listHead->Flink;
		    entry != listHead;
		    entry = entry->Flink)
		{
			pdoData = CONTAINING_RECORD(entry, PDO_DEVICE_DATA, Link);
			if(pdoData->Present)
			{
				relations->Objects[prevcount++] = pdoData->Self;
				ObReferenceObject(pdoData->Self);
			}
			else
			{
				pdoData->ReportedMissing = TRUE;
			}
		}

		ExReleaseFastMutex(&FdoData->Mutex);

		TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_PNP, "#PDOS present = %lu", numPdosPresent);
		TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_PNP, "#PDOs reported = %lu", relations->Count);

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
VirtUsb_PDO_PnP(
	_In_ PPDO_DEVICE_DATA PdoData,
	_In_ PIRP             Irp
	)
/*
    Handle requests from the Plug & Play system for the devices on the BUS
*/
{
	NTSTATUS           status;
	PIO_STACK_LOCATION irpStack = IoGetCurrentIrpStackLocation(Irp);

	PAGED_CODE();

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_PNP, ">%!FUNC!");

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
		// complete all irps in work units and free the work units
		VirtUsb_FailUrbIO(PdoData, STATUS_NO_SUCH_DEVICE, TRUE);
		status = STATUS_SUCCESS; // We must not fail this IRP.
		break;

	case IRP_MN_REMOVE_DEVICE:
		// We will delete the PDO only after we have reported to the
		// Plug and Play manager that it's missing.
		if(PdoData->ReportedMissing)
		{
			PFDO_DEVICE_DATA fdoData;

			// complete all irps in work units and free the work units
			VirtUsb_FailUrbIO(PdoData, STATUS_NO_SUCH_DEVICE, TRUE);

			SET_NEW_PNP_STATE(PdoData, Deleted);
			IoReleaseRemoveLockAndWait(&PdoData->RemoveLock, Irp);

			// Remove the PDO from the list.
			fdoData = FDO_FROM_PDO(PdoData);
			ExAcquireFastMutex(&fdoData->Mutex);
			RemoveEntryList(&PdoData->Link);
			ExReleaseFastMutex(&fdoData->Mutex);

			// Free up resources associated with PDO and delete it.
			VirtUsb_DestroyPdo(PdoData);
			TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_PNP,
			            "Deleting PDO: 0x%p  ID: %lu", PdoData->Self, PdoData->Id);
			IoDeleteDevice(PdoData->Self);

			Irp->IoStatus.Status = status = STATUS_SUCCESS; // We must not fail this IRP.
			IoCompleteRequest(Irp, IO_NO_INCREMENT);
			return status;
		}
		if(PdoData->Present)
		{
			// complete all irps in work units but keep the canceled work units (with wu->Irp==NULL)
			VirtUsb_FailUrbIO(PdoData, STATUS_NO_SUCH_DEVICE, FALSE);

			// When the device is disabled, the PDO transitions from
			// RemovePending to NotStarted. We shouldn't delete
			// the PDO because a) the device is still present on the bus,
			// b) we haven't reported missing to the PnP manager.
			SET_NEW_PNP_STATE(PdoData, NotStarted);
			status = STATUS_SUCCESS; // We must not fail this IRP.
		}
		else
		{
			// We should not get here, because our device never comes back after we've
			// reported it missing. (We report it missing, if user-mode closes the file;
			// and we report it present, if user-mode opens the file and calls IOCREGISTER,
			// but then we create a completely new pdo -- we never re-use one, after file
			// gets closed.)
			ASSERT(PdoData->Present);
			status = STATUS_SUCCESS; // We must not fail this IRP.
		}
		break;

	case IRP_MN_QUERY_CAPABILITIES:
		// Return the capabilities of a device, such as whether the device
		// can be locked or ejected..etc
		status = VirtUsb_PDO_QueryDeviceCaps(PdoData, Irp);
		break;

	case IRP_MN_QUERY_ID:
		// Query the IDs of the device
		TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_PNP, "QueryId Type: %s",
			DbgDeviceIDString(irpStack->Parameters.QueryId.IdType));
		status = VirtUsb_PDO_QueryDeviceId(PdoData, Irp);
		break;

	case IRP_MN_QUERY_DEVICE_RELATIONS:
		TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_PNP, "QueryDeviceRelation Type: %s",
			DbgDeviceRelationString(irpStack->Parameters.QueryDeviceRelations.Type));
		status = VirtUsb_PDO_QueryDeviceRelations(PdoData, Irp);
		break;

	case IRP_MN_QUERY_DEVICE_TEXT:
		status = VirtUsb_PDO_QueryDeviceText(PdoData, Irp);
		break;

	case IRP_MN_QUERY_RESOURCES:
		status = VirtUsb_PDO_QueryResources(PdoData, Irp);
		break;

	case IRP_MN_QUERY_RESOURCE_REQUIREMENTS:
		status = VirtUsb_PDO_QueryResourceRequirements(PdoData, Irp);
		break;

	case IRP_MN_QUERY_BUS_INFORMATION:
		status = VirtUsb_PDO_QueryBusInformation(PdoData, Irp);
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
		if(PdoData->ParentFdo)
		{
			PFDO_DEVICE_DATA fdoData;
			fdoData = FDO_FROM_PDO(PdoData);
			ExAcquireFastMutex(&fdoData->Mutex);
			PdoData->Present = FALSE;
			ExReleaseFastMutex(&fdoData->Mutex);
		}
		status = STATUS_SUCCESS;
		break;

	case IRP_MN_QUERY_INTERFACE:
		// This request enables a driver to export a direct-call
		// interface to other drivers. A bus driver that exports
		// an interface must handle this request for its child
		// devices (child PDOs).
		status = VirtUsb_PDO_QueryInterface(PdoData, Irp);
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
		//VIRTUSB_KDPRINT("Not handled\n");
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
VirtUsb_PDO_QueryDeviceCaps(
	_In_ PPDO_DEVICE_DATA PdoData,
	_In_ PIRP             Irp
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

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_PNP, ">%!FUNC!");

	ASSERT(PdoData);
	ASSERT(PdoData->ParentFile);

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
	status = VirtUsb_GetDeviceCapabilities(
		FDO_FROM_PDO(PdoData)->NextLowerDriver, &parentCapabilities);
	if(!NT_SUCCESS(status))
	{
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_PNP, "%!FUNC! failed");
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

	// Ejection not supported
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
	deviceCapabilities->SilentInstall = TRUE;

	// Specifies an address indicating where the device is located
	// on its underlying bus. The interpretation of this number is
	// bus-specific. If the address is unknown or the bus driver
	// does not support an address, the bus driver leaves this
	// member at its default value of 0xFFFFFFFF. In this example
	// the location address is same as instance id.
	deviceCapabilities->Address = PdoData->Id;

	// UINumber specifies a number associated with the device that can
	// be displayed in the user interface.
	deviceCapabilities->UINumber = PdoData->Id;

	return STATUS_SUCCESS;
}

NTSTATUS
VirtUsb_PDO_QueryDeviceId(
	_In_ PPDO_DEVICE_DATA PdoData,
	_In_ PIRP             Irp
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

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_PNP, ">%!FUNC!");

	stack = IoGetCurrentIrpStackLocation(Irp);

	switch(stack->Parameters.QueryId.IdType)
	{
	case BusQueryDeviceID:
		// The DeviceID is an unique string to identify identical devices.

		buffer = (PWCHAR)HcdDeviceID;
		while(*(buffer++));
		length = (ULONG)(buffer - HcdDeviceID) * sizeof(WCHAR);
		ASSERT(length >= sizeof(WCHAR));
		ASSERT(length ^ 1);

		buffer = ExAllocatePoolWithTag(PagedPool, length, VIRTUSB_NOT_OWNER_POOL_TAG);
		if(!buffer)
		{
			status = STATUS_INSUFFICIENT_RESOURCES;
			break;
		}

		RtlCopyMemory(buffer, HcdDeviceID, length);
		ASSERT(!buffer[length / sizeof(WCHAR) - 1]);
		Irp->IoStatus.Information = (ULONG_PTR)buffer;
		TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_PNP,
		            "DeviceId: (%lu)\"%ws\"", length / sizeof(WCHAR) - 1, buffer);
		break;

	case BusQueryInstanceID:
		buffer = PdoData->InstanceID;
		while(*(buffer++));
		length = (ULONG)(buffer - PdoData->InstanceID) * sizeof(WCHAR);
		ASSERT(length >= sizeof(WCHAR));
		ASSERT(length ^ 1);

		buffer = ExAllocatePoolWithTag(PagedPool, length, VIRTUSB_NOT_OWNER_POOL_TAG);
		if(!buffer)
		{
			status = STATUS_INSUFFICIENT_RESOURCES;
			break;
		}

		RtlCopyMemory(buffer, PdoData->InstanceID, length);
		ASSERT(!buffer[length / sizeof(WCHAR) - 1]);
		Irp->IoStatus.Information = (ULONG_PTR)buffer;
		TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_PNP,
		            "InstanceId: (%lu)\"%ws\"", length / sizeof(WCHAR) - 1, buffer);
		break;

	case BusQueryHardwareIDs:
		// A device has at least one hardware id.
		// In a list of hardware IDs (multi_sz string) for a device,
		// DeviceId is the most specific and should be first in the list.

		buffer = (PWCHAR)HcdHardwareIDs;
		do
		{
			while(*(buffer++));
		} while(*(buffer++));
		length = (ULONG)(buffer - HcdHardwareIDs) * sizeof(WCHAR);
		ASSERT(length >= 2 * sizeof(WCHAR));
		ASSERT(length ^ 1);

		buffer = ExAllocatePoolWithTag(PagedPool, length, VIRTUSB_NOT_OWNER_POOL_TAG);
		if(!buffer)
		{
			status = STATUS_INSUFFICIENT_RESOURCES;
			break;
		}

		RtlCopyMemory(buffer, HcdHardwareIDs, length);
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
					TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_PNP,
					            "HardwareId[%lu]: (%lu)\"%ws\"", cnt, (ULONG)(ULONG_PTR)(buffer - start) - 1, start);
					cnt++;
				}
			} while(*buffer);
			TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_PNP, "#HardwareIds: %lu", cnt);
		}
#endif
		break;

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
VirtUsb_PDO_QueryDeviceText(
	_In_ PPDO_DEVICE_DATA PdoData,
	_In_ PIRP             Irp
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

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_PNP, ">%!FUNC!");

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
				buffer = (PWCHAR)HcdText;
				while(*(buffer++));
				length = (ULONG)(buffer - HcdText) * sizeof(WCHAR);
				ASSERT(length >= sizeof(WCHAR));
				ASSERT(length ^ 1);

				buffer = ExAllocatePoolWithTag(PagedPool, length, VIRTUSB_NOT_OWNER_POOL_TAG);
				if(!buffer)
				{
					status = STATUS_INSUFFICIENT_RESOURCES;
					break;
				}

				RtlCopyMemory(buffer, HcdText, length);
				ASSERT(!buffer[length / sizeof(WCHAR) - 1]);
				Irp->IoStatus.Information = (ULONG_PTR)buffer;
				TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_PNP,
				            "Text: (%lu)\"%ws\"", length / sizeof(WCHAR) - 1, buffer);
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
VirtUsb_PDO_QueryResources(
	_In_ PPDO_DEVICE_DATA PdoData,
	_In_ PIRP             Irp
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

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_PNP, ">%!FUNC!");

	// we don't have any boot configuration resources
	return Irp->IoStatus.Status;
}

NTSTATUS
VirtUsb_PDO_QueryResourceRequirements(
	_In_ PPDO_DEVICE_DATA PdoData,
	_In_ PIRP             Irp
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

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_PNP, ">%!FUNC!");

	// we don't need any resources
	return Irp->IoStatus.Status;
}

NTSTATUS
VirtUsb_PDO_QueryDeviceRelations(
	_In_ PPDO_DEVICE_DATA PdoData,
	_In_ PIRP             Irp
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

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_PNP, ">%!FUNC!");

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
			                      VIRTUSB_NOT_OWNER_POOL_TAG);
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
VirtUsb_PDO_QueryBusInformation(
	_In_ PPDO_DEVICE_DATA PdoData,
	_In_ PIRP             Irp
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

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_PNP, ">%!FUNC!");

	ASSERT(PdoData);
	ASSERT(Irp);

	busInfo = ExAllocatePoolWithTag(PagedPool, sizeof(PNP_BUS_INFORMATION), VIRTUSB_NOT_OWNER_POOL_TAG);
	if(!busInfo)
	{
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	busInfo->BusTypeGuid = GUID_DEVCLASS_VIRTUSB_BUS;

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
VirtUsb_PDO_QueryInterface(
	_In_ PPDO_DEVICE_DATA PdoData,
	_In_ PIRP             Irp
	)
{
	PIO_STACK_LOCATION             irpStack;
	LPGUID                         interfaceType;
	PVIRTUSB_BUS_INTERFACE_VHCI_V1 ifcVhci;
	USHORT                         size, ver;

	PAGED_CODE();

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_PNP, ">%!FUNC!");

	if(PdoData->DevicePnPState & Deleted)
	{
		NTSTATUS status;
		Irp->IoStatus.Status = status = STATUS_NO_SUCH_DEVICE;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		return status;
	}

	irpStack = IoGetCurrentIrpStackLocation(Irp);
	interfaceType = (LPGUID)irpStack->Parameters.QueryInterface.InterfaceType;
	if(IsEqualGUID(interfaceType, &VIRTUSB_BUS_INTERFACE_VHCI_GUID))
	{
		size = irpStack->Parameters.QueryInterface.Size;
		ver = irpStack->Parameters.QueryInterface.Version;
		if((size < sizeof(VIRTUSB_BUS_INTERFACE_VHCI_V0)) ||
		   (ver != VIRTUSB_BUSIF_VHCI_VERSION_0 &&
		    ver != VIRTUSB_BUSIF_VHCI_VERSION_1) ||
		   (size < sizeof(VIRTUSB_BUS_INTERFACE_VHCI_V1) &&
		    ver == VIRTUSB_BUSIF_VHCI_VERSION_1))
		{
			return STATUS_INVALID_PARAMETER;
		}

		ifcVhci = (PVIRTUSB_BUS_INTERFACE_VHCI_V1)irpStack->Parameters.QueryInterface.Interface;
		ifcVhci->Size = size;
		ifcVhci->Version = ver;
		ifcVhci->Context = PdoData;
		ifcVhci->InterfaceReference   = VirtUsb_InterfaceReference;
		ifcVhci->InterfaceDereference = VirtUsb_InterfaceDereference;

		if(ver == VIRTUSB_BUSIF_VHCI_VERSION_0)
		{
			goto IfcVhciDone;
		}

		// V1:
		ifcVhci->GetRootHubContext          = VirtUsb_BUSIF_GetRootHubContext;
		ifcVhci->GetRootHubHubDescriptor    = VirtUsb_BUSIF_GetRootHubHubDescriptor;
		ifcVhci->ReferenceUsbDeviceByHandle = VirtUsb_BUSIF_ReferenceUsbDeviceByHandle;
		ifcVhci->ReferenceUsbDevice         = VirtUsb_BUSIF_ReferenceUsbDevice;
		ifcVhci->DereferenceUsbDevice       = VirtUsb_BUSIF_DereferenceUsbDevice;
		ifcVhci->CreateUsbDevice            = VirtUsb_BUSIF_CreateUsbDevice;
		ifcVhci->InitializeUsbDevice        = VirtUsb_BUSIF_InitializeUsbDevice;
		ifcVhci->RemoveUsbDevice            = VirtUsb_BUSIF_RemoveUsbDevice;

IfcVhciDone:
		// Must take a reference before returning
		ifcVhci->InterfaceReference(ifcVhci->Context);
		return STATUS_SUCCESS;
	}

	// Interface type not supported
	return Irp->IoStatus.Status;
}

NTSTATUS
VirtUsb_GetDeviceCapabilities(
	_In_ PDEVICE_OBJECT       DeviceObject,
	_In_ PDEVICE_CAPABILITIES DeviceCapabilities
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

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_PNP, ">%!FUNC!");

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
