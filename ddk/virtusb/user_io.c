#include "virtusb.h"
#include "user_io.h"
#include "roothub.h"
#include "proc_urb.h"
#include "work_unit.h"
#include "usbdev.h"

VOID
VirtUsb_CancelFetchWorkIrp(
	__inout PDEVICE_OBJECT DeviceObject,
	__in    PIRP           Irp
	);

NTSTATUS
VirtUsb_IOC_PortStat(
	IN PFILE_CONTEXT File,
	IN UCHAR         Index,
	IN USHORT        Status,
	IN USHORT        Change
	);

NTSTATUS
VirtUsb_IOC_FetchWork(
	IN  PFILE_CONTEXT File,
	OUT PVIRTUSB_WORK Work
	);

NTSTATUS
VirtUsb_IOC_Giveback(
	IN PFILE_CONTEXT                     File,
	IN UINT64                            Handle,
	IN USBD_STATUS                       UrbStatus,
	IN ULONG                             BufferActual,
	IN ULONG                             BufferLength,
	IN CONST VOID                        *Buffer,
	IN ULONG                             IsoPacketsLength,
	IN CONST VIRTUSB_ISO_PACKET_GIVEBACK *IsoPackets,
	IN ULONG                             IsoPacketCount,
	IN ULONG                             IsoErrorCount
	);

NTSTATUS
VirtUsb_IOC_FetchData(
	IN     PFILE_CONTEXT                    File,
	IN     UINT64                           Handle,
	IN OUT ULONG                            *BufferLength,
	OUT    CONST VOID                       **Buffer,
	OUT    PMDL                             *Mdl,
	IN     ULONG                            IsoPacketCount,
	OUT    CONST USBD_ISO_PACKET_DESCRIPTOR **IsoPackets
	);

NTSTATUS
VirtUsb_DispatchCreateClose(
	IN PDEVICE_OBJECT DeviceObject,
	IN PIRP           Irp
	)
{
	PIO_STACK_LOCATION  irpStack;
	NTSTATUS            status;
	PFDO_DEVICE_DATA    fdoData;
	PPDO_DEVICE_DATA    pdoData;
	PFILE_OBJECT        file;
	PFILE_CONTEXT       context;
	ULONG               i;
	BOOLEAN             pdoPresent, hasChanges = FALSE;

	ASSERT(KeGetCurrentIrql() < DISPATCH_LEVEL);

	VIRTUSB_KDPRINT("in VirtUsb_DispatchCreateClose\n");

	// We allow create/close requests for the FDO only.
	// That is the bus itself.
	fdoData = DeviceObject->DeviceExtension;
	if(!fdoData->IsFDO)
	{
		Irp->IoStatus.Information = 0;
		Irp->IoStatus.Status = status = STATUS_INVALID_DEVICE_REQUEST;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		return status;
	}

	if(!NT_SUCCESS(IoAcquireRemoveLock(&fdoData->RemoveLock, Irp)))
	{
		Irp->IoStatus.Information = 0;
		Irp->IoStatus.Status = status = STATUS_NO_SUCH_DEVICE;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		return status;
	}

	irpStack = IoGetCurrentIrpStackLocation(Irp);
	file = irpStack->FileObject;

	VIRTUSB_KDPRINT1("file object:          0x%p\n", file);
	VIRTUSB_KDPRINT1("file object context:  0x%p\n", file->FsContext);
	VIRTUSB_KDPRINT1("file object context2: 0x%p\n", file->FsContext2);
	VIRTUSB_KDPRINT1("device object:        0x%p\n", DeviceObject);
	VIRTUSB_KDPRINT1("fdevice object:       0x%p\n", file->DeviceObject);

	ASSERT(!file->FsContext2);

	switch(irpStack->MajorFunction)
	{
	case IRP_MJ_CREATE:
		VIRTUSB_KDPRINT("Create\n");
		ASSERT(!file->FsContext);

		// Check to see whether the bus is removed
		if(fdoData->DevicePnPState & Deleted)
		{
			Irp->IoStatus.Information = 0;
			Irp->IoStatus.Status = status = STATUS_NO_SUCH_DEVICE;
			IoCompleteRequest(Irp, IO_NO_INCREMENT);
			IoReleaseRemoveLock(&fdoData->RemoveLock, Irp);
			return status;
		}

		// file context gets set up during IOCREGISTER
		status = STATUS_SUCCESS;
		break;

	case IRP_MJ_CLOSE:
		VIRTUSB_KDPRINT("Close\n");

		context = (PFILE_CONTEXT)file->FsContext;
		if(context)
		{
			KIRQL   irql;
			PKEVENT *disconnectEvents = NULL;
			ULONG   numPorts, evIndex = 0;

			ASSERT(context->FileObj == file);
			ASSERT(context->ParentFdo == DeviceObject);
			ASSERT(context->HcdPdo);

			pdoData = context->HcdPdo->DeviceExtension;

			KeAcquireSpinLock(&pdoData->Lock, &irql);
			context->State = Closing;
			KeReleaseSpinLock(&pdoData->Lock, irql);
			VirtUsb_FailFileIO(context, STATUS_FILE_CLOSED);
			ASSERT(IsListEmpty(&context->FetchWorkIrpList));

			numPorts = context->PortCount;

			if(numPorts)
			{
				disconnectEvents = ExAllocatePoolWithTag(NonPagedPool,
				                                         (sizeof(PKEVENT) + sizeof(KWAIT_BLOCK)) * numPorts,
				                                         VIRTUSB_POOL_TAG);
			}

			KeAcquireSpinLock(&pdoData->Lock, &irql);
			if(pdoData->RootHub)
			{
				for(i = 0; i < numPorts; i++)
				{
					// make sure port has disconnected state
					USHORT newStatus = pdoData->RootHub->Port[i].Status & (USB_PORT_STATUS_POWER | USB_PORT_STATUS_OVER_CURRENT);
					if(newStatus != pdoData->RootHub->Port[i].Status)
					{
						VIRTUSB_KDPRINT1("DISCONNECT port %lu\n", i + 1);
						pdoData->RootHub->Port[i].Change |= pdoData->RootHub->Port[i].Status ^ newStatus;
						pdoData->RootHub->Port[i].Status = newStatus;
						hasChanges = TRUE;
						if(disconnectEvents)
						{
							disconnectEvents[evIndex] = &pdoData->RootHub->Port[i].HubDriverTouchedEvent;
							KeClearEvent(disconnectEvents[evIndex]);
							evIndex++;
						}
					}
				}
			}
			KeReleaseSpinLock(&pdoData->Lock, irql);
			if(hasChanges)
			{
				VirtUsb_RootHubNotify(pdoData);
				if(disconnectEvents)
				{
					LARGE_INTEGER timeout;
					timeout.QuadPart = -50000000; // 5 seconds

					// wait for the hub driver to issue GET_PORT_STATUS requests for all the
					// disconnected ports
					status = KeWaitForMultipleObjects(evIndex,
					                                  disconnectEvents,
					                                  WaitAll,
					                                  Executive,
					                                  KernelMode,
					                                  FALSE,
					                                  &timeout,
					                                  (PKWAIT_BLOCK)(disconnectEvents + evIndex));
				}
			}

			if(disconnectEvents)
			{
				ExFreePoolWithTag(disconnectEvents, VIRTUSB_POOL_TAG);
			}

			VIRTUSB_KDPRINT("cleanup file context before closing\n");

			pdoPresent = FALSE;
			ExAcquireFastMutex(&fdoData->Mutex);

			RemoveEntryList(&context->Link);

			KeAcquireSpinLock(&pdoData->Lock, &irql);
			pdoData->ParentFile = NULL;
			if(pdoData->Present)
			{
				pdoData->Present = FALSE;
				pdoPresent = TRUE;
			}
			KeReleaseSpinLock(&pdoData->Lock, irql);

			ExReleaseFastMutex(&fdoData->Mutex);

			// If closing this file will cause the pdo to be lost, inform PnP about it.
			ASSERT(pdoPresent);
			if(pdoPresent)
			{
				IoInvalidateDeviceRelations(fdoData->UnderlyingPDO, BusRelations);
			}

#if DBG
			file->FsContext = (PVOID)(ULONG_PTR)0xdeadf00d;
#endif

			ObDereferenceObject(context->HcdPdo);
			ExFreePoolWithTag(context, VIRTUSB_FILE_POOL_TAG);
		}
		else
		{
			VIRTUSB_KDPRINT("closing file object with no context (IOCREGISTER was never called?)\n");
		}

		status = STATUS_SUCCESS;
		break;

	default:
		status = STATUS_INVALID_DEVICE_REQUEST;
		break;
	}

	Irp->IoStatus.Information = 0;
	Irp->IoStatus.Status = status;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	IoReleaseRemoveLock(&fdoData->RemoveLock, Irp);
	return status;
}

NTSTATUS
VirtUsb_DispatchDeviceControl(
	IN PDEVICE_OBJECT DeviceObject,
	IN PIRP           Irp
	)
{
	KIRQL                            irql, cancelIrql;
	NTSTATUS                         status = STATUS_SUCCESS;
	ULONG_PTR                        inf = 0;
	PIO_STACK_LOCATION               irpStack;
	CONST VIRTUSB_REGISTER           *reg;
	CONST VIRTUSB_PORT_STAT          *stat;
	VIRTUSB_PORT_STAT                stat_cpy;
	PVIRTUSB_WORK                    work;
	PVIRTUSB_URB_DATA                udata;
	CONST VIRTUSB_GIVEBACK           *gb;
	ULONG                            i, user_len, iso_count, err_count;
	PVOID                            user_buf, buffer;
	CONST USBD_ISO_PACKET_DESCRIPTOR *iso_packets;
	USBD_STATUS                      urb_status;
	UINT64                           handle;
	PFILE_CONTEXT                    file;
	PFDO_DEVICE_DATA                 fdoData = DeviceObject->DeviceExtension;
	PPDO_DEVICE_DATA                 pdoData;
	PMDL                             mdl;
#ifdef _WIN64
	BOOLEAN                          user32;
#endif

	ASSERT(KeGetCurrentIrql() < DISPATCH_LEVEL);

	//VIRTUSB_KDPRINT("in VirtUsb_DispatchDeviceControl\n");

	if(!fdoData->IsFDO)
	{
		Irp->IoStatus.Status = status = STATUS_INVALID_DEVICE_REQUEST;
		Irp->IoStatus.Information = inf;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		return status;
	}

	if(!NT_SUCCESS(IoAcquireRemoveLock(&fdoData->RemoveLock, Irp)))
	{
		Irp->IoStatus.Status = status = STATUS_NO_SUCH_DEVICE;
		Irp->IoStatus.Information = inf;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		return status;
	}

	if(fdoData->DevicePnPState & Deleted)
	{
		Irp->IoStatus.Status = status = STATUS_NO_SUCH_DEVICE;
		Irp->IoStatus.Information = inf;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		IoReleaseRemoveLock(&fdoData->RemoveLock, Irp);
		return status;
	}

	irpStack = IoGetCurrentIrpStackLocation(Irp);
	file = (PFILE_CONTEXT)irpStack->FileObject->FsContext;

	//VIRTUSB_KDPRINT1("file object:          0x%p\n", irpStack->FileObject);
	//VIRTUSB_KDPRINT1("file object context:  0x%p\n", file);
	//VIRTUSB_KDPRINT1("device object:        0x%p\n", DeviceObject);

	switch(irpStack->Parameters.DeviceIoControl.IoControlCode)
	{
	case VIRTUSB_IOCREGISTER:
		VIRTUSB_KDPRINT("IOCREGISTER\n");

		if(file)
		{
			VIRTUSB_KDPRINT("already done!\n");
			status = STATUS_UNSUCCESSFUL;
			break;
		}

		reg = (PVIRTUSB_REGISTER)Irp->AssociatedIrp.SystemBuffer;
		if(!reg)
		{
			VIRTUSB_KDPRINT("no buffer\n");
			status = STATUS_INVALID_PARAMETER;
			break;
		}
		if(irpStack->Parameters.DeviceIoControl.InputBufferLength < sizeof(VIRTUSB_REGISTER))
		{
			VIRTUSB_KDPRINT("input buffer too small\n");
			status = STATUS_BUFFER_TOO_SMALL;
			break;
		}
		user_len = irpStack->Parameters.DeviceIoControl.OutputBufferLength;
		if(user_len && user_len < sizeof(ULONG))
		{
			VIRTUSB_KDPRINT("output buffer too small\n");
			status = STATUS_BUFFER_TOO_SMALL;
			break;
		}
		if(reg->Version != 1)
		{
			VIRTUSB_KDPRINT1("unknown version: %lu\n", (ULONG)reg->Version);
			status = STATUS_UNKNOWN_REVISION;
			break;
		}
		VIRTUSB_KDPRINT1("version: %lu\n", (ULONG)reg->Version);
		if(!reg->PortCount)
		{
			VIRTUSB_KDPRINT("port count is zero\n");
			status = STATUS_INVALID_PARAMETER;
			break;
		}

		file = ExAllocatePoolWithTag(NonPagedPool, sizeof(FILE_CONTEXT), VIRTUSB_FILE_POOL_TAG);
		if(!file)
		{
			status = STATUS_INSUFFICIENT_RESOURCES;
			VIRTUSB_KDPRINT("failed to allocate file context\n");
			break;
		}
		VIRTUSB_KDPRINT1("file context: 0x%p\n", file);
		RtlZeroMemory(file, sizeof(FILE_CONTEXT));

		file->IocVersion = reg->Version;
		file->PortCount = reg->PortCount;
		file->ParentFdo = DeviceObject;
		file->FileObj = irpStack->FileObject;
#ifdef _WIN64
		file->User32 = IoIs32bitProcess(NULL);
		if(file->User32)
		{
			VIRTUSB_KDPRINT("process is 32 bit\n");
		}
		else
		{
			VIRTUSB_KDPRINT("process is 64 bit\n");
		}
#endif

		InitializeListHead(&file->FetchWorkIrpList);

		ExAcquireFastMutex(&fdoData->Mutex);
		InsertTailList(&fdoData->ListOfFiles, &file->Link);
		ExReleaseFastMutex(&fdoData->Mutex);

		status = VirtUsb_CreatePdo(file);
		if(!NT_SUCCESS(status))
		{
			ExAcquireFastMutex(&fdoData->Mutex);
			RemoveEntryList(&file->Link);
			ExReleaseFastMutex(&fdoData->Mutex);
			break;
		}

		ASSERT(file->HcdPdo);
		pdoData = file->HcdPdo->DeviceExtension;

		ObReferenceObject(file->HcdPdo); // we keep this ref until file gets closed

		if(user_len)
		{
			*((PULONG)Irp->AssociatedIrp.SystemBuffer) = pdoData->Id;
			inf = sizeof(ULONG);
			VIRTUSB_KDPRINT("reported id to user-mode\n");
		}
		file->State = Registered;
		irpStack->FileObject->FsContext = file;
		break;

	default:
		if(!file)
		{
			switch(irpStack->Parameters.DeviceIoControl.IoControlCode)
			{
			case VIRTUSB_IOCPORTSTAT:
			case VIRTUSB_IOCFETCHWORK:
			case VIRTUSB_IOCGIVEBACK:
			case VIRTUSB_IOCFETCHDATA:
				VIRTUSB_KDPRINT("missing IOCREGISTER!\n");
				status = STATUS_UNSUCCESSFUL;
				break;
			default:
				VIRTUSB_KDPRINT1("invalid IOCTL: 0x%08lx\n", irpStack->Parameters.DeviceIoControl.IoControlCode);
				status = STATUS_INVALID_DEVICE_REQUEST;
				break;
			}
			break;
		}
#ifdef _WIN64
		user32 = file->User32;
#endif

		ASSERT(file->HcdPdo);
		pdoData = file->HcdPdo->DeviceExtension;

		switch(irpStack->Parameters.DeviceIoControl.IoControlCode)
		{
		case VIRTUSB_IOCPORTSTAT:
			VIRTUSB_KDPRINT("IOCPORTSTAT\n");

			stat = (PVIRTUSB_PORT_STAT)irpStack->Parameters.DeviceIoControl.Type3InputBuffer;
			if(!stat)
			{
				VIRTUSB_KDPRINT("no input buffer\n");
				status = STATUS_INVALID_PARAMETER;
				break;
			}
			if(irpStack->Parameters.DeviceIoControl.InputBufferLength < sizeof(VIRTUSB_PORT_STAT))
			{
				VIRTUSB_KDPRINT("input buffer too small\n");
				status = STATUS_BUFFER_TOO_SMALL;
				break;
			}

			try
			{
				ProbeForRead((PVOID)stat, sizeof(VIRTUSB_PORT_STAT), min(TYPE_ALIGNMENT(VIRTUSB_PORT_STAT), TYPE_ALIGNMENT(ULONG_PTR)));
				stat_cpy = *stat;
			}
			except(EXCEPTION_EXECUTE_HANDLER)
			{
				VIRTUSB_KDPRINT("reading input buffer failed\n");
				status = GetExceptionCode();
				break;
			}

			if(stat_cpy.Flags)
			{
				status = STATUS_INVALID_PARAMETER;
				break;
			}

			status = VirtUsb_IOC_PortStat(file,
			                              stat_cpy.PortIndex,
			                              stat_cpy.Status,
			                              stat_cpy.Change);
			break;

		case VIRTUSB_IOCFETCHWORK:
			//VIRTUSB_KDPRINT("IOCFETCHWORK\n");

			user_buf = Irp->AssociatedIrp.SystemBuffer;
			if(!user_buf)
			{
				VIRTUSB_KDPRINT("no buffer\n");
				status = STATUS_INVALID_PARAMETER;
				break;
			}

			work = (PVIRTUSB_WORK)user_buf;
			user_len = irpStack->Parameters.DeviceIoControl.OutputBufferLength;
			if(user_len < sizeof(VIRTUSB_WORK))
			{
				VIRTUSB_KDPRINT("output buffer too small\n");
				status = STATUS_BUFFER_TOO_SMALL;
				break;
			}

			KeAcquireSpinLock(&pdoData->Lock, &irql);
			status = VirtUsb_IOC_FetchWork(file, work);
			if(status == STATUS_PENDING)
			{
				IoMarkIrpPending(Irp);
				IoAcquireCancelSpinLock(&cancelIrql);
				IoSetCancelRoutine(Irp, &VirtUsb_CancelFetchWorkIrp);
				if(Irp->Cancel)
				{
					PDRIVER_CANCEL cancelCallback = IoSetCancelRoutine(Irp, NULL);
					IoReleaseCancelSpinLock(cancelIrql);
					InitializeListHead(&Irp->Tail.Overlay.ListEntry);
					KeReleaseSpinLock(&pdoData->Lock, irql);
					if(cancelCallback)
					{
						status = STATUS_CANCELLED;
						break; // complete irp
					}
					// cancel routine was already called, so we have to return STATUS_PENDING
				}
				else
				{
					IoReleaseCancelSpinLock(cancelIrql);
					InsertTailList(&file->FetchWorkIrpList, &Irp->Tail.Overlay.ListEntry);
					KeReleaseSpinLock(&pdoData->Lock, irql);
				}
				//VIRTUSB_KDPRINT("*** fetch work irp pending\n");
				status = STATUS_PENDING;
				return status;
			}
			else if(NT_SUCCESS(status))
			{
				inf = sizeof(VIRTUSB_WORK);
			}
			KeReleaseSpinLock(&pdoData->Lock, irql);
			break;

		case VIRTUSB_IOCGIVEBACK:
			VIRTUSB_KDPRINT("IOCGIVEBACK\n");

			gb = (PVIRTUSB_GIVEBACK)irpStack->Parameters.DeviceIoControl.Type3InputBuffer;
			if(!gb)
			{
				VIRTUSB_KDPRINT("no input buffer\n");
				status = STATUS_INVALID_PARAMETER;
				break;
			}
			if(irpStack->Parameters.DeviceIoControl.InputBufferLength < SIZEOF_VIRTUSB_GIVEBACK(0))
			{
				VIRTUSB_KDPRINT("input buffer too small\n");
				status = STATUS_BUFFER_TOO_SMALL;
				break;
			}

			try
			{
#ifdef _WIN64
				if(TYPE_ALIGNMENT(VIRTUSB_GIVEBACK) > TYPE_ALIGNMENT(ULONG) && user32)
				{
					ProbeForRead((PVOID)gb, SIZEOF_VIRTUSB_GIVEBACK(0), TYPE_ALIGNMENT(ULONG));
					if((ULONG_PTR)gb & (TYPE_ALIGNMENT(VIRTUSB_GIVEBACK) - 1))
					{
						// buffer is misaligned for 64-bit access -- split to 32-bit
						*((ULONG *)&handle) = *((ULONG *)&gb->Handle);
						*((ULONG *)&handle + 1) = *((ULONG *)&gb->Handle + 1);
					}
					else
					{
						handle = gb->Handle;
					}
				}
				else
				{
#endif
					ProbeForRead((PVOID)gb, SIZEOF_VIRTUSB_GIVEBACK(0), min(TYPE_ALIGNMENT(VIRTUSB_GIVEBACK), TYPE_ALIGNMENT(ULONG_PTR)));
					handle = gb->Handle;
#ifdef _WIN64
				}
#endif
				user_len = gb->BufferActual;
				iso_count = gb->PacketCount;
				err_count = gb->ErrorCount;
				urb_status = gb->Status;
			}
			except(EXCEPTION_EXECUTE_HANDLER)
			{
				VIRTUSB_KDPRINT("reading input buffer failed\n");
				status = GetExceptionCode();
				break;
			}
			if(!handle)
			{
				VIRTUSB_KDPRINT("handle is NULL\n");
				status = STATUS_INVALID_PARAMETER;
				break;
			}

			user_buf = Irp->UserBuffer;

			status = VirtUsb_IOC_Giveback(file,
			                              handle,
			                              urb_status,
			                              user_len,
			                              irpStack->Parameters.DeviceIoControl.OutputBufferLength,
			                              user_buf,
			                              (irpStack->Parameters.DeviceIoControl.InputBufferLength - SIZEOF_VIRTUSB_GIVEBACK(0)) / sizeof(VIRTUSB_ISO_PACKET_GIVEBACK),
			                              &gb->IsoPackets[0],
			                              iso_count,
			                              err_count);
			break;

		case VIRTUSB_IOCFETCHDATA:
			VIRTUSB_KDPRINT("IOCFETCHDATA\n");

			udata = (PVIRTUSB_URB_DATA)irpStack->Parameters.DeviceIoControl.Type3InputBuffer;
			if(!udata)
			{
				VIRTUSB_KDPRINT("no input buffer\n");
				status = STATUS_INVALID_PARAMETER;
				break;
			}
			if(irpStack->Parameters.DeviceIoControl.InputBufferLength < SIZEOF_VIRTUSB_URB_DATA(0))
			{
				VIRTUSB_KDPRINT("input buffer too small\n");
				status = STATUS_BUFFER_TOO_SMALL;
				break;
			}

			try
			{
#ifdef _WIN64
				if(TYPE_ALIGNMENT(VIRTUSB_URB_DATA) > TYPE_ALIGNMENT(ULONG) && user32)
				{
					ProbeForRead(udata, SIZEOF_VIRTUSB_URB_DATA(0), TYPE_ALIGNMENT(ULONG));
					if((ULONG_PTR)udata & (TYPE_ALIGNMENT(VIRTUSB_URB_DATA) - 1))
					{
						// buffer is misaligned for 64-bit access -- split to 32-bit
						*((ULONG *)&handle) = *((ULONG *)&udata->Handle);
						*((ULONG *)&handle + 1) = *((ULONG *)&udata->Handle + 1);
					}
					else
					{
						handle = udata->Handle;
					}
				}
				else
				{
#endif
					ProbeForRead(udata, SIZEOF_VIRTUSB_URB_DATA(0), min(TYPE_ALIGNMENT(VIRTUSB_URB_DATA), TYPE_ALIGNMENT(ULONG_PTR)));
					handle = udata->Handle;
#ifdef _WIN64
				}
#endif
				iso_count = udata->PacketCount;
			}
			except(EXCEPTION_EXECUTE_HANDLER)
			{
				VIRTUSB_KDPRINT("reading input buffer failed\n");
				status = GetExceptionCode();
				break;
			}
			if(!handle)
			{
				VIRTUSB_KDPRINT("handle is NULL\n");
				status = STATUS_INVALID_PARAMETER;
				break;
			}

			user_buf = Irp->UserBuffer;
			user_len = irpStack->Parameters.DeviceIoControl.OutputBufferLength;

			status = VirtUsb_IOC_FetchData(file,
			                               handle,
			                               &user_len,
			                               &buffer,
			                               &mdl,
			                               iso_count,
			                               &iso_packets);

			if(user_len)
			{
				if(!buffer && mdl)
				{
					buffer = MmGetSystemAddressForMdlSafe(mdl, NormalPagePriority);
					// TODO: do we have to clean up something, or is the creator of this mdl doing this?
				}
				if(buffer)
				{
					if(!user_buf)
					{
						VIRTUSB_KDPRINT("no output buffer\n");
						status = STATUS_INVALID_PARAMETER;
						break;
					}
					try
					{
						ProbeForWrite(user_buf, user_len, TYPE_ALIGNMENT(UCHAR));
						RtlCopyMemory(user_buf, buffer, user_len);
					}
					except(EXCEPTION_EXECUTE_HANDLER)
					{
						VIRTUSB_KDPRINT("writing output buffer failed\n");
						status = GetExceptionCode();
						break;
					}
					inf = user_len;
				}
			}
			if(iso_packets && iso_count)
			{
				if(irpStack->Parameters.DeviceIoControl.InputBufferLength < SIZEOF_VIRTUSB_URB_DATA(iso_count))
				{
					VIRTUSB_KDPRINT("input buffer too small to store iso packet array\n");
					status = STATUS_BUFFER_TOO_SMALL;
					break;
				}
				try
				{
					ProbeForWrite(&udata->IsoPackets[0], iso_count * sizeof(VIRTUSB_ISO_PACKET_DATA), min(TYPE_ALIGNMENT(VIRTUSB_ISO_PACKET_DATA), TYPE_ALIGNMENT(ULONG_PTR)));
					for(i = 0; i < iso_count; i++)
					{
						udata->IsoPackets[i].Offset = iso_packets[i].Offset;
						// TODO: is this right?
						udata->IsoPackets[i].PacketLength = iso_packets[i].Length;
					}
				}
				except(EXCEPTION_EXECUTE_HANDLER)
				{
					VIRTUSB_KDPRINT("writing iso packet array to input buffer failed\n");
					status = GetExceptionCode();
					break;
				}
			}
			break;

		default:
			VIRTUSB_KDPRINT1("invalid IOCTL: 0x%08lx\n", irpStack->Parameters.DeviceIoControl.IoControlCode);
			status = STATUS_INVALID_DEVICE_REQUEST;
			break;
		}
	}

	Irp->IoStatus.Status = status;
	Irp->IoStatus.Information = inf;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	IoReleaseRemoveLock(&fdoData->RemoveLock, Irp);
	return status;
}

NTSTATUS
VirtUsb_IOC_PortStat(
	IN PFILE_CONTEXT File,
	IN UCHAR         Index,
	IN USHORT        Status,
	IN USHORT        Change
	)
{
	KIRQL            irql;
	PUSBHUB_CONTEXT  rhub;
	PPDO_DEVICE_DATA pdoData;
	USHORT           overcurrent;

	ASSERT(KeGetCurrentIrql() < DISPATCH_LEVEL);

	VIRTUSB_KDPRINT("in VirtUsb_IOC_PortStat\n");

	ASSERT(File);
	ASSERT(File->HcdPdo);
	pdoData = File->HcdPdo->DeviceExtension;
	rhub = pdoData->RootHub;
	ASSERT(rhub);

	VIRTUSB_KDPRINT1("HCD-ID:    %lu\n", pdoData->Id);
	VIRTUSB_KDPRINT1("PortIndex: %lu\n", (ULONG)Index);
	VIRTUSB_KDPRINT1("Status:    0x%04hx\n", Status);
	VIRTUSB_KDPRINT1("Change:    0x%04hx\n", Change);

	if(!Index || Index > rhub->PortCount)
	{
		return STATUS_INVALID_PARAMETER;
	}

	if(Change != USB_PORT_STATUS_CONNECT &&
	   Change != USB_PORT_STATUS_ENABLE &&
	   Change != USB_PORT_STATUS_SUSPEND &&
	   Change != USB_PORT_STATUS_OVER_CURRENT &&
	   Change != USB_PORT_STATUS_RESET &&
	   Change != (USB_PORT_STATUS_RESET | USB_PORT_STATUS_ENABLE))
	{
		return STATUS_INVALID_PARAMETER;
	}

	KeAcquireSpinLock(&pdoData->Lock, &irql);

	if(!(rhub->Port[Index - 1].Status & USB_PORT_STATUS_POWER))
	{
		KeReleaseSpinLock(&pdoData->Lock, irql);
		return STATUS_UNSUCCESSFUL;
	}

	switch(Change)
	{
	case USB_PORT_STATUS_CONNECT:
		overcurrent = rhub->Port[Index - 1].Status & USB_PORT_STATUS_OVER_CURRENT;
		rhub->Port[Index - 1].Change |= USB_PORT_STATUS_CONNECT;
		if(Status & USB_PORT_STATUS_CONNECT)
		{
			rhub->Port[Index - 1].Status = USB_PORT_STATUS_POWER | USB_PORT_STATUS_CONNECT |
				((Status & USB_PORT_STATUS_LOW_SPEED) ? USB_PORT_STATUS_LOW_SPEED :
				((Status & USB_PORT_STATUS_HIGH_SPEED) ? USB_PORT_STATUS_HIGH_SPEED : 0)) |
				overcurrent;
		}
		else
		{
			rhub->Port[Index - 1].Status = USB_PORT_STATUS_POWER | overcurrent;
		}
		rhub->Port[Index - 1].Flags &= ~VIRTUSB_PORT_STAT_FLAGS_RESUMING;
		if(rhub->Port[Index - 1].Status & USB_PORT_STATUS_CONNECT)
		{
			VIRTUSB_KDPRINT1("*** port %lu: device connected\n", (ULONG)Index);
		}
		else
		{
			VIRTUSB_KDPRINT1("*** port %lu: device disconnected\n", (ULONG)Index);
		}
		break;

	case USB_PORT_STATUS_ENABLE:
		if(!(rhub->Port[Index - 1].Status & USB_PORT_STATUS_CONNECT) ||
		   (rhub->Port[Index - 1].Status & USB_PORT_STATUS_RESET) ||
		   (Status & USB_PORT_STATUS_ENABLE))
		{
			KeReleaseSpinLock(&pdoData->Lock, irql);
			return STATUS_UNSUCCESSFUL;
		}
		rhub->Port[Index - 1].Change |= USB_PORT_STATUS_ENABLE;
		rhub->Port[Index - 1].Status &= ~USB_PORT_STATUS_ENABLE;
		rhub->Port[Index - 1].Flags &= ~VIRTUSB_PORT_STAT_FLAGS_RESUMING;
		rhub->Port[Index - 1].Status &= ~USB_PORT_STATUS_SUSPEND;
		VIRTUSB_KDPRINT1("*** port %lu: disabled\n", (ULONG)Index);
		break;

	case USB_PORT_STATUS_SUSPEND:
		if(!(rhub->Port[Index - 1].Status & USB_PORT_STATUS_CONNECT) ||
		   !(rhub->Port[Index - 1].Status & USB_PORT_STATUS_ENABLE) ||
		   (rhub->Port[Index - 1].Status & USB_PORT_STATUS_RESET) ||
		   (Status & USB_PORT_STATUS_SUSPEND))
		{
			KeReleaseSpinLock(&pdoData->Lock, irql);
			return STATUS_UNSUCCESSFUL;
		}
		rhub->Port[Index - 1].Flags &= ~VIRTUSB_PORT_STAT_FLAGS_RESUMING;
		rhub->Port[Index - 1].Change |= USB_PORT_STATUS_SUSPEND;
		rhub->Port[Index - 1].Status &= ~USB_PORT_STATUS_SUSPEND;
		VIRTUSB_KDPRINT1("*** port %lu: resumed\n", (ULONG)Index);
		break;

	case USB_PORT_STATUS_OVER_CURRENT:
		rhub->Port[Index - 1].Change |= USB_PORT_STATUS_OVER_CURRENT;
		rhub->Port[Index - 1].Status &= ~USB_PORT_STATUS_OVER_CURRENT;
		rhub->Port[Index - 1].Status |= Status & USB_PORT_STATUS_OVER_CURRENT;
		if(rhub->Port[Index - 1].Status & USB_PORT_STATUS_OVER_CURRENT)
		{
			VIRTUSB_KDPRINT1("*** port %lu: overcurrent detected\n", (ULONG)Index);
		}
		else
		{
			VIRTUSB_KDPRINT1("*** port %lu: overcurrent gone\n", (ULONG)Index);
		}
		break;

	default: // USB_PORT_STATUS_RESET [| USB_PORT_STATUS_ENABLE]
		if(!(rhub->Port[Index - 1].Status & USB_PORT_STATUS_CONNECT) ||
		   !(rhub->Port[Index - 1].Status & USB_PORT_STATUS_RESET) ||
		   (Status & USB_PORT_STATUS_RESET))
		{
			KeReleaseSpinLock(&pdoData->Lock, irql);
			return STATUS_UNSUCCESSFUL;
		}
		if(Change & USB_PORT_STATUS_ENABLE)
		{
			if(Status & USB_PORT_STATUS_ENABLE)
			{
				KeReleaseSpinLock(&pdoData->Lock, irql);
				return STATUS_UNSUCCESSFUL;
			}
			rhub->Port[Index - 1].Change |= USB_PORT_STATUS_ENABLE;
		}
		else
		{
			rhub->Port[Index - 1].Status |= Status & USB_PORT_STATUS_ENABLE;
		}
		rhub->Port[Index - 1].Change |= USB_PORT_STATUS_RESET;
		rhub->Port[Index - 1].Status &= ~USB_PORT_STATUS_RESET;
		if(rhub->Port[Index - 1].Status & USB_PORT_STATUS_ENABLE)
		{
			VIRTUSB_KDPRINT1("*** port %lu: reset complete\n", (ULONG)Index);
			if(rhub->Port[Index - 1].UsbDevice)
			{
				VIRTUSB_KDPRINT1("port %lu: device enters default state\n", (ULONG)Index);
				VIRTUSB_KDPRINT1("port %lu: set device address to 0x00 after reset\n", (ULONG)Index);
				rhub->Port[Index - 1].UsbDevice->State = DefaultDevState;
				rhub->Port[Index - 1].UsbDevice->Address = 0x00;
			}
			else
			{
				VIRTUSB_KDPRINT("no device\n");
			}
		}
		else
		{
			VIRTUSB_KDPRINT1("*** port %lu: reset failed\n", (ULONG)Index);
		}
		break;
	}

	VirtUsb_PortStatUpdateToUser(pdoData, Index);
	VirtUsb_GotWork(pdoData, irql); // releases spin lock

	VirtUsb_RootHubNotify(pdoData);

	return STATUS_SUCCESS;
}

NTSTATUS
VirtUsb_IOC_FetchWork(
	IN  PFILE_CONTEXT File,
	OUT PVIRTUSB_WORK Work
	)
/*
    caller has lock
*/
{
	PUSBHUB_CONTEXT  rhub;
	ULONG            _port, port;
	PLIST_ENTRY      entry;
	PURB             urb;
	PUSBDEV_CONTEXT  dev;
	PPIPE_CONTEXT    ep;
	PWORK            wu;
	PPDO_DEVICE_DATA pdoData;
	UCHAR            urbType = VIRTUSB_URB_TYPE_BULK;
	UCHAR            epAdr = 0;
	BOOLEAN          isIn;

	ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);

	//VIRTUSB_KDPRINT("in VirtUsb_IOC_FetchWork\n");

	ASSERT(File);
	ASSERT(Work);
	ASSERT(File->HcdPdo);
	pdoData = File->HcdPdo->DeviceExtension;
	rhub = pdoData->RootHub;
	ASSERT(rhub);

	//VIRTUSB_KDPRINT1("HCD-ID: %lu\n", pdoData->Id);

	if(!IsListEmpty(&pdoData->CancelWorkList))
	{
		entry = RemoveHeadList(&pdoData->CancelWorkList);
		ASSERT(entry != &pdoData->CancelWorkList);
		wu = CONTAINING_RECORD(entry, WORK, ListEntry);
		ASSERT(wu);
		VIRTUSB_KDPRINT1("IOCFETCHWORK: [work=CANCEL_URB handle=0x%016llx]\n", wu->Handle);
		Work->Type = VIRTUSB_WORK_TYPE_CANCEL_URB;
		Work->Urb.Handle = wu->Handle;
		InsertTailList(&pdoData->CancelingWorkList, &wu->ListEntry);
		return STATUS_SUCCESS;
	}

	if(VirtUsb_IsPortBitArraySet(pdoData->PortUpdate))
	{
		if(pdoData->PortSchedOffset >= rhub->PortCount)
			pdoData->PortSchedOffset = 0;
		for(_port = 0; _port < rhub->PortCount; _port++)
		{
			// The port which will be checked first, is rotated by PortSchedOffset, so that every port
			// has its chance to be reported to user space, even if the hcd is under heavy load.
			port = (_port + pdoData->PortSchedOffset) % rhub->PortCount;
			if(test_bit(port + 1, pdoData->PortUpdate))
			{
				clear_bit(port + 1, pdoData->PortUpdate);
				pdoData->PortSchedOffset = (UCHAR)(port + 1);
				VIRTUSB_KDPRINT3("IOCFETCHWORK: [work=PORT_STAT port=%lu status=0x%04hx change=0x%04hx]\n", port + 1, rhub->Port[port].Status, rhub->Port[port].Change);
				Work->Type = VIRTUSB_WORK_TYPE_PORT_STAT;
				Work->PortStat.PortIndex = (UCHAR)(port + 1);
				Work->PortStat.Status = rhub->Port[port].Status;
				Work->PortStat.Change = rhub->Port[port].Change;
				Work->PortStat.Flags = rhub->Port[port].Flags;
				return STATUS_SUCCESS;
			}
		}
	}

	if(!IsListEmpty(&pdoData->PendingWorkList))
	{
		entry = RemoveHeadList(&pdoData->PendingWorkList);
		ASSERT(entry != &pdoData->PendingWorkList);
		wu = CONTAINING_RECORD(entry, WORK, ListEntry);
		ASSERT(wu);
		ASSERT(wu->Irp);
		urb = VirtUsb_GetCurrentWorkUnitUrb(wu);
		ASSERT(urb);
		ep = wu->Pipe;
		ASSERT(ep);
		dev = ep->Device;
		ASSERT(dev);
		ASSERT(ep == (PPIPE_CONTEXT)urb->UrbControlTransfer.PipeHandle);

		isIn = !!(urb->UrbControlTransfer.TransferFlags & USBD_TRANSFER_DIRECTION_IN);

		switch(urb->UrbHeader.Function)
		{
		case URB_FUNCTION_CONTROL_TRANSFER:
			urbType = VIRTUSB_URB_TYPE_CONTROL;
			if(!ep->IsEP0)
			{
				ASSERT(ep->Descriptor);
				epAdr = ep->Descriptor->bEndpointAddress & 0x7f;
			}
			if(isIn) epAdr |= 0x80;
			break;
		case URB_FUNCTION_ISOCH_TRANSFER:
			ASSERT(ep->Descriptor);
			urbType = VIRTUSB_URB_TYPE_ISO;
			epAdr = ep->Descriptor->bEndpointAddress;
			break;
		case URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER:
			ASSERT(ep->Descriptor);
			if((ep->Descriptor->bmAttributes & 0x03) == USB_ENDPOINT_TYPE_INTERRUPT)
			{
				urbType = VIRTUSB_URB_TYPE_INT;
			}
			epAdr = ep->Descriptor->bEndpointAddress;
			break;
		default:
			ASSERT(FALSE);
			break;
		}

		Work->Type = VIRTUSB_WORK_TYPE_PROCESS_URB;
		Work->Urb.Handle = wu->Handle;
		Work->Urb.Address = dev->Address;
		Work->Urb.Endpoint = epAdr;
		Work->Urb.Type = urbType;
		Work->Urb.Flags = 0;
		if(urb->UrbControlTransfer.TransferFlags & USBD_SHORT_TRANSFER_OK)
			Work->Urb.Flags |= VIRTUSB_URB_FLAGS_SHORT_TRANSFER_OK;
		if(urb->UrbControlTransfer.TransferFlags & USBD_START_ISO_TRANSFER_ASAP)
			Work->Urb.Flags |= VIRTUSB_URB_FLAGS_START_ISO_TRANSFER_ASAP;
		if(urbType == VIRTUSB_URB_TYPE_CONTROL)
		{
			PVIRTUSB_SETUP_PACKET sp = (PVIRTUSB_SETUP_PACKET)urb->UrbControlTransfer.SetupPacket;
			Work->Urb.BufferLength = sp->wLength;
			Work->Urb.SetupPacket.bmRequestType = sp->bmRequestType;
			Work->Urb.SetupPacket.bRequest = sp->bRequest;
			Work->Urb.SetupPacket.wValue = sp->wValue;
			Work->Urb.SetupPacket.wIndex = sp->wIndex;
			Work->Urb.SetupPacket.wLength = sp->wLength;
		}
		else
		{
			Work->Urb.BufferLength = urb->UrbControlTransfer.TransferBufferLength;
		}
		if(urbType == VIRTUSB_URB_TYPE_ISO)
		{
			Work->Urb.PacketCount = urb->UrbIsochronousTransfer.NumberOfPackets;
		}
		else
		{
			Work->Urb.PacketCount = 0;
		}
		if(ep->IsEP0)
		{
			Work->Urb.Interval = 0;
		}
		else
		{
			Work->Urb.Interval = ep->Descriptor->bInterval;
		}

		VIRTUSB_KDPRINT1("IOCFETCHWORK: [work=PROCESS_URB handle=0x%016llx]\n", wu->Handle);
		// TODO: dump urb
		InsertTailList(&pdoData->FetchedWorkList, &wu->ListEntry);
		return STATUS_SUCCESS;
	}

	return STATUS_PENDING;
}

PWORK
FORCEINLINE
VirtUsb_FromUrbHandle(
	IN PPDO_DEVICE_DATA PdoData,
	IN UINT64           Handle
	)
/*
    caller has lock
*/
{
	PLIST_ENTRY        listHead, entry;
	PWORK              wu;
	ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
	listHead = &PdoData->FetchedWorkList;
	for(entry = listHead->Flink;
	    entry != listHead;
	    entry = entry->Flink)
	{
		wu = CONTAINING_RECORD(entry, WORK, ListEntry);
		if(wu->Handle == Handle)
		{
			return wu;
		}
	}
	return NULL;
}

PWORK
FORCEINLINE
VirtUsb_FromUrbHandleInCancel(
	IN PPDO_DEVICE_DATA PdoData,
	IN UINT64           Handle
	)
/*
    caller has lock
*/
{
	PLIST_ENTRY        listHead, entry;
	PWORK              wu;
	ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
	listHead = &PdoData->CancelWorkList;
	for(entry = listHead->Flink;
	    entry != listHead;
	    entry = entry->Flink)
	{
		wu = CONTAINING_RECORD(entry, WORK, ListEntry);
		if(wu->Handle == Handle)
		{
			return wu;
		}
	}
	return NULL;
}

PWORK
FORCEINLINE
VirtUsb_FromUrbHandleInCanceling(
	IN PPDO_DEVICE_DATA PdoData,
	IN UINT64           Handle
	)
/*
    caller has lock
*/
{
	PLIST_ENTRY        listHead, entry;
	PWORK              wu;
	ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
	listHead = &PdoData->CancelingWorkList;
	for(entry = listHead->Flink;
	    entry != listHead;
	    entry = entry->Flink)
	{
		wu = CONTAINING_RECORD(entry, WORK, ListEntry);
		if(wu->Handle == Handle)
		{
			return wu;
		}
	}
	return NULL;
}

NTSTATUS
VirtUsb_IOC_Giveback(
	IN PFILE_CONTEXT                     File,
	IN UINT64                            Handle,
	IN USBD_STATUS                       UrbStatus,
	IN ULONG                             BufferActual,
	IN ULONG                             BufferLength,
	IN CONST VOID                        *Buffer,
	IN ULONG                             IsoPacketsLength,
	IN CONST VIRTUSB_ISO_PACKET_GIVEBACK *IsoPackets,
	IN ULONG                             IsoPacketCount,
	IN ULONG                             IsoErrorCount
	)
{
	NTSTATUS         status = STATUS_SUCCESS;
	USBD_STATUS      usbStatus;
	KIRQL            irql, cancelIrql;
	PIRP             irp;
	PURB             urb;
	PWORK            wu;
	PPIPE_CONTEXT    ep;
	UCHAR            urbType = VIRTUSB_URB_TYPE_BULK;
	BOOLEAN          isIso, isIn, isCancel = FALSE;
	PDRIVER_CANCEL   cancelCallback = NULL;
	PPDO_DEVICE_DATA pdoData;

	UNREFERENCED_PARAMETER(BufferLength);

	ASSERT(KeGetCurrentIrql() < DISPATCH_LEVEL);

	VIRTUSB_KDPRINT("in VirtUsb_IOC_Giveback\n");

	ASSERT(File);
	ASSERT(Handle);
	ASSERT(IsoPackets);
	ASSERT(File->HcdPdo);
	pdoData = File->HcdPdo->DeviceExtension;

	VIRTUSB_KDPRINT1("HCD-ID: %lu\n", pdoData->Id);

	KeAcquireSpinLock(&pdoData->Lock, &irql);
	wu = VirtUsb_FromUrbHandle(pdoData, Handle);
	if(!wu)
	{
		// if not found, check the cancel{,ing} list
		wu = VirtUsb_FromUrbHandleInCanceling(pdoData, Handle);
		if(!wu) wu = VirtUsb_FromUrbHandleInCancel(pdoData, Handle);
		if(wu)
		{
			VIRTUSB_KDPRINT("GIVEBACK: urb was canceled\n");
			status = STATUS_REQUEST_ABORTED;
			isCancel = TRUE;
			ASSERT(!wu->Irp || wu->Irp->Cancel);
		}
		else
		{
			VIRTUSB_KDPRINT("GIVEBACK: handle not found\n");
			status = STATUS_NOT_FOUND;
			goto Err;
		}
	}
	RemoveEntryList(&wu->ListEntry);

	ep = wu->Pipe;
	ASSERT(ep);
	ASSERT(ep->Device);

	irp = wu->Irp;
	if(!irp)
	{
		ASSERT(isCancel);
		// irp got canceled on DISPATCH level (or cancel routine has timed out)
		KeReleaseSpinLock(&pdoData->Lock, irql);
		VirtUsb_DereferenceUsbDevice(ep->Device, irp, FALSE);
		VirtUsb_FreeWorkUnit(wu);
		return STATUS_REQUEST_ABORTED;
	}

	IoAcquireCancelSpinLock(&cancelIrql);
	cancelCallback = IoSetCancelRoutine(irp, NULL);
	IoReleaseCancelSpinLock(cancelIrql);

	urb = VirtUsb_GetCurrentWorkUnitUrb(wu);
	ASSERT(urb);

	KeReleaseSpinLock(&pdoData->Lock, irql);

	isIn = !!(urb->UrbControlTransfer.TransferFlags & USBD_TRANSFER_DIRECTION_IN);

	switch(urb->UrbHeader.Function)
	{
	case URB_FUNCTION_CONTROL_TRANSFER:
		urbType = VIRTUSB_URB_TYPE_CONTROL;
		break;
	case URB_FUNCTION_ISOCH_TRANSFER:
		urbType = VIRTUSB_URB_TYPE_ISO;
		break;
	case URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER:
		ASSERT(ep->Descriptor);
		if((ep->Descriptor->bmAttributes & 0x03) == USB_ENDPOINT_TYPE_INTERRUPT)
		{
			urbType = VIRTUSB_URB_TYPE_INT;
		}
		break;
	default:
		ASSERT(FALSE);
		break;
	}
	isIso = urbType == VIRTUSB_URB_TYPE_ISO;

	if(isIso)
	{
		if(IsoPacketsLength < IsoPacketCount)
		{
			VIRTUSB_KDPRINT("GIVEBACK(ISO): input buffer too small to contain iso packet array\n");
			status = STATUS_BUFFER_TOO_SMALL;
			goto DoneWithErrors;
		}
		if(isIn && BufferActual != urb->UrbIsochronousTransfer.TransferBufferLength)
		{
			VIRTUSB_KDPRINT("GIVEBACK(ISO): invalid: BufferActual != TransferBufferLength\n");
			status = STATUS_INVALID_PARAMETER;
			goto DoneWithErrors;
		}
		if(IsoPacketCount != urb->UrbIsochronousTransfer.NumberOfPackets)
		{
			VIRTUSB_KDPRINT("GIVEBACK(ISO): invalid: NumberOfPackets missmatch\n");
			status = STATUS_INVALID_PARAMETER;
			goto DoneWithErrors;
		}
		if(IsoPacketCount && !IsoPackets)
		{
			VIRTUSB_KDPRINT("GIVEBACK(ISO): invalid: IsoPackets must not be zero\n");
			status = STATUS_INVALID_PARAMETER;
			goto DoneWithErrors;
		}
		if(IsoPacketCount)
		{
			try
			{
				ProbeForRead((PVOID)IsoPackets, SIZEOF_VIRTUSB_GIVEBACK(IsoPacketCount) - SIZEOF_VIRTUSB_GIVEBACK(0), min(TYPE_ALIGNMENT(VIRTUSB_ISO_PACKET_GIVEBACK), TYPE_ALIGNMENT(ULONG_PTR)));
			}
			except(EXCEPTION_EXECUTE_HANDLER)
			{
				VIRTUSB_KDPRINT("GIVEBACK(ISO): reading input buffer failed\n");
				status = GetExceptionCode();
				goto DoneWithErrors;
			}
		}
	}
	else if(BufferActual > urb->UrbControlTransfer.TransferBufferLength)
	{
		VIRTUSB_KDPRINT("GIVEBACK: invalid: BufferActual > TransferBufferLength\n");
		status = STATUS_INVALID_PARAMETER;
		goto DoneWithErrors;
	}
	if(isIn)
	{
		if(BufferActual && !Buffer)
		{
			VIRTUSB_KDPRINT("GIVEBACK: Buffer must not be zero\n");
			status = STATUS_INVALID_PARAMETER;
			goto DoneWithErrors;
		}
		try
		{
			ProbeForRead((PVOID)Buffer, BufferActual, TYPE_ALIGNMENT(UCHAR));
			if(urb->UrbControlTransfer.TransferBuffer)
			{
				RtlCopyMemory(urb->UrbControlTransfer.TransferBuffer, Buffer, BufferActual);
			}
			else if(urb->UrbControlTransfer.TransferBufferMDL)
			{
				PVOID buf = MmGetSystemAddressForMdlSafe(urb->UrbControlTransfer.TransferBufferMDL, NormalPagePriority);
				RtlCopyMemory(buf, Buffer, BufferActual);
			}
			else
			{
				ASSERT(FALSE);
			}
		}
		except(EXCEPTION_EXECUTE_HANDLER)
		{
			VIRTUSB_KDPRINT("GIVEBACK: reading output buffer failed\n");
			status = GetExceptionCode();
			goto DoneWithErrors;
		}
	}
	else if(Buffer)
	{
		VIRTUSB_KDPRINT("GIVEBACK: Buffer should be NULL\n");
		status = STATUS_INVALID_PARAMETER;
		goto DoneWithErrors;
	}
	if(isIso)
	{
		if(IsoPacketCount)
		{
			try
			{
				ULONG i;
				for(i = 0; i < IsoPacketCount; i++)
				{
					urb->UrbIsochronousTransfer.IsoPacket[i].Status = IsoPackets[i].Status;
					urb->UrbIsochronousTransfer.IsoPacket[i].Length = IsoPackets[i].PacketActual;
				}
			}
			except(EXCEPTION_EXECUTE_HANDLER)
			{
				VIRTUSB_KDPRINT("GIVEBACK(ISO): reading input buffer failed\n");
				status = GetExceptionCode();
				goto DoneWithErrors;
			}
		}
		urb->UrbIsochronousTransfer.ErrorCount = IsoErrorCount;
	}
	else
	{
		urb->UrbControlTransfer.TransferBufferLength = BufferActual;
	}

	if(isCancel && (USBD_PENDING(UrbStatus) || UrbStatus == USBD_STATUS_CANCELED))
	{
		usbStatus = USBD_STATUS_CANCELED;
		status = STATUS_CANCELLED;
	}
	else
	{
		usbStatus = USBD_PENDING(UrbStatus) ? USBD_STATUS_INTERNAL_HC_ERROR : UrbStatus;
		status = USBD_SUCCESS(UrbStatus) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
	}

Complete:
	KeAcquireSpinLock(&pdoData->Lock, &irql);

	if(cancelCallback)
	{
		BOOLEAN hasMoreUrbs = VirtUsb_CompleteWorkUnitCurrentUrb(wu, usbStatus);
		if(!isCancel && hasMoreUrbs)
		{
			// If this work unit contains more urbs, then re-queue it.
			// We have to re-register the cancel routine first.
			IoAcquireCancelSpinLock(&cancelIrql);
			IoSetCancelRoutine(irp, &VirtUsb_CancelUrbIrp);
			if(irp->Cancel)
			{
				cancelCallback = IoSetCancelRoutine(irp, NULL);
				IoReleaseCancelSpinLock(cancelIrql);
				InitializeListHead(&wu->ListEntry); // (to 'no-op-ify' RemoveEntryList(&wu->ListEntry) in cancel routine)
				KeReleaseSpinLock(&pdoData->Lock, irql);
				if(cancelCallback)
				{
					VirtUsb_CompleteWorkUnitCurrentUrb(wu, USBD_STATUS_CANCELED);
					status = VirtUsb_CompleteWorkUnit(wu, STATUS_CANCELLED);
					VirtUsb_DereferenceUsbDevice(ep->Device, irp, FALSE);
					VirtUsb_FreeWorkUnit(wu);
					IoCompleteRequest(irp, IO_NO_INCREMENT);
					IoReleaseRemoveLock(&pdoData->RemoveLock, irp);
				}
			}
			else
			{
				IoReleaseCancelSpinLock(cancelIrql);
				InsertTailList(&pdoData->PendingWorkList, &wu->ListEntry);
				KeReleaseSpinLock(&pdoData->Lock, irql);
			}
		}
		else
		{
			KeReleaseSpinLock(&pdoData->Lock, irql);
			VirtUsb_CompleteWorkUnit(wu, status);
			VirtUsb_FreeWorkUnit(wu);
			VirtUsb_DereferenceUsbDevice(ep->Device, irp, FALSE);
			IoCompleteRequest(irp, IO_NO_INCREMENT);
			IoReleaseRemoveLock(&pdoData->RemoveLock, irp);
		}
	}
	else
	{
		ASSERT(isCancel);
		InitializeListHead(&wu->ListEntry);
		// check if cancel routine is already sleeping
		if(wu->CancelEvent)
		{
			// trigger IoCompleteRequest in the sleeping cancel routine
			KeSetEvent(wu->CancelEvent, IO_NO_INCREMENT, FALSE);
		}
		else
		{
			// the cancel routine did not sleep yet. we have to inform the cancel routine that
			// the irp can be completed now.
			wu->CancelRace = TRUE;
		}
		KeReleaseSpinLock(&pdoData->Lock, irql);
	}
	return status;

DoneWithErrors:
	usbStatus = USBD_STATUS_INTERNAL_HC_ERROR;
	goto Complete;

Err:
	KeReleaseSpinLock(&pdoData->Lock, irql);
	return status;
}

NTSTATUS
VirtUsb_IOC_FetchData(
	IN     PFILE_CONTEXT                    File,
	IN     UINT64                           Handle,
	IN OUT ULONG                            *BufferLength,
	OUT    CONST VOID                       **Buffer,
	OUT    PMDL                             *Mdl,
	IN     ULONG                            IsoPacketCount,
	OUT    CONST USBD_ISO_PACKET_DESCRIPTOR **IsoPackets
	)
{
	NTSTATUS         status = STATUS_SUCCESS;
	KIRQL            irql;
	PPDO_DEVICE_DATA pdoData;
	PURB             urb;
	PWORK            wu;
	PPIPE_CONTEXT    ep;
	ULONG            len;
	UCHAR            urbType = VIRTUSB_URB_TYPE_BULK;
	BOOLEAN          isIn;

	ASSERT(KeGetCurrentIrql() < DISPATCH_LEVEL);

	VIRTUSB_KDPRINT("in VirtUsb_IOC_FetchData\n");

	ASSERT(File);
	ASSERT(Handle);
	ASSERT(BufferLength);
	ASSERT(Buffer);
	ASSERT(Mdl);
	ASSERT(IsoPackets);
	pdoData = File->HcdPdo->DeviceExtension;

	VIRTUSB_KDPRINT1("HCD-ID: %lu\n", pdoData->Id);

	*Buffer = NULL;
	*Mdl = NULL;
	*IsoPackets = NULL;

	KeAcquireSpinLock(&pdoData->Lock, &irql);
	wu = VirtUsb_FromUrbHandle(pdoData, Handle);
	if(!wu)
	{
		// if not found, check the cancel{,ing} list
		wu = VirtUsb_FromUrbHandleInCanceling(pdoData, Handle);
		if(!wu) wu = VirtUsb_FromUrbHandleInCancel(pdoData, Handle);
		if(wu)
		{
			RemoveEntryList(&wu->ListEntry);
			ASSERT(!wu->Irp || wu->Irp->Cancel);
			if(!wu->Irp)
			{
				KeReleaseSpinLock(&pdoData->Lock, irql);
				VirtUsb_DereferenceUsbDevice(wu->Pipe->Device, NULL, FALSE);
				VirtUsb_FreeWorkUnit(wu);
				return STATUS_REQUEST_ABORTED;
			}
			// NOTE: we don't need to check if cancel routine got already called -- we know it was, when
			//       wu is in Cancel{,ing} list
			InitializeListHead(&wu->ListEntry);
			// check if cancel routine is already sleeping
			if(wu->CancelEvent)
			{
				// trigger IoCompleteRequest in the sleeping cancel routine
				KeSetEvent(wu->CancelEvent, IO_NO_INCREMENT, FALSE);
			}
			else
			{
				// the cancel routine did not sleep yet. we have to inform the cancel routine that
				// the irp can be completed now.
				wu->CancelRace = TRUE;
			}
			KeReleaseSpinLock(&pdoData->Lock, irql);
			return STATUS_REQUEST_ABORTED;
		}
		else
		{
			status = STATUS_NOT_FOUND;
			goto Err;
		}
	}

	ASSERT(wu->Irp);
	urb = VirtUsb_GetCurrentWorkUnitUrb(wu);
	ASSERT(urb);
	ep = wu->Pipe;
	ASSERT(ep);
	ASSERT(ep == (PPIPE_CONTEXT)urb->UrbControlTransfer.PipeHandle);

	isIn = !!(urb->UrbControlTransfer.TransferFlags & USBD_TRANSFER_DIRECTION_IN);

	switch(urb->UrbHeader.Function)
	{
	case URB_FUNCTION_CONTROL_TRANSFER:
		urbType = VIRTUSB_URB_TYPE_CONTROL;
		break;
	case URB_FUNCTION_ISOCH_TRANSFER:
		urbType = VIRTUSB_URB_TYPE_ISO;
		break;
	case URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER:
		// don't care about bulk or int (defaults to bulk)
		break;
	default:
		ASSERT(FALSE);
		break;
	}

	len = urb->UrbControlTransfer.TransferBufferLength;
	if(urbType == VIRTUSB_URB_TYPE_CONTROL)
	{
		PVIRTUSB_SETUP_PACKET sp = (PVIRTUSB_SETUP_PACKET)urb->UrbControlTransfer.SetupPacket;
		len = sp->wLength;
	}

	if(urbType == VIRTUSB_URB_TYPE_ISO)
	{
		if(IsoPacketCount != urb->UrbIsochronousTransfer.NumberOfPackets)
		{
			status = STATUS_INVALID_PARAMETER;
			goto Err;
		}
		if(IsoPacketCount)
		{
			*IsoPackets = &urb->UrbIsochronousTransfer.IsoPacket[0];
		}
	}
	else if(isIn || !len || !(urb->UrbControlTransfer.TransferBuffer || urb->UrbControlTransfer.TransferBufferMDL))
	{
		status = STATUS_NO_MORE_ENTRIES;
		goto Err;
	}

	if(!isIn && len)
	{
		if(!(urb->UrbControlTransfer.TransferBuffer || urb->UrbControlTransfer.TransferBufferMDL) ||
			*BufferLength < len)
		{
			status = STATUS_INVALID_PARAMETER;
			goto Err;
		}
		*BufferLength = len;
		*Buffer = urb->UrbControlTransfer.TransferBuffer;
		*Mdl = urb->UrbControlTransfer.TransferBufferMDL;
	}

Err:
	KeReleaseSpinLock(&pdoData->Lock, irql);
	return status;
}

VOID
VirtUsb_GotWork(
	IN PPDO_DEVICE_DATA PdoData,
	IN KIRQL            OldIrql
	)
/*
    caller has lock; this routine releases the lock
*/
{
	KIRQL              cancelIrql;
	PIRP               nextIrp;
	PLIST_ENTRY        listEntry;
	LIST_ENTRY         completeIrpList;
	PDRIVER_CANCEL     cancelRoutine;
	PIO_STACK_LOCATION irpStack;
	NTSTATUS           status;
	PVIRTUSB_WORK      work;
	PFILE_CONTEXT      file;
	PFDO_DEVICE_DATA   fdoData;

	ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);

	//VIRTUSB_KDPRINT("in VirtUsb_GotWork\n");

	ASSERT(PdoData);
	file = PdoData->ParentFile;
	if(!file)
	{
		// file closed
		KeReleaseSpinLock(&PdoData->Lock, OldIrql);
		VIRTUSB_KDPRINT("in VirtUsb_GotWork: *** File gone ***\n");
		return;
	}

	ASSERT(PdoData->ParentFdo);
	fdoData = PdoData->ParentFdo->DeviceExtension;

	InitializeListHead(&completeIrpList);

	while(!IsListEmpty(&file->FetchWorkIrpList) &&
	      (!IsListEmpty(&PdoData->PendingWorkList) ||
	       !IsListEmpty(&PdoData->CancelWorkList) ||
	       VirtUsb_IsPortBitArraySet(PdoData->PortUpdate)))
	{
		listEntry = RemoveHeadList(&file->FetchWorkIrpList);
		nextIrp = CONTAINING_RECORD(listEntry, IRP, Tail.Overlay.ListEntry);
		IoAcquireCancelSpinLock(&cancelIrql);
		cancelRoutine = IoSetCancelRoutine(nextIrp, NULL);
		if(nextIrp->Cancel && !cancelRoutine)
		{
			IoReleaseCancelSpinLock(cancelIrql);
			InitializeListHead(listEntry);
			continue;
		}
		IoReleaseCancelSpinLock(cancelIrql);

		work = (PVIRTUSB_WORK)nextIrp->AssociatedIrp.SystemBuffer;
		irpStack = IoGetCurrentIrpStackLocation(nextIrp);
		ASSERT(work);
		ASSERT(irpStack->Parameters.DeviceIoControl.OutputBufferLength >= sizeof(VIRTUSB_WORK));
		status = VirtUsb_IOC_FetchWork(file, work);
		ASSERT(status != STATUS_PENDING);
		nextIrp->IoStatus.Status = status;
		nextIrp->IoStatus.Information = NT_SUCCESS(status) ? sizeof(VIRTUSB_WORK) : 0;
		InsertTailList(&completeIrpList, listEntry);
	}

	KeReleaseSpinLock(&PdoData->Lock, OldIrql);

	while(!IsListEmpty(&completeIrpList))
	{
		listEntry = RemoveHeadList(&completeIrpList);
		nextIrp = CONTAINING_RECORD(listEntry, IRP, Tail.Overlay.ListEntry);
		IoCompleteRequest(nextIrp, IO_NO_INCREMENT);
		IoReleaseRemoveLock(&fdoData->RemoveLock, nextIrp);
	}
}

VOID
VirtUsb_PortStatUpdateToUser(
	IN PPDO_DEVICE_DATA PdoData,
	IN UCHAR            Index
	)
/*
    caller has lock
    first port is port# 1 (not 0)
*/
{
	ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
	VIRTUSB_KDPRINT("in VirtUsb_PortStatUpdateToUser\n");
	ASSERT(PdoData);
	ASSERT(PdoData->RootHub);
	ASSERT(Index);
	ASSERT(Index <= PdoData->RootHub->PortCount);
	ASSERT(!test_bit(0, PdoData->PortUpdate)); // lowest bit is reserved and always has to be 0
	set_bit(Index, PdoData->PortUpdate);
}

BOOLEAN
VirtUsb_IsPortBitArraySet(
	IN CONST ULONG_PTR *BitArr
	)
/*
    caller has lock
*/
{
	ULONG i;
	ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
	//VIRTUSB_KDPRINT("in VirtUsb_IsPortBitArraySet\n");
	ASSERT(BitArr);
	for(i = 0; i < 256 / (sizeof(ULONG_PTR) * 8); i++)
	{
		if(BitArr[i])
		{
			return TRUE;
		}
	}
	return FALSE;
}

VOID
VirtUsb_CancelFetchWorkIrp(
	__inout PDEVICE_OBJECT DeviceObject,
	__in    PIRP           Irp
	)
{
	PFDO_DEVICE_DATA   fdoData = DeviceObject->DeviceExtension;
	PIO_STACK_LOCATION irpStack = IoGetCurrentIrpStackLocation(Irp);
	PFILE_CONTEXT      file = (PFILE_CONTEXT)irpStack->FileObject->FsContext;
	PPDO_DEVICE_DATA   pdoData = file->HcdPdo->DeviceExtension;

	ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);

	IoReleaseCancelSpinLock(DISPATCH_LEVEL);

	//VIRTUSB_KDPRINT("in VirtUsb_CancelFetchWorkIrp\n");

	KeAcquireSpinLockAtDpcLevel(&pdoData->Lock);
	RemoveEntryList(&Irp->Tail.Overlay.ListEntry);
	KeReleaseSpinLockFromDpcLevel(&pdoData->Lock);
	KeLowerIrql(Irp->CancelIrql);
	Irp->IoStatus.Status = STATUS_CANCELLED;
	Irp->IoStatus.Information = 0;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	IoReleaseRemoveLock(&fdoData->RemoveLock, Irp);
}

VOID
VirtUsb_FailFileIO(
	IN OUT PFILE_CONTEXT File,
	IN     NTSTATUS      FailReason
	)
/*
    Fails all pending irps for <File> with status code <FailReason>

    IRQL = PASSIVE_LEVEL
*/
{
	KIRQL            irql, cancelIrql;
	PFDO_DEVICE_DATA fdoData;
	PPDO_DEVICE_DATA pdoData;
	PIRP             irp;
	PLIST_ENTRY      entry;
	LIST_ENTRY       failIrpList;
	PDRIVER_CANCEL   cancelRoutine;

	ASSERT(KeGetCurrentIrql() < DISPATCH_LEVEL);

	VIRTUSB_KDPRINT("in VirtUsb_FailFileIO\n");

	ASSERT(File);
	ASSERT(!NT_SUCCESS(FailReason));
	ASSERT(File->ParentFdo);
	ASSERT(File->HcdPdo);
	fdoData = File->ParentFdo->DeviceExtension;
	pdoData = File->HcdPdo->DeviceExtension;

	InitializeListHead(&failIrpList);

	KeAcquireSpinLock(&pdoData->Lock, &irql);
	while(!IsListEmpty(&File->FetchWorkIrpList))
	{
		entry = RemoveHeadList(&File->FetchWorkIrpList);
		irp = CONTAINING_RECORD(entry, IRP, Tail.Overlay.ListEntry);
		IoAcquireCancelSpinLock(&cancelIrql);
		cancelRoutine = IoSetCancelRoutine(irp, NULL);
		if(irp->Cancel && !cancelRoutine)
		{
			IoReleaseCancelSpinLock(cancelIrql);
			InitializeListHead(entry);
		}
		else
		{
			IoReleaseCancelSpinLock(cancelIrql);
			InsertTailList(&failIrpList, entry);
		}
	}
	ASSERT(IsListEmpty(&File->FetchWorkIrpList));
	KeReleaseSpinLock(&pdoData->Lock, irql);

	for(entry = failIrpList.Flink;
	    entry != &failIrpList;
	    entry = entry->Flink)
	{
		irp = CONTAINING_RECORD(entry, IRP, Tail.Overlay.ListEntry);
		irp->IoStatus.Status = FailReason;
		irp->IoStatus.Information = 0;
		IoCompleteRequest(irp, IO_NO_INCREMENT);
		IoReleaseRemoveLock(&fdoData->RemoveLock, irp);
	}
}
