#ifndef VIRTUSB_WORK_UNIT_H
#define VIRTUSB_WORK_UNIT_H

#include "virtusb.h"

typedef struct _WORK        WORK, *PWORK;
typedef struct _WORK_SUBURB WORK_SUBURB, *PWORK_SUBURB;

typedef LONG
	(*PWORK_SUBURB_COMPLETION_HANDLER)(
		_Inout_ PWORK,
		_Inout_ PWORK_SUBURB,
		_In_    USBD_STATUS
		);

typedef NTSTATUS
	(*PWORK_COMPLETION_HANDLER)(
		_Inout_ PWORK,
		_In_    NTSTATUS,
		_In_    BOOLEAN
		);

typedef enum _WORK_TYPE
{
	SimpleUrb = 0,
	MultipleUrbs
} WORK_TYPE;

struct _WORK_SUBURB
{
	PURB                            Urb;
	PWORK_SUBURB_COMPLETION_HANDLER CompletionHandler;
	PVOID                           Context[2];
};

struct _WORK
{
	UINT64                   Handle;
	LIST_ENTRY               ListEntry;
	PPDO_DEVICE_DATA         PdoData;
	PPIPE_CONTEXT            Pipe;
	PIRP                     Irp;
	PKEVENT                  CancelEvent;
	BOOLEAN                  CancelRace;
	LONG                     CurrentSubUrb;
	ULONG                    SubUrbCount;
#if DBG
	// used to track proper usage of CompleteWorkUnit/HardCancelWorkUnit & FreeWorkUnit routines
	BOOLEAN                  Completed;
#endif

	// only if created with VirtUsb_CreateMultipleUrbsWorkUnit:
	PWORK_COMPLETION_HANDLER CompletionHandler;
	PVOID                    Context[2];
	WORK_SUBURB              SubUrb[1];
};
#define URB_WORK_SIZE(subUrbCount) \
        ((subUrbCount) ? FIELD_OFFSET(WORK, SubUrb[subUrbCount]) \
                       : FIELD_OFFSET(WORK, CompletionHandler))

#define WORK_FROM_IRP(irp) \
        ((PWORK)(irp)->Tail.Overlay.DriverContext[0])

PWORK
VirtUsb_AllocateSimpleUrbWorkUnit(
	_In_    PPDO_DEVICE_DATA PdoData,
	_Inout_ PIRP             Irp,
	_In_    PPIPE_CONTEXT    Pipe
	);

PWORK
VirtUsb_AllocateMultipleUrbsWorkUnit(
	_In_    PPDO_DEVICE_DATA PdoData,
	_Inout_ PIRP             Irp,
	_In_    PPIPE_CONTEXT    Pipe,
	_In_    ULONG            SubUrbCount
	);

VOID
VirtUsb_FreeWorkUnit(
	_Inout_ PWORK WorkUnit
	);

BOOLEAN
VirtUsb_CompleteWorkUnitCurrentUrb(
	_Inout_ PWORK       WorkUnit,
	_In_    USBD_STATUS UsbdStatus
	);

NTSTATUS
VirtUsb_CompleteWorkUnit(
	_Inout_ PWORK    WorkUnit,
	_In_    NTSTATUS NtStatus
	);

VOID
VirtUsb_HardCancelWorkUnit(
	_Inout_ PWORK WorkUnit
	);

PURB
VirtUsb_GetCurrentWorkUnitUrb(
	_In_ CONST WORK *WorkUnit
	);

WORK_TYPE
FORCEINLINE
VirtUsb_GetWorkUnitType(
	_In_ CONST WORK *WorkUnit
	)
{
	ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);
	ASSERT(WorkUnit);
	return WorkUnit->SubUrbCount ? MultipleUrbs : SimpleUrb;
}

#endif // !VIRTUSB_WORK_UNIT_H
