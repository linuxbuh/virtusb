#ifndef VIRTUSB_H
#define VIRTUSB_H

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

#include "virtusb_ioctl.h"
#include "common.h"

// the TYPE_ALIGNMENT macro causes this warning:
#pragma warning(disable:4116)

#define MOFRESOURCENAME L"VirtUsbWMI"


// *** POOL-TAGS used for ExAllocatePoolWithTag and ExFreePoolWithTag ***

// Default tag for the everyday usage (for trivial things that can't get messed up)
#define VIRTUSB_POOL_TAG           ((ULONG)'bsUV' | PROTECTED_POOL)  // VUsb

// Buffers which are freed by another driver
#define VIRTUSB_NOT_OWNER_POOL_TAG ((ULONG)'XsUV')                   // VUsX

// File contexts
#define VIRTUSB_FILE_POOL_TAG      ((ULONG)'FsUV' | PROTECTED_POOL)  // VUsF

// USB device contexts and their sub contexts (config, ifc, ...)
#define VIRTUSB_DEVICE_POOL_TAG    ((ULONG)'DsUV' | PROTECTED_POOL)  // VUsD

// Work units
#define VIRTUSB_WORK_POOL_TAG      ((ULONG)'WsUV' | PROTECTED_POOL)  // VUsW


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

typedef enum _FILE_IFC_STATE
{
	NotRegistered = 0,            // File opened; IOCREGISTER not yet called
	Registered,                   // IOCREGISTER called and Pdo created
	Closing                       // File is closing; waiting for pending user-mode-ioctl-irps to fail
} FILE_IFC_STATE;

// Pre-declaration
typedef struct _FILE_CONTEXT *PFILE_CONTEXT;

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
	PDEVICE_OBJECT     UnderlyingPDO;
	PDEVICE_OBJECT     NextLowerDriver;

	// List of PDOs created so far
	LIST_ENTRY         ListOfPdos;

	// List of Files created so far
	LIST_ENTRY         ListOfFiles;

	// For synchronizing access to Pdo- and File-lists
	FAST_MUTEX         Mutex;

	// The name returned from IoRegisterDeviceInterface,
	// which is used as a handle for IoSetDeviceInterfaceState.
	UNICODE_STRING     InterfaceName;

	// WMI Information
	WMILIB_CONTEXT     WmiLibInfo;
} FDO_DEVICE_DATA, *PFDO_DEVICE_DATA;

// The device-extension for the PDOs.
// That's of the host controller which this bus-driver enumerates.
typedef struct _PDO_DEVICE_DATA
{
	COMMON_DEVICE_DATA;

	// A back-pointer to the bus
	PDEVICE_OBJECT     ParentFdo;

	// A back-pointer to the file context
	PFILE_CONTEXT      ParentFile;

	// For PDO-list in FDO_DEVICE_DATA
	LIST_ENTRY         Link;

	// Instance-ID
	WCHAR              InstanceID[16];

	// Present is set to TRUE when the PDO is created,
	// and set to FALSE when the user-mode wants it to be plugged out.
	// We will delete the PDO in IRP_MN_REMOVE only after we have reported
	// to the PnP-manager that it's missing.
	BOOLEAN            Present;
	BOOLEAN            ReportedMissing;

	// work units which are waiting to get fetched by user-mode are in this list
	LIST_ENTRY         PendingWorkList;

	// work units which were fetched by user-mode but not already given back are in this list
	LIST_ENTRY         FetchedWorkList;

	// work units which were fetched by user-mode and not already given back, and which should be
	// canceled are in this list
	LIST_ENTRY         CancelWorkList;

	// work units which were fetched by user-mode and not already given back, and for which the
	// user-mode already knows about the cancelation state are in this list
	LIST_ENTRY         CancelingWorkList;

	// For scheduling port stat updates
	UCHAR              PortSchedOffset;

	// 256 bits, indicating the need for a user-mode port stat update
	ULONG_PTR          PortUpdate[256 / (sizeof(ULONG_PTR) * 8)];

	// Notification requests from the root-hubs interrupt endpoint
	LIST_ENTRY         RootHubRequests;
	PIO_WORKITEM       RootHubNotifyWorkItem;

	// For synchronizing access to URB-lists and PortUpdate-array.
	KSPIN_LOCK         Lock;

	// Each VHCD gets its unique id assigned on creation
	ULONG              Id;

	// Root-hub
	PUSBHUB_CONTEXT    RootHub;

	// Used to track the interfaces handed out to other drivers.
	ULONG              InterfaceRefCount;
} PDO_DEVICE_DATA, *PPDO_DEVICE_DATA;

typedef struct _FILE_CONTEXT
{
	// A back-pointer to the bus
	PDEVICE_OBJECT     ParentFdo;

	// For PDO-list in FDO_DEVICE_DATA
	LIST_ENTRY         Link;

	// A back pointer to the file-object for which this is the FsContext
	PFILE_OBJECT       FileObj;

	// HCD Pdo
	PDEVICE_OBJECT     HcdPdo;

	// Queues IOCFETCHWORK irps
	LIST_ENTRY         FetchWorkIrpList;

#ifdef _WIN64
	// TRUE, if user-mode process is 32-bit
	BOOLEAN            User32;
#endif

	FILE_IFC_STATE     State;

	// Version of the IOCTL-interface the user-mode process is using
	UCHAR              IocVersion;

	UCHAR              PortCount;
} FILE_CONTEXT, *PFILE_CONTEXT;

#define FDO_FROM_PDO(pdoData) \
        ((PFDO_DEVICE_DATA)(pdoData)->ParentFdo->DeviceExtension)

#define INITIALIZE_PNP_STATE(_Data_) \
        (_Data_)->DevicePnPState = NotStarted; \
        (_Data_)->PreviousPnPState = NotStarted;

#define SET_NEW_PNP_STATE(_Data_, _state_) \
        (_Data_)->PreviousPnPState = (_Data_)->DevicePnPState; \
        (_Data_)->DevicePnPState = (_state_);

#define RESTORE_PREVIOUS_PNP_STATE(_Data_) \
        (_Data_)->DevicePnPState = (_Data_)->PreviousPnPState;

VOID
VirtUsb_FdoInterfaceDown(
	_Inout_ PFDO_DEVICE_DATA FdoData
	);

NTSTATUS
VirtUsb_StartFdo(
	_Inout_ PFDO_DEVICE_DATA FdoData
	);

NTSTATUS
VirtUsb_CreatePdo(
	_Inout_ PFILE_CONTEXT File
	);

NTSTATUS
VirtUsb_InitializePdo(
	_Inout_ PPDO_DEVICE_DATA PdoData
	);

VOID
VirtUsb_DestroyPdo(
	_Inout_ PPDO_DEVICE_DATA PdoData
	);

NTSTATUS
VirtUsb_SendIrpSynchronously(
	_In_    PDEVICE_OBJECT DeviceObject,
	_Inout_ PIRP           Irp
	);

NTSTATUS
VirtUsb_CompletionRoutine(
	_In_ PDEVICE_OBJECT DeviceObject,
	_In_ PIRP           Irp,
	_In_ PVOID          Context
	);

// Path to the driver's Services Key in the registry
extern UNICODE_STRING VirtUsb_RegPath;

extern CONST WCHAR *CONST HcdText;
extern CONST WCHAR *CONST HcdDeviceID;
extern CONST WCHAR *CONST HcdHardwareIDs;

#endif // !VIRTUSB_H
