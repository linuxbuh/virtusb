#ifndef VUSBVHCI_H
#define VUSBVHCI_H

#define NTSTRSAFE_LIB
#include <ntddk.h>
#include <ntstrsafe.h>
#include <wdmsec.h> // required for IoCreateDeviceSecure
#include <wmilib.h> // required for WMILIB_CONTEXT
#include <wmistr.h> // required for WMIREG_FLAG_INSTANCE_PDO
#include <initguid.h> // required for GUID definitions
#include <devguid.h> // required for GUID_DEVCLASS_USB
#include <usb.h>
#include <hubbusif.h>
#include <usbbusif.h>
#include <usbioctl.h>

#include "../virtusb/common.h"

// the TYPE_ALIGNMENT macro causes this warning:
#pragma warning(disable:4116)

#define MOFRESOURCENAME L"VUsbVhciWMI"


// *** POOL-TAGS used for ExAllocatePoolWithTag and ExFreePoolWithTag ***

// Default tag for the everyday usage (for trivial things that can't get messed up)
#define VUSBVHCI_POOL_TAG           ((ULONG)'ichV' | PROTECTED_POOL)  // Vhci

// Buffers which are freed by another driver
#define VUSBVHCI_NOT_OWNER_POOL_TAG ((ULONG)'XchV')                   // VhcX


typedef enum _DEVICE_PNP_STATE
{
	NotStarted            = 0x00, // Not started yet
	Started               = 0x01, // Device has received the START_DEVICE IRP
	StopPending           = 0x12, // Device has received the QUERY_STOP IRP
	Stopped               = 0x10, // Device has received the STOP_DEVICE IRP
	RemovePending         = 0x24, // Device has received the QUERY_REMOVE IRP
	SurpriseRemovePending = 0x28, // Device has received the SURPRISE_REMOVE IRP
	Deleted               = 0x20, // Device has received the REMOVE_DEVICE IRP
	Unknown               = 0x3e  // Unknown state
} DEVICE_PNP_STATE;

//
// The QUEUE_STATE enumeration defines the different states the driver-managed IRP
// queue can be set to.
// The function driver initializes the queue state to HoldRequests when
// VUsbVhci_AddDevice initializes the FDO.
// The function driver sets the queue state to HoldRequests when the hardware
// instance is not yet started, temporarily stopped for resource rebalancing, or
// entering a sleep state.
// The function driver sets the queue state to AllowRequests when the hardware
// instance is active and ready to processing new or pending read/write/device control
// IRPs.
// The function driver sets the queue state to FailRequests when the hardware
// instance is no longer present.
typedef enum _QUEUE_STATE
{
	HoldRequests = 0,
	AllowRequests,
	FailRequests
} QUEUE_STATE;

// A common header for the device-extensions of the PDOs and FDO
typedef struct _COMMON_DEVICE_DATA
{
	// A back-pointer to the device-object for which this is the extension
	PDEVICE_OBJECT     Self;

	// This flag helps distinguish between PDO and FDO
	BOOLEAN            IsFDO;

	// We track the state of the device with every PnP-Irp
	// that affects the device through these two variables.
	DEVICE_PNP_STATE   DevicePnPState, PreviousPnPState;

	// Stores the current system-power-state
	SYSTEM_POWER_STATE SystemPowerState;

	// Stores current device-power-state
	DEVICE_POWER_STATE DevicePowerState;

	// Reference tracking stuff for device removal
	IO_REMOVE_LOCK     RemoveLock;
} COMMON_DEVICE_DATA, *PCOMMON_DEVICE_DATA;

// The device extension of the bus itself. From whence the PDO's are born.
typedef struct _FDO_DEVICE_DATA
{
	COMMON_DEVICE_DATA;

	// The underlying bus PDO and the actual device object to which our
	// FDO is attached
	PDEVICE_OBJECT                UnderlyingPDO;
	PDEVICE_OBJECT                NextLowerDriver;

	// Number from which the "\Device\HCD%lu" symlink was created.
	// This is needed for cleaning up the symlink. (see AddDevice routine)
	ULONG                         UsbDeviceNumber;
	BOOLEAN                       SymlinkFailed; // TRUE, if symlink creation failed -- so we don't need to clean it up.

	// Root-Hub-PDO
	PDEVICE_OBJECT                RHubPdo;

	// The name returned from IoRegisterDeviceInterface,
	// which is used as a handle for IoSetDeviceInterfaceState.
	UNICODE_STRING                InterfaceName;

	// WMI Information
	WMILIB_CONTEXT                WmiLibInfo;

	VIRTUSB_BUS_INTERFACE_VHCI_V1 VirtUsbInterface;
} FDO_DEVICE_DATA, *PFDO_DEVICE_DATA;

// The device-extension for the PDOs.
// That's of the virtual root-hub which this vhci-driver creates.
typedef struct _PDO_DEVICE_DATA
{
	COMMON_DEVICE_DATA;

	// A back-pointer to the host controller
	PDEVICE_OBJECT     ParentFdo;

	FAST_MUTEX         Mutex;

	// Used to track the intefaces handed out to other drivers.
	ULONG              InterfaceRefCount;

	// The name returned from IoRegisterDeviceInterface,
	// which is used as a handle for IoSetDeviceInterfaceState.
	// This is the name of the symlink to the root-hub pdo, which
	// others ask for.
	UNICODE_STRING     InterfaceName;

	BOOLEAN            Present;
	BOOLEAN            ReportedMissing;
} PDO_DEVICE_DATA, *PPDO_DEVICE_DATA;

#define FDO_FROM_PDO(pdoData) \
        ((PFDO_DEVICE_DATA)(pdoData)->ParentFdo->DeviceExtension)

#define PDO_FROM_FDO(fdoData) \
        ((PPDO_DEVICE_DATA)(fdoData)->RHubPdo->DeviceExtension)

#define INITIALIZE_PNP_STATE(_Data_) \
        (_Data_)->DevicePnPState = NotStarted; \
        (_Data_)->PreviousPnPState = NotStarted;

#define SET_NEW_PNP_STATE(_Data_, _state_) \
        (_Data_)->PreviousPnPState = (_Data_)->DevicePnPState; \
        (_Data_)->DevicePnPState = (_state_);

#define RESTORE_PREVIOUS_PNP_STATE(_Data_) \
        (_Data_)->DevicePnPState = (_Data_)->PreviousPnPState;

VOID
VUsbVhci_FdoInterfaceDown(
	_Inout_ PFDO_DEVICE_DATA FdoData
	);

NTSTATUS
VUsbVhci_StartFdo(
	_Inout_ PFDO_DEVICE_DATA FdoData
	);

NTSTATUS
VUsbVhci_CreatePdo(
	_Inout_ PFDO_DEVICE_DATA FdoData
	);

VOID
VUsbVhci_DestroyPdo(
	_Inout_ PPDO_DEVICE_DATA PdoData
	);

NTSTATUS
VUsbVhci_GetVirtUsbInterface(
	_In_  PDEVICE_OBJECT                 Pdo,
	_Out_ PVIRTUSB_BUS_INTERFACE_VHCI_V1 Interface
	);

NTSTATUS
VUsbVhci_GetAndStoreRootHubName(
	_Inout_ PPDO_DEVICE_DATA PdoData
	);

NTSTATUS
VUsbVhci_SendIrpSynchronously(
	_In_    PDEVICE_OBJECT DeviceObject,
	_Inout_ PIRP           Irp
	);

NTSTATUS
VUsbVhci_CompletionRoutine(
	_In_ PDEVICE_OBJECT DeviceObject,
	_In_ PIRP           Irp,
	_In_ PVOID          Context
	);

PUSBHUB_CONTEXT
VUsbVhci_GetRootHubContext(
	_In_ PFDO_DEVICE_DATA FdoData
	);

PUSB_HUB_DESCRIPTOR
VUsbVhci_GetRootHubHubDescriptor(
	_In_ PFDO_DEVICE_DATA FdoData
	);

NTSTATUS
VUsbVhci_ReferenceUsbDeviceByHandle(
	_In_  PFDO_DEVICE_DATA FdoData,
	_In_  PVOID            DeviceHandle,
	_Out_ PUSBDEV_CONTEXT  *DeviceContext
	);

NTSTATUS
VUsbVhci_ReferenceUsbDevice(
	_In_ PFDO_DEVICE_DATA FdoData,
	_In_ PUSBDEV_CONTEXT  DeviceContext
	);

NTSTATUS
VUsbVhci_DereferenceUsbDevice(
	_In_ PFDO_DEVICE_DATA FdoData,
	_In_ PUSBDEV_CONTEXT  DeviceContext
	);

NTSTATUS
VUsbVhci_CreateUsbDevice(
	_In_    PFDO_DEVICE_DATA FdoData,
	_Inout_ PVOID            *DeviceHandle,
	_In_    PUSBHUB_CONTEXT  HubDeviceContext,
	_In_    USHORT           PortStatus,
	_In_    USHORT           PortNumber
	);

NTSTATUS
VUsbVhci_InitializeUsbDevice(
	_In_    PFDO_DEVICE_DATA FdoData,
	_Inout_ PUSBDEV_CONTEXT  DeviceContext
	);

NTSTATUS
VUsbVhci_RemoveUsbDevice(
	_In_    PFDO_DEVICE_DATA FdoData,
	_Inout_ PUSBDEV_CONTEXT  DeviceContext,
	_In_    ULONG            Flags,
	_In_    BOOLEAN          Dereference
	);

// Path to the driver's Services Key in the registry
extern UNICODE_STRING VUsbVhci_RegPath;

extern CONST WCHAR *CONST RootHubText;
extern CONST WCHAR *CONST RootHubDeviceID;
extern CONST WCHAR *CONST RootHubHardwareIDs;

extern CONST WCHAR *CONST FDONameFormatString;
extern CONST WCHAR *CONST FDOSymlinkFormatString;
extern CONST WCHAR *CONST PDONameFormatString;

#endif // !VUSBVHCI_H
