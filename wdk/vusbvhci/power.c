#include "vusbvhci.h"
#include "trace.h"
#include "power.tmh"
#include "power.h"

NTSTATUS
VUsbVhci_FDO_Power(
	_In_ PFDO_DEVICE_DATA Data,
	_In_ PIRP             Irp
	);

NTSTATUS
VUsbVhci_PDO_Power(
	_In_ PPDO_DEVICE_DATA PdoData,
	_In_ PIRP             Irp
	);

PCHAR
PowerMinorFunctionString(
	_In_ UCHAR MinorFunction
	);

PCHAR
DbgSystemPowerString(
	_In_ SYSTEM_POWER_STATE Type
	);

PCHAR
DbgDevicePowerString(
	_In_ DEVICE_POWER_STATE Type
	);

NTSTATUS
VUsbVhci_DispatchPower(
	_In_ PDEVICE_OBJECT DeviceObject,
	_In_ PIRP           Irp
	)
/*
    Handles power Irps sent to both FDO and child PDO.
    Note: Currently we do not implement full power handling
          for the FDO.
*/
{
	PIO_STACK_LOCATION  irpStack;
	NTSTATUS            status;
	PCOMMON_DEVICE_DATA commonData;

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_POWER, ">%!FUNC!");

	status = STATUS_SUCCESS;
	irpStack = IoGetCurrentIrpStackLocation(Irp);
	ASSERT(IRP_MJ_POWER == irpStack->MajorFunction);

	commonData = (PCOMMON_DEVICE_DATA)DeviceObject->DeviceExtension;

	// If the device has been removed, the driver should
	// not pass the IRP down to the next lower driver.
	if(commonData->DevicePnPState == Deleted)
	{
		PoStartNextPowerIrp(Irp);
		Irp->IoStatus.Status = status = STATUS_NO_SUCH_DEVICE;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		return status;
	}

	if(commonData->IsFDO)
	{
		TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_POWER,
		            "FDO %s IRP: 0x%p %s %s",
		            PowerMinorFunctionString(irpStack->MinorFunction),
		            Irp,
		            DbgSystemPowerString(commonData->SystemPowerState),
		            DbgDevicePowerString(commonData->DevicePowerState));

		status = VUsbVhci_FDO_Power((PFDO_DEVICE_DATA)DeviceObject->DeviceExtension, Irp);
	}
	else
	{
		TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_POWER,
		            "PDO %s IRP: 0x%p %s %s",
		            PowerMinorFunctionString(irpStack->MinorFunction),
		            Irp,
		            DbgSystemPowerString(commonData->SystemPowerState),
		            DbgDevicePowerString(commonData->DevicePowerState));

		status = VUsbVhci_PDO_Power((PPDO_DEVICE_DATA)DeviceObject->DeviceExtension, Irp);
	}

	return status;
}

PCHAR
PowerMinorFunctionString(
	_In_ UCHAR MinorFunction
	)
{
	switch(MinorFunction)
	{
	case IRP_MN_SET_POWER:
		return "IRP_MN_SET_POWER";
	case IRP_MN_QUERY_POWER:
		return "IRP_MN_QUERY_POWER";
	case IRP_MN_POWER_SEQUENCE:
		return "IRP_MN_POWER_SEQUENCE";
	case IRP_MN_WAIT_WAKE:
		return "IRP_MN_WAIT_WAKE";
	default:
		return "unknown_power_irp";
	}
}

PCHAR
DbgSystemPowerString(
	_In_ SYSTEM_POWER_STATE Type
	)
{
	switch(Type)
	{
	case PowerSystemUnspecified:
		return "PowerSystemUnspecified";
	case PowerSystemWorking:
		return "PowerSystemWorking";
	case PowerSystemSleeping1:
		return "PowerSystemSleeping1";
	case PowerSystemSleeping2:
		return "PowerSystemSleeping2";
	case PowerSystemSleeping3:
		return "PowerSystemSleeping3";
	case PowerSystemHibernate:
		return "PowerSystemHibernate";
	case PowerSystemShutdown:
		return "PowerSystemShutdown";
	case PowerSystemMaximum:
		return "PowerSystemMaximum";
	default:
		return "Unknown System Power State";
	}
}

PCHAR
DbgDevicePowerString(
	_In_ DEVICE_POWER_STATE Type
	)
{
	switch(Type)
	{
	case PowerDeviceUnspecified:
		return "PowerDeviceUnspecified";
	case PowerDeviceD0:
		return "PowerDeviceD0";
	case PowerDeviceD1:
		return "PowerDeviceD1";
	case PowerDeviceD2:
		return "PowerDeviceD2";
	case PowerDeviceD3:
		return "PowerDeviceD3";
	case PowerDeviceMaximum:
		return "PowerDeviceMaximum";
	default:
		return "Unknown Device Power State";
	}
}

NTSTATUS
VUsbVhci_FDO_Power(
	_In_ PFDO_DEVICE_DATA FdoData,
	_In_ PIRP             Irp
	)
/*
    Handles power Irps sent to the FDO.
    This driver is power policy owner for the bus itself
    (not the devices on the bus). We will just print some debug outputs and
    forward this Irp to the next level.
*/
{
	NTSTATUS           status;
	POWER_STATE        powerState;
	POWER_STATE_TYPE   powerType;
	PIO_STACK_LOCATION stack;

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_POWER, ">%!FUNC!");

	stack = IoGetCurrentIrpStackLocation(Irp);
	powerType = stack->Parameters.Power.Type;
	powerState = stack->Parameters.Power.State;

	if(!NT_SUCCESS(IoAcquireRemoveLock(&FdoData->RemoveLock, Irp)))
	{
		PoStartNextPowerIrp(Irp);
		IoSkipCurrentIrpStackLocation(Irp);
		return PoCallDriver(FdoData->NextLowerDriver, Irp);
	}

	// If the device is not stated yet, just pass it down.
	if(FdoData->DevicePnPState == NotStarted)
	{
		PoStartNextPowerIrp(Irp);
		IoSkipCurrentIrpStackLocation(Irp);
		status = PoCallDriver(FdoData->NextLowerDriver, Irp);
		IoReleaseRemoveLock(&FdoData->RemoveLock, Irp);
		return status;
	}

	if(stack->MinorFunction == IRP_MN_SET_POWER)
	{
		TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_POWER,
		            "Request to set %s state to %s",
		            ((powerType == SystemPowerState) ?  "System" : "Device"),
		            ((powerType == SystemPowerState) ?
		            DbgSystemPowerString(powerState.SystemState) :
		            DbgDevicePowerString(powerState.DeviceState)));
	}

	PoStartNextPowerIrp(Irp);
	IoSkipCurrentIrpStackLocation(Irp);
	status = PoCallDriver(FdoData->NextLowerDriver, Irp);
	IoReleaseRemoveLock(&FdoData->RemoveLock, Irp);
	return status;
}

NTSTATUS
VUsbVhci_PDO_Power(
	_In_ PPDO_DEVICE_DATA PdoData,
	_In_ PIRP             Irp
	)
/*
    Handles power Irps sent to the PDO.
    Typically a bus driver, that is not a power
    policy owner for the device, does nothing
    more than starting the next power IRP and
    completing this one.
*/

{
	NTSTATUS           status;
	PIO_STACK_LOCATION stack;
	POWER_STATE        powerState;
	POWER_STATE_TYPE   powerType;

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_POWER, ">%!FUNC!");

	if(!NT_SUCCESS(IoAcquireRemoveLock(&PdoData->RemoveLock, Irp)))
	{
		PoStartNextPowerIrp(Irp);
		Irp->IoStatus.Status = status = STATUS_NO_SUCH_DEVICE;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		return status;
	}

	stack = IoGetCurrentIrpStackLocation(Irp);
	powerType = stack->Parameters.Power.Type;
	powerState = stack->Parameters.Power.State;

	switch(stack->MinorFunction)
	{
	case IRP_MN_SET_POWER:
		TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_POWER,
		            "Setting %s power state to %s",
		            ((powerType == SystemPowerState) ?  "System" : "Device"),
		            ((powerType == SystemPowerState) ?
		            DbgSystemPowerString(powerState.SystemState) :
		            DbgDevicePowerString(powerState.DeviceState)));
		switch(powerType)
		{
		case DevicePowerState:
			PoSetPowerState(PdoData->Self, powerType, powerState);
			PdoData->DevicePowerState = powerState.DeviceState;
			//if(PdoData->ParentFile)
			//{
				// TODO: inform user-mode about power state and wait
				//       here until user-mode gets this information by
				//       doing a FETCH_WORK ioctl.
			//}
			status = STATUS_SUCCESS;
			break;

		case SystemPowerState:
			PdoData->SystemPowerState = powerState.SystemState;
			status = STATUS_SUCCESS;
			break;

		default:
			status = STATUS_NOT_SUPPORTED;
			break;
		}
		break;

	case IRP_MN_QUERY_POWER:
		status = STATUS_SUCCESS;
		break;

	case IRP_MN_WAIT_WAKE:
		// We cannot support wait-wake because our parent, the VHCI enumerator,
		// doesn't support wait-wake.
		// If you are a bus enumerated device, and if your parent bus supports
		// wait-wake, you should send a wait/wake IRP (PoRequestPowerIrp)
		// in response to this request.
		// If you want to test the wait/wake logic implemented in the function
		// driver, you could do the following simulation:
		// a) Mark this IRP pending.
		// b) Set a cancel routine.
		// c) Save this IRP in the device extension
		// d) Return STATUS_PENDING.
		// Later on if you suspend and resume your system, your BUS_FDO_POWER
		// will be called to power the bus. In response to IRP_MN_SET_POWER, if the
		// powerstate is PowerSystemWorking, complete this Wake IRP.
		// If the function driver, decides to cancel the wake IRP, your cancel routine
		// will be called. There you just complete the IRP with STATUS_CANCELLED.
	case IRP_MN_POWER_SEQUENCE:
	default:
		status = STATUS_NOT_SUPPORTED;
		break;
	}

	if(status != STATUS_NOT_SUPPORTED)
	{
		Irp->IoStatus.Status = status;
	}

	PoStartNextPowerIrp(Irp);
	status = Irp->IoStatus.Status;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	IoReleaseRemoveLock(&PdoData->RemoveLock, Irp);
	return status;
}
