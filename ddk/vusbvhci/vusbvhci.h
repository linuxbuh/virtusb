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
#ifdef TARGETING_Win2K
#include <usbdi.h>
#include "../virtusb/w2k.h"
#else
#include <usb.h>
#ifndef USB_OTHER_SPEED_CONFIGURATION_DESCRIPTOR_TYPE
// undefined in winxp headers:
#define USB_OTHER_SPEED_CONFIGURATION_DESCRIPTOR_TYPE 0x07
#endif
#include <hubbusif.h>
#include <usbbusif.h>
#endif
#include <usbioctl.h>

#include "../virtusb/common.h"

// the TYPE_ALIGNMENT macro causes this warning:
#pragma warning(disable:4116)

#if DBG
#define VUSBVHCI_KDPRINT(fmt) DbgPrint("VUSBVHCI.SYS: " fmt)
#define VUSBVHCI_KDPRINT1(fmt, a1) DbgPrint("VUSBVHCI.SYS: " fmt, a1)
#define VUSBVHCI_KDPRINT2(fmt, a1, a2) DbgPrint("VUSBVHCI.SYS: " fmt, a1, a2)
#define VUSBVHCI_KDPRINT3(fmt, a1, a2, a3) DbgPrint("VUSBVHCI.SYS: " fmt, a1, a2, a3)
#define VUSBVHCI_KDPRINT4(fmt, a1, a2, a3, a4) DbgPrint("VUSBVHCI.SYS: " fmt, a1, a2, a3, a4)
#define VUSBVHCI_KDPRINT5(fmt, a1, a2, a3, a4, a5) DbgPrint("VUSBVHCI.SYS: " fmt, a1, a2, a3, a4, a5)
#else
#define VUSBVHCI_KDPRINT(fmt) do {} while(0)
#define VUSBVHCI_KDPRINT1(fmt, a1) do {} while(0)
#define VUSBVHCI_KDPRINT2(fmt, a1, a2) do {} while(0)
#define VUSBVHCI_KDPRINT3(fmt, a1, a2, a3) do {} while(0)
#define VUSBVHCI_KDPRINT4(fmt, a1, a2, a3, a4) do {} while(0)
#define VUSBVHCI_KDPRINT5(fmt, a1, a2, a3, a4, a5) do {} while(0)
#endif

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
	IN OUT PFDO_DEVICE_DATA FdoData
	);

NTSTATUS
VUsbVhci_StartFdo(
	IN OUT PFDO_DEVICE_DATA FdoData
	);

NTSTATUS
VUsbVhci_CreatePdo(
	IN OUT PFDO_DEVICE_DATA FdoData
	);

VOID
VUsbVhci_DestroyPdo(
	IN OUT PPDO_DEVICE_DATA PdoData
	);

NTSTATUS
VUsbVhci_GetVirtUsbInterface(
	IN  PDEVICE_OBJECT                 Pdo,
	OUT PVIRTUSB_BUS_INTERFACE_VHCI_V1 Interface
	);

NTSTATUS
VUsbVhci_GetAndStoreRootHubName(
	IN OUT PPDO_DEVICE_DATA PdoData
	);

NTSTATUS
VUsbVhci_SendIrpSynchronously(
	IN     PDEVICE_OBJECT DeviceObject,
	IN OUT PIRP           Irp
	);

NTSTATUS
VUsbVhci_CompletionRoutine(
	IN PDEVICE_OBJECT DeviceObject,
	IN PIRP           Irp,
	IN PVOID          Context
	);

PUSBHUB_CONTEXT
VUsbVhci_GetRootHubContext(
	IN PFDO_DEVICE_DATA FdoData
	);

PUSB_HUB_DESCRIPTOR
VUsbVhci_GetRootHubHubDescriptor(
	IN PFDO_DEVICE_DATA FdoData
	);

NTSTATUS
VUsbVhci_ReferenceUsbDeviceByHandle(
	IN  PFDO_DEVICE_DATA FdoData,
	IN  PVOID            DeviceHandle,
	OUT PUSBDEV_CONTEXT  *DeviceContext
	);

NTSTATUS
VUsbVhci_ReferenceUsbDevice(
	IN PFDO_DEVICE_DATA FdoData,
	IN PUSBDEV_CONTEXT  DeviceContext
	);

NTSTATUS
VUsbVhci_DereferenceUsbDevice(
	IN PFDO_DEVICE_DATA FdoData,
	IN PUSBDEV_CONTEXT  DeviceContext
	);

NTSTATUS
VUsbVhci_CreateUsbDevice(
	IN     PFDO_DEVICE_DATA FdoData,
	IN OUT PVOID            *DeviceHandle,
	IN     PUSBHUB_CONTEXT  HubDeviceContext,
	IN     USHORT           PortStatus,
	IN     USHORT           PortNumber
	);

NTSTATUS
VUsbVhci_InitializeUsbDevice(
	IN     PFDO_DEVICE_DATA FdoData,
	IN OUT PUSBDEV_CONTEXT  DeviceContext
	);

NTSTATUS
VUsbVhci_RemoveUsbDevice(
	IN     PFDO_DEVICE_DATA FdoData,
	IN OUT PUSBDEV_CONTEXT  DeviceContext,
	IN     ULONG            Flags,
	IN     BOOLEAN          Dereference
	);

// Path to the driver's Services Key in the registry
extern UNICODE_STRING RegPath;

extern CONST WCHAR *CONST RootHubText;
extern CONST WCHAR *CONST RootHubDeviceID;
extern CONST WCHAR *CONST RootHubHardwareIDs;

extern CONST WCHAR *CONST FDONameFormatString;
extern CONST WCHAR *CONST FDOSymlinkFormatString;
extern CONST WCHAR *CONST PDONameFormatString;

#endif // !VUSBVHCI_H
