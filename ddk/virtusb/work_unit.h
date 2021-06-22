#ifndef VIRTUSB_WORK_UNIT_H
#define VIRTUSB_WORK_UNIT_H

#include "virtusb.h"

typedef struct _WORK        WORK, *PWORK;
typedef struct _WORK_SUBURB WORK_SUBURB, *PWORK_SUBURB;

typedef LONG
	(*PWORK_SUBURB_COMPLETION_HANDLER)(
		IN OUT PWORK,
		IN OUT PWORK_SUBURB,
		IN     USBD_STATUS
		);

typedef NTSTATUS
	(*PWORK_COMPLETION_HANDLER)(
		IN OUT PWORK,
		IN     NTSTATUS,
		IN     BOOLEAN
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
	IN     PPDO_DEVICE_DATA PdoData,
	IN OUT PIRP             Irp,
	IN     PPIPE_CONTEXT    Pipe
	);

PWORK
VirtUsb_AllocateMultipleUrbsWorkUnit(
	IN     PPDO_DEVICE_DATA PdoData,
	IN OUT PIRP             Irp,
	IN     PPIPE_CONTEXT    Pipe,
	IN     ULONG            SubUrbCount
	);

VOID
VirtUsb_FreeWorkUnit(
	IN OUT PWORK WorkUnit
	);

BOOLEAN
VirtUsb_CompleteWorkUnitCurrentUrb(
	IN OUT PWORK       WorkUnit,
	IN     USBD_STATUS UsbdStatus
	);

NTSTATUS
VirtUsb_CompleteWorkUnit(
	IN OUT PWORK    WorkUnit,
	IN     NTSTATUS NtStatus
	);

VOID
VirtUsb_HardCancelWorkUnit(
	IN OUT PWORK WorkUnit
	);

PURB
VirtUsb_GetCurrentWorkUnitUrb(
	IN CONST WORK *WorkUnit
	);

WORK_TYPE
FORCEINLINE
VirtUsb_GetWorkUnitType(
	IN CONST WORK *WorkUnit
	)
{
	ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);
	ASSERT(WorkUnit);
	return WorkUnit->SubUrbCount ? MultipleUrbs : SimpleUrb;
}

#endif // !VIRTUSB_WORK_UNIT_H
