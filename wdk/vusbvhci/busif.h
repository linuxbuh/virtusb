#ifndef VUSBVHCI_BUSIF_H
#define VUSBVHCI_BUSIF_H

#include "vusbvhci.h"

#ifndef TARGETING_Win2K

VOID
VUsbVhci_InterfaceReference(
	_In_ PVOID Context
	);

VOID
VUsbVhci_InterfaceDereference(
	_In_ PVOID Context
	);

// USB_BUS_INTERFACE_HUB_V5 declarations:

NTSTATUS
USB_BUSIFFN
VUsbVhci_BUSIF_CreateUsbDevice(
	_In_    PVOID              BusContext,
	_Inout_ PUSB_DEVICE_HANDLE *DeviceHandle,
	_In_    PUSB_DEVICE_HANDLE HubDeviceHandle,
	_In_    USHORT             PortStatus,
	_In_    USHORT             PortNumber
	);

NTSTATUS
USB_BUSIFFN
VUsbVhci_BUSIF_InitializeUsbDevice(
	_In_    PVOID              BusContext,
	_Inout_ PUSB_DEVICE_HANDLE DeviceHandle
	);

NTSTATUS
USB_BUSIFFN
VUsbVhci_BUSIF_RemoveUsbDevice(
	_In_    PVOID              BusContext,
	_Inout_ PUSB_DEVICE_HANDLE DeviceHandle,
	_In_    ULONG              Flags
	);

NTSTATUS
USB_BUSIFFN
VUsbVhci_BUSIF_GetUsbDescriptors(
	_In_    PVOID              BusContext,
	_Inout_ PUSB_DEVICE_HANDLE DeviceHandle,
	_Inout_ PUCHAR             DeviceDescriptorBuffer,
	_Inout_ PULONG             DeviceDescriptorBufferLength,
	_Inout_ PUCHAR             ConfigDescriptorBuffer,
	_Inout_ PULONG             ConfigDescriptorBufferLength
	);

NTSTATUS
USB_BUSIFFN
VUsbVhci_BUSIF_RestoreUsbDevice(
	_In_    PVOID              BusContext,
	_Inout_ PUSB_DEVICE_HANDLE OldDeviceHandle,
	_Inout_ PUSB_DEVICE_HANDLE NewDeviceHandle
	);

NTSTATUS
USB_BUSIFFN
VUsbVhci_BUSIF_GetUsbDeviceHackFlags(
	_In_    PVOID              BusContext,
	_In_    PUSB_DEVICE_HANDLE DeviceHandle,
	_Inout_ PULONG             HackFlags
	);

NTSTATUS
USB_BUSIFFN
VUsbVhci_BUSIF_GetPortHackFlags(
	_In_    PVOID  BusContext,
	_Inout_ PULONG HackFlags
	);

NTSTATUS
USB_BUSIFFN
VUsbVhci_BUSIF_QueryDeviceInformation(
	_In_    PVOID              BusContext,
	_In_    PUSB_DEVICE_HANDLE DeviceHandle,
	_Inout_ PVOID              DeviceInformationBuffer,
	_In_    ULONG              DeviceInformationBufferLength,
	_Inout_ PULONG             LengthOfDataReturned
	);

NTSTATUS
USB_BUSIFFN
VUsbVhci_BUSIF_GetControllerInformation(
	_In_    PVOID  BusContext,
	_Inout_ PVOID  ControllerInformationBuffer,
	_In_    ULONG  ControllerInformationBufferLength,
	_Inout_ PULONG LengthOfDataReturned
	);

NTSTATUS
USB_BUSIFFN
VUsbVhci_BUSIF_ControllerSelectiveSuspend(
	_In_ PVOID   BusContext,
	_In_ BOOLEAN Enable
	);

NTSTATUS
USB_BUSIFFN
VUsbVhci_BUSIF_GetExtendedHubInformation(
	_In_    PVOID          BusContext,
	_In_    PDEVICE_OBJECT HubPhysicalDeviceObject,
	_Inout_ PVOID          HubInformationBuffer,
	_In_    ULONG          HubInformationBufferLength,
	_Inout_ PULONG         LengthOfDataReturned
	);

NTSTATUS
USB_BUSIFFN
VUsbVhci_BUSIF_GetRootHubSymbolicName(
	_In_    PVOID  BusContext,
	_Inout_ PVOID  HubInformationBuffer,
	_In_    ULONG  HubInformationBufferLength,
	_Out_   PULONG HubNameActualLength
	);

PVOID
USB_BUSIFFN
VUsbVhci_BUSIF_GetDeviceBusContext(
	_In_ PVOID HubBusContext,
	_In_ PVOID DeviceHandle
	);

NTSTATUS
USB_BUSIFFN
VUsbVhci_BUSIF_Initialize20Hub(
	_In_ PVOID              HubBusContext,
	_In_ PUSB_DEVICE_HANDLE HubDeviceHandle,
	_In_ ULONG              TtCount
	);

NTSTATUS
USB_BUSIFFN
VUsbVhci_BUSIF_RootHubInitNotification(
	_In_ PVOID             HubBusContext,
	_In_ PVOID             CallbackContext,
	_In_ PRH_INIT_CALLBACK CallbackFunction
	);

VOID
USB_BUSIFFN
VUsbVhci_BUSIF_FlushTransfers(
	PVOID BusContext,
	PVOID DeviceHandle
	);

VOID
USB_BUSIFFN
VUsbVhci_BUSIF_SetDeviceHandleData(
	PVOID          BusContext,
	PVOID          DeviceHandle,
	PDEVICE_OBJECT UsbDevicePdo
	);

// USB_BUS_INTERFACE_USBDI_V2 declarations:

NTSTATUS
USB_BUSIFFN
VUsbVhci_BUSIF_SubmitIsoOutUrb(
	_In_ PVOID BusContext,
	_In_ PURB  Urb
	);

VOID
USB_BUSIFFN
VUsbVhci_BUSIF_GetUSBDIVersion(
	_In_    PVOID                     BusContext,
	_Inout_ PUSBD_VERSION_INFORMATION VersionInformation,
	_Inout_ PULONG                    HcdCapabilities
	);

NTSTATUS
USB_BUSIFFN
VUsbVhci_BUSIF_QueryBusTime(
	_In_    PVOID  BusContext,
	_Inout_ PULONG CurrentUsbFrame
	);

NTSTATUS
USB_BUSIFFN
VUsbVhci_BUSIF_EnumLogEntry(
	PVOID BusContext,
	ULONG DriverTag,
	ULONG EnumTag,
	ULONG P1,
	ULONG P2
	);

NTSTATUS
USB_BUSIFFN
VUsbVhci_BUSIF_QueryBusInformation(
	_In_    PVOID  BusContext,
	_In_    ULONG  Level,
	_Inout_ PVOID  BusInformationBuffer,
	_Inout_ PULONG BusInformationBufferLength,
	_Out_   PULONG BusInformationActualLength
	);

BOOLEAN
USB_BUSIFFN
VUsbVhci_BUSIF_IsDeviceHighSpeed(
	_In_ PVOID BusContext
	);

#endif // !TARGETING_Win2K

#endif // !VUSBVHCI_BUSIF_H
