#include "virtusb.h"
#include "trace.h"
#include "wmi.tmh"
#include "wmi.h"

NTSTATUS
VirtUsb_SetWmiDataItem(
	_In_ PDEVICE_OBJECT DeviceObject,
	_In_ PIRP           Irp,
	_In_ ULONG          GuidIndex,
	_In_ ULONG          InstanceIndex,
	_In_ ULONG          DataItemId,
	_In_ ULONG          BufferSize,
	_In_ PUCHAR         Buffer
	);

NTSTATUS
VirtUsb_SetWmiDataBlock(
	_In_ PDEVICE_OBJECT DeviceObject,
	_In_ PIRP           Irp,
	_In_ ULONG          GuidIndex,
	_In_ ULONG          InstanceIndex,
	_In_ ULONG          BufferSize,
	_In_ PUCHAR         Buffer
	);

NTSTATUS
VirtUsb_QueryWmiDataBlock(
	_In_    PDEVICE_OBJECT DeviceObject,
	_In_    PIRP           Irp,
	_In_    ULONG          GuidIndex,
	_In_    ULONG          InstanceIndex,
	_In_    ULONG          InstanceCount,
	_Inout_ PULONG         InstanceLengthArray,
	_In_    ULONG          BufferAvail,
	_Out_   PUCHAR         Buffer
	);

NTSTATUS
VirtUsb_QueryWmiRegInfo(
	_In_  PDEVICE_OBJECT  DeviceObject,
	_Out_ ULONG           *RegFlags,
	_Out_ PUNICODE_STRING InstanceName,
	_Out_ PUNICODE_STRING *RegistryPath,
	_Out_ PUNICODE_STRING MofResourceName,
	_Out_ PDEVICE_OBJECT  *Pdo
	);

PCHAR
WMIMinorFunctionString(
	_In_ UCHAR MinorFunction
	);

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, VirtUsb_DispatchSystemControl)
#pragma alloc_text(PAGE, VirtUsb_WmiRegistration)
#pragma alloc_text(PAGE, VirtUsb_WmiDeRegistration)
#pragma alloc_text(PAGE, VirtUsb_SetWmiDataItem)
#pragma alloc_text(PAGE, VirtUsb_SetWmiDataBlock)
#pragma alloc_text(PAGE, VirtUsb_QueryWmiDataBlock)
#pragma alloc_text(PAGE, VirtUsb_QueryWmiRegInfo)
#endif // ALLOC_PRAGMA

NTSTATUS
VirtUsb_DispatchSystemControl(
	_In_ PDEVICE_OBJECT DeviceObject,
	_In_ PIRP           Irp
	)
/*
    We have just received a System Control IRP.

    Assume that this is a WMI IRP and
    call into the WMI system library and let it handle this IRP for us.
*/
{
	PFDO_DEVICE_DATA       fdoData;
	SYSCTL_IRP_DISPOSITION disposition;
	NTSTATUS               status;
	PIO_STACK_LOCATION     stack;
	PCOMMON_DEVICE_DATA    commonData;
#if DBG
	UNICODE_STRING         ustr;
#endif

	PAGED_CODE();

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_WMI, ">%!FUNC!");

	stack = IoGetCurrentIrpStackLocation(Irp);

	commonData = (PCOMMON_DEVICE_DATA)DeviceObject->DeviceExtension;

	if(!commonData->IsFDO)
	{
		// The PDO, just complete the request with the current status
		TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_WMI, "PDO %s",
		            WMIMinorFunctionString(stack->MinorFunction));
		status = Irp->IoStatus.Status;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		return status;
	}

	fdoData = (PFDO_DEVICE_DATA)DeviceObject->DeviceExtension;

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_WMI, "FDO %s",
	            WMIMinorFunctionString(stack->MinorFunction));

	if(!NT_SUCCESS(IoAcquireRemoveLock(&fdoData->RemoveLock, Irp)))
	{
		Irp->IoStatus.Status = status = STATUS_NO_SUCH_DEVICE;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		return status;
	}

	if(fdoData->DevicePnPState & Deleted)
	{
		Irp->IoStatus.Status = status = STATUS_NO_SUCH_DEVICE;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		IoReleaseRemoveLock(&fdoData->RemoveLock, Irp);
		return status;
	}

#if DBG
	if(stack->Parameters.WMI.DataPath)
	{
		ustr.MaximumLength = 64 * sizeof(UNICODE_NULL);
		ustr.Length = 0;
		ustr.Buffer = ExAllocatePoolWithTag(PagedPool,
		                                    ustr.MaximumLength,
		                                    VIRTUSB_POOL_TAG);
		if(!ustr.Buffer)
			goto skipGuid;
		RtlStringFromGUID((GUID *)stack->Parameters.WMI.DataPath, &ustr);
		TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_WMI, "GUID: %wZ", &ustr);
		ExFreePoolWithTag(ustr.Buffer, VIRTUSB_POOL_TAG);
skipGuid:;
	}
#endif

	status = WmiSystemControl(&fdoData->WmiLibInfo,
	                          DeviceObject,
	                          Irp,
	                          &disposition);
	switch(disposition)
	{
	case IrpProcessed:
		// This irp has been processed and may be completed or pending.
		break;

	case IrpNotCompleted:
		// This irp has not been completed, but has been fully processed.
		// we will complete it now
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		break;

	case IrpNotWmi:
		TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_WMI,
		            "IrpNotWmi -- MinorFunction: %lu", (ULONG)stack->MinorFunction);
	case IrpForward:
		// This irp is either not a WMI irp or is a WMI irp targetted
		// at a device lower in the stack.
		IoSkipCurrentIrpStackLocation(Irp);
		status = IoCallDriver(fdoData->NextLowerDriver, Irp);
		break;

	default:
		// We really should never get here, but if we do just forward...
		TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_WMI,
		            "MinorFunction: %lu", (ULONG)stack->MinorFunction);
		ASSERT(FALSE);
		IoSkipCurrentIrpStackLocation(Irp);
		status = IoCallDriver(fdoData->NextLowerDriver, Irp);
		break;
	}

	IoReleaseRemoveLock(&fdoData->RemoveLock, Irp);

	return status;
}

PCHAR
WMIMinorFunctionString(
	_In_ UCHAR MinorFunction
	)
{
	switch(MinorFunction)
	{
	case IRP_MN_CHANGE_SINGLE_INSTANCE:
		return "IRP_MN_CHANGE_SINGLE_INSTANCE";
	case IRP_MN_CHANGE_SINGLE_ITEM:
		return "IRP_MN_CHANGE_SINGLE_ITEM";
	case IRP_MN_DISABLE_COLLECTION:
		return "IRP_MN_DISABLE_COLLECTION";
	case IRP_MN_DISABLE_EVENTS:
		return "IRP_MN_DISABLE_EVENTS";
	case IRP_MN_ENABLE_COLLECTION:
		return "IRP_MN_ENABLE_COLLECTION";
	case IRP_MN_ENABLE_EVENTS:
		return "IRP_MN_ENABLE_EVENTS";
	case IRP_MN_EXECUTE_METHOD:
		return "IRP_MN_EXECUTE_METHOD";
	case IRP_MN_QUERY_ALL_DATA:
		return "IRP_MN_QUERY_ALL_DATA";
	case IRP_MN_QUERY_SINGLE_INSTANCE:
		return "IRP_MN_QUERY_SINGLE_INSTANCE";
	case IRP_MN_REGINFO:
		return "IRP_MN_REGINFO";
#ifndef TARGETING_Win2K
	case IRP_MN_REGINFO_EX:
		return "IRP_MN_REGINFO_EX";
#endif
	default:
		return "unknown_syscontrol_irp";
	}
}

NTSTATUS
VirtUsb_WmiRegistration(
	_In_ PFDO_DEVICE_DATA FdoData
	)
/*
    Registers with WMI as a data provider for this
    instance of the device
*/
{
	NTSTATUS status;

	PAGED_CODE();

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_WMI, ">%!FUNC!");

	FdoData->WmiLibInfo.GuidCount = 0;
	FdoData->WmiLibInfo.GuidList = NULL;
	FdoData->WmiLibInfo.QueryWmiRegInfo = VirtUsb_QueryWmiRegInfo;
	FdoData->WmiLibInfo.QueryWmiDataBlock = VirtUsb_QueryWmiDataBlock;
	FdoData->WmiLibInfo.SetWmiDataBlock = VirtUsb_SetWmiDataBlock;
	FdoData->WmiLibInfo.SetWmiDataItem = VirtUsb_SetWmiDataItem;
	FdoData->WmiLibInfo.ExecuteWmiMethod = NULL;
	FdoData->WmiLibInfo.WmiFunctionControl = NULL;

	// Register with WMI
	status = IoWMIRegistrationControl(FdoData->Self,
	                                  WMIREG_ACTION_REGISTER);

	return status;
}

NTSTATUS
VirtUsb_WmiDeRegistration (
	_In_ PFDO_DEVICE_DATA FdoData
	)
/*
    Inform WMI to remove this DeviceObject from its
    list of providers. This function also
    decrements the reference count of the deviceobject.
*/
{
	PAGED_CODE();

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_WMI, ">%!FUNC!");

	return IoWMIRegistrationControl(FdoData->Self,
	                                WMIREG_ACTION_DEREGISTER);
}

NTSTATUS
VirtUsb_SetWmiDataItem(
	_In_ PDEVICE_OBJECT DeviceObject,
	_In_ PIRP           Irp,
	_In_ ULONG          GuidIndex,
	_In_ ULONG          InstanceIndex,
	_In_ ULONG          DataItemId,
	_In_ ULONG          BufferSize,
	_In_ PUCHAR         Buffer
	)
/*
    This routine is a callback into the driver to set for the contents of
    a data block. When the driver has finished filling the data block it
    must call WmiCompleteRequest to complete the irp. The driver can
    return STATUS_PENDING if the irp cannot be completed immediately.

Arguments:
    DeviceObject is the device whose data block is being queried

    Irp is the Irp that makes this request

    GuidIndex is the index into the list of guids provided when the
        device registered

    InstanceIndex is the index that denotes which instance of the data block
        is being queried.

    DataItemId has the id of the data item being set

    BufferSize has the size of the data item passed

    Buffer has the new values for the data item
*/
{
	PFDO_DEVICE_DATA fdoData;
	NTSTATUS         status;
	ULONG            requiredSize = 0;

	UNREFERENCED_PARAMETER(InstanceIndex);
	UNREFERENCED_PARAMETER(DataItemId);
	UNREFERENCED_PARAMETER(BufferSize);
	UNREFERENCED_PARAMETER(Buffer);

	PAGED_CODE();

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_WMI, ">%!FUNC!");

	fdoData = (PFDO_DEVICE_DATA)DeviceObject->DeviceExtension;

	switch(GuidIndex)
	{
	default:
		status = STATUS_WMI_GUID_NOT_FOUND;
	}

	status = WmiCompleteRequest(DeviceObject,
	                            Irp,
	                            status,
	                            requiredSize,
	                            IO_NO_INCREMENT);

	return status;
}

NTSTATUS
VirtUsb_SetWmiDataBlock(
	_In_ PDEVICE_OBJECT DeviceObject,
	_In_ PIRP           Irp,
	_In_ ULONG          GuidIndex,
	_In_ ULONG          InstanceIndex,
	_In_ ULONG          BufferSize,
	_In_ PUCHAR         Buffer
	)
/*
    This routine is a callback into the driver to set the contents of
    a data block. When the driver has finished filling the data block it
    must call WmiCompleteRequest to complete the irp. The driver can
    return STATUS_PENDING if the irp cannot be completed immediately.

Arguments:
    DeviceObject is the device whose data block is being queried

    Irp is the Irp that makes this request

    GuidIndex is the index into the list of guids provided when the
        device registered

    InstanceIndex is the index that denotes which instance of the data block
        is being queried.

    BufferSize has the size of the data block passed

    Buffer has the new values for the data block
*/
{
	PFDO_DEVICE_DATA fdoData;
	NTSTATUS         status;
	ULONG            requiredSize = 0;

	UNREFERENCED_PARAMETER(InstanceIndex);
	UNREFERENCED_PARAMETER(BufferSize);
	UNREFERENCED_PARAMETER(Buffer);

	PAGED_CODE();

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_WMI, ">%!FUNC!");

	fdoData = (PFDO_DEVICE_DATA)DeviceObject->DeviceExtension;

	switch(GuidIndex)
	{
	default:
		status = STATUS_WMI_GUID_NOT_FOUND;
	}

	status = WmiCompleteRequest(DeviceObject,
	                            Irp,
	                            status,
	                            requiredSize,
	                            IO_NO_INCREMENT);

	return(status);
}

NTSTATUS
VirtUsb_QueryWmiDataBlock(
	_In_    PDEVICE_OBJECT DeviceObject,
	_In_    PIRP           Irp,
	_In_    ULONG          GuidIndex,
	_In_    ULONG          InstanceIndex,
	_In_    ULONG          InstanceCount,
	_Inout_ PULONG         InstanceLengthArray,
	_In_    ULONG          BufferAvail,
	_Out_   PUCHAR         Buffer
	)
/*
    This routine is a callback into the driver to query for the contents of
    a data block. When the driver has finished filling the data block it
    must call WmiCompleteRequest to complete the irp. The driver can
    return STATUS_PENDING if the irp cannot be completed immediately.

Arguments:
    DeviceObject is the device whose data block is being queried

    Irp is the Irp that makes this request

    GuidIndex is the index into the list of guids provided when the
        device registered

    InstanceIndex is the index that denotes which instance of the data block
        is being queried.

    InstanceCount is the number of instnaces expected to be returned for
        the data block.

    InstanceLengthArray is a pointer to an array of ULONG that returns the
        lengths of each instance of the data block. If this is NULL then
        there was not enough space in the output buffer to fulfill the request
        so the irp should be completed with the buffer needed.

    BufferAvail on has the maximum size available to write the data
        block.

    Buffer on return is filled with the returned data block
*/
{
	PFDO_DEVICE_DATA fdoData;
	NTSTATUS         status;
	ULONG            size = 0;

	UNREFERENCED_PARAMETER(InstanceIndex);
	UNREFERENCED_PARAMETER(InstanceCount);
	UNREFERENCED_PARAMETER(InstanceLengthArray);
	UNREFERENCED_PARAMETER(BufferAvail);
	UNREFERENCED_PARAMETER(Buffer);

	PAGED_CODE();

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_WMI, ">%!FUNC!");

	// Only ever registers 1 instance per guid
	ASSERT((InstanceIndex == 0) && (InstanceCount == 1));

	fdoData = (PFDO_DEVICE_DATA)DeviceObject->DeviceExtension;

	switch(GuidIndex)
	{
	default:
		status = STATUS_WMI_GUID_NOT_FOUND;
	}

	status = WmiCompleteRequest(DeviceObject,
	                            Irp,
	                            status,
	                            size,
	                            IO_NO_INCREMENT);

	return status;
}

NTSTATUS
VirtUsb_QueryWmiRegInfo(
	_In_  PDEVICE_OBJECT  DeviceObject,
	_Out_ ULONG           *RegFlags,
	_Out_ PUNICODE_STRING InstanceName,
	_Out_ PUNICODE_STRING *RegistryPath,
	_Out_ PUNICODE_STRING MofResourceName,
	_Out_ PDEVICE_OBJECT  *Pdo
	)
/*
    This routine is a callback into the driver to retrieve the list of
    guids or data blocks that the driver wants to register with WMI. This
    routine may not pend or block. Driver should NOT call
    WmiCompleteRequest.

Arguments:
    DeviceObject is the device whose data block is being queried

    *RegFlags returns with a set of flags that describe the guids being
        registered for this device. If the device wants to enable and disable
        collection callbacks before receiving queries for the registered
        guids then it should return the WMIREG_FLAG_EXPENSIVE flag. Also the
        returned flags may specify WMIREG_FLAG_INSTANCE_PDO in which case
        the instance name is determined from the PDO associated with the
        device object. Note that the PDO must have an associated devnode. If
        WMIREG_FLAG_INSTANCE_PDO is not set then Name must return a unique
        name for the device.

    InstanceName returns with the instance name for the guids if
        WMIREG_FLAG_INSTANCE_PDO is not set in the returned *RegFlags. The
        caller will call ExFreePool with the buffer returned.

    *RegistryPath returns with the registry path of the driver

    *MofResourceName returns with the name of the MOF resource attached to
        the binary file. If the driver does not have a mof resource attached
        then this can be returned as NULL.

    *Pdo returns with the device object for the PDO associated with this
        device if the WMIREG_FLAG_INSTANCE_PDO flag is retured in
        *RegFlags.
*/
{
	PFDO_DEVICE_DATA fdoData;

	UNREFERENCED_PARAMETER(InstanceName);

	PAGED_CODE();

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_WMI, ">%!FUNC!");

	fdoData = (PFDO_DEVICE_DATA)DeviceObject->DeviceExtension;

	*RegFlags = WMIREG_FLAG_INSTANCE_PDO;
	*RegistryPath = &VirtUsb_RegPath;
	*Pdo = fdoData->UnderlyingPDO;
	RtlInitUnicodeString(MofResourceName, MOFRESOURCENAME);

	return STATUS_SUCCESS;
}
