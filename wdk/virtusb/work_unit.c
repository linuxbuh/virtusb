#include "virtusb.h"
#include "trace.h"
#include "work_unit.tmh"
#include "work_unit.h"

PWORK
VirtUsb_AllocateSimpleUrbWorkUnit(
	_In_    PPDO_DEVICE_DATA PdoData,
	_Inout_ PIRP             Irp,
	_In_    PPIPE_CONTEXT    Pipe
	)
{
	PURB  urb;
	PWORK wu;

	ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_WORK_UNIT, ">%!FUNC!");

	ASSERT(PdoData);
	ASSERT(Irp);
	ASSERT(Pipe);
	urb = URB_FROM_IRP(Irp);
	ASSERT(urb);

	wu = ExAllocatePoolWithTag(NonPagedPool, URB_WORK_SIZE(0), VIRTUSB_WORK_POOL_TAG);
	if(!wu)
	{
		return NULL;
	}
	RtlZeroMemory(wu, URB_WORK_SIZE(0));
	wu->Handle = (UINT64)(ULONG_PTR)wu;
	wu->PdoData = PdoData;
	wu->Pipe = Pipe;
	wu->Irp = Irp;
	Irp->Tail.Overlay.DriverContext[0] = wu;
	return wu;
}

PWORK
VirtUsb_AllocateMultipleUrbsWorkUnit(
	_In_    PPDO_DEVICE_DATA PdoData,
	_Inout_ PIRP             Irp,
	_In_    PPIPE_CONTEXT    Pipe,
	_In_    ULONG            SubUrbCount
	)
{
	PURB  urb;
	PWORK wu;

	ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_WORK_UNIT, ">%!FUNC!");

	ASSERT(PdoData);
	ASSERT(Irp);
	ASSERT(Pipe);
	ASSERT(SubUrbCount);
	urb = URB_FROM_IRP(Irp);
	ASSERT(urb);

	wu = ExAllocatePoolWithTag(NonPagedPool, URB_WORK_SIZE(SubUrbCount), VIRTUSB_WORK_POOL_TAG);
	if(!wu)
	{
		return NULL;
	}
	RtlZeroMemory(wu, URB_WORK_SIZE(SubUrbCount));
	wu->Handle = (UINT64)(ULONG_PTR)wu;
	wu->PdoData = PdoData;
	wu->Pipe = Pipe;
	wu->Irp = Irp;
	wu->SubUrbCount = SubUrbCount;
	Irp->Tail.Overlay.DriverContext[0] = wu;
	return wu;
}

VOID
VirtUsb_FreeWorkUnit(
	_Inout_ PWORK WorkUnit
	)
{
	ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_WORK_UNIT, ">%!FUNC!");

	ASSERT(WorkUnit);

#if DBG
	ASSERT(WorkUnit->Completed);
	WorkUnit->Handle          = 0xdeadf00dbeefbabe;
	WorkUnit->ListEntry.Flink = (PVOID)(ULONG_PTR)0xdeadf00d;
	WorkUnit->ListEntry.Blink = (PVOID)(ULONG_PTR)0xdeadf00d;
	WorkUnit->PdoData         = (PVOID)(ULONG_PTR)0xdeadf00d;
	WorkUnit->Pipe            = (PVOID)(ULONG_PTR)0xdeadf00d;
	WorkUnit->Irp             = (PVOID)(ULONG_PTR)0xdeadf00d;
	WorkUnit->CancelEvent     = (PVOID)(ULONG_PTR)0xdeadf00d;
	WorkUnit->CurrentSubUrb   = 0xdeadf00d;
	if(WorkUnit->SubUrbCount)
	{
		ULONG i;
		WorkUnit->CompletionHandler = (PWORK_COMPLETION_HANDLER)(ULONG_PTR)0xdeadf00d;
		WorkUnit->Context[0]        = (PVOID)(ULONG_PTR)0xdeadf00d;
		WorkUnit->Context[1]        = (PVOID)(ULONG_PTR)0xdeadf00d;
		for(i = 0; i < WorkUnit->SubUrbCount; i++)
		{
			WorkUnit->SubUrb[i].Urb               = (PVOID)(ULONG_PTR)0xdeadf00d;
			WorkUnit->SubUrb[i].CompletionHandler = (PWORK_SUBURB_COMPLETION_HANDLER)(ULONG_PTR)0xdeadf00d;
			WorkUnit->SubUrb[i].Context[0]        = (PVOID)(ULONG_PTR)0xdeadf00d;
			WorkUnit->SubUrb[i].Context[1]        = (PVOID)(ULONG_PTR)0xdeadf00d;
		}
	}
#endif
	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_WORK_UNIT, "free wu 0x%p", WorkUnit);

	ExFreePoolWithTag(WorkUnit, VIRTUSB_WORK_POOL_TAG);
}

BOOLEAN
VirtUsb_CompleteWorkUnitCurrentUrb(
	_Inout_ PWORK       WorkUnit,
	_In_    USBD_STATUS UsbdStatus
	)
{
	PURB urb;

	ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_WORK_UNIT, ">%!FUNC!");

	ASSERT(WorkUnit);
	ASSERT(WorkUnit->Irp);

	if(VirtUsb_GetWorkUnitType(WorkUnit) == MultipleUrbs)
	{
		PWORK_SUBURB subUrb = &WorkUnit->SubUrb[WorkUnit->CurrentSubUrb];
		LONG         next;
		BOOLEAN      hasMoreUrbs;
		if(subUrb->CompletionHandler)
		{
			// sub-urb-completion-handler tells us the index of the next sub-urb,
			// which should be processed by user-mode.
			next = subUrb->CompletionHandler(WorkUnit, subUrb, UsbdStatus);
			ASSERT(next >= -1);
			ASSERT((ULONG)(next + 1) <= WorkUnit->SubUrbCount);
		}
		else
		{
			// If there is no sub-urb-completion-handler, which could tell us the
			// index of the next sub-urb, then just increment it.
			next = WorkUnit->CurrentSubUrb + 1;
			// If this sub-urb completes with an error status or it is the last one
			// in the array, then no sub-urb follows.
			if(!USBD_SUCCESS(UsbdStatus) || (ULONG)next >= WorkUnit->SubUrbCount)
				next = -1;
			subUrb->Urb->UrbHeader.Status = UsbdStatus;
		}
		hasMoreUrbs = next != -1;
		if(!hasMoreUrbs)
		{
			// If this is the last sub-urb, then set the status of urb.
			// This value can be overwritten in the work-unit-completion-handler.
			urb = URB_FROM_IRP(WorkUnit->Irp);
			ASSERT(urb);
			urb->UrbHeader.Status = subUrb->Urb->UrbHeader.Status;
		}
		WorkUnit->CurrentSubUrb = next;
		return hasMoreUrbs;
	}

	urb = VirtUsb_GetCurrentWorkUnitUrb(WorkUnit);
	ASSERT(urb);
	urb->UrbHeader.Status = UsbdStatus;
	WorkUnit->CurrentSubUrb = -1;
	return FALSE;
}

NTSTATUS
VirtUsb_CompleteWorkUnit(
	_Inout_ PWORK    WorkUnit,
	_In_    NTSTATUS NtStatus
	)
{
	ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_WORK_UNIT, ">%!FUNC!");

	ASSERT(WorkUnit);
	ASSERT(WorkUnit->Irp);
#if DBG
	ASSERT(!WorkUnit->Completed);
#endif

	if(VirtUsb_GetWorkUnitType(WorkUnit) == MultipleUrbs)
	{
		if(WorkUnit->CompletionHandler) NtStatus =
			WorkUnit->CompletionHandler(WorkUnit, NtStatus, FALSE);
#if DBG
		WorkUnit->CompletionHandler = (PWORK_COMPLETION_HANDLER)(ULONG_PTR)0xdeadf00d;
#endif
	}
	WorkUnit->Irp->IoStatus.Status = NtStatus;
	if(NtStatus == STATUS_CANCELLED || NtStatus == STATUS_NO_SUCH_DEVICE)
		WorkUnit->Irp->IoStatus.Information = 0;
	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_WORK_UNIT,
	            "*** WORK completion summary: handle=0x%016llx ntStatus=0x%08lx usbdStatus=0x%08lx",
	            WorkUnit->Handle, NtStatus, ((PURB)URB_FROM_IRP(WorkUnit->Irp))->UrbHeader.Status);
#if DBG
	WorkUnit->Completed = TRUE;
#endif
	return NtStatus;
}

VOID
VirtUsb_HardCancelWorkUnit(
	_Inout_ PWORK WorkUnit
	)
/*
	IRQL = DISPATCH_LEVEL
*/
{
	ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_WORK_UNIT, ">%!FUNC!");
	ASSERT(WorkUnit);
#if DBG
	ASSERT(!WorkUnit->Completed);
#endif
	if(VirtUsb_GetWorkUnitType(WorkUnit) == MultipleUrbs)
	{
		NTSTATUS status = STATUS_CANCELLED;
		if(WorkUnit->CompletionHandler) status =
			WorkUnit->CompletionHandler(WorkUnit, status, TRUE);
		ASSERT(status == STATUS_CANCELLED);
#if DBG
		WorkUnit->CompletionHandler = (PWORK_COMPLETION_HANDLER)(ULONG_PTR)0xdeadf00d;
#endif
	}
	WorkUnit->Irp = NULL;
#if DBG
	WorkUnit->CurrentSubUrb = -1;
	WorkUnit->Completed = TRUE;
#endif
}

PURB
VirtUsb_GetCurrentWorkUnitUrb(
	_In_ CONST WORK *WorkUnit
	)
{
	ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);
	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_WORK_UNIT, ">%!FUNC!");
	ASSERT(WorkUnit);
	ASSERT(WorkUnit->Irp);
	if(VirtUsb_GetWorkUnitType(WorkUnit) == MultipleUrbs)
	{
		ASSERT(WorkUnit->CurrentSubUrb >= 0);
		ASSERT((ULONG)WorkUnit->CurrentSubUrb < WorkUnit->SubUrbCount);
		return WorkUnit->SubUrb[WorkUnit->CurrentSubUrb].Urb;
	}
	ASSERT(!WorkUnit->CurrentSubUrb);
	return URB_FROM_IRP(WorkUnit->Irp);
}
