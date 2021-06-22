#ifndef VUSBVHCI_BUSIF_H
#define VUSBVHCI_BUSIF_H

#include "vusbvhci.h"

#ifndef TARGETING_Win2K

VOID
VUsbVhci_InterfaceReference(
	IN PVOID Context
	);

VOID
VUsbVhci_InterfaceDereference(
	IN PVOID Context
	);

// USB_BUS_INTERFACE_HUB_V5 declarations:

NTSTATUS
USB_BUSIFFN
VUsbVhci_BUSIF_CreateUsbDevice(
	IN     PVOID              BusContext,
	IN OUT PUSB_DEVICE_HANDLE *DeviceHandle,
	IN     PUSB_DEVICE_HANDLE HubDeviceHandle,
	IN     USHORT             PortStatus,
	IN     USHORT             PortNumber
	);

NTSTATUS
USB_BUSIFFN
VUsbVhci_BUSIF_InitializeUsbDevice(
	IN     PVOID              BusContext,
	IN OUT PUSB_DEVICE_HANDLE DeviceHandle
	);

NTSTATUS
USB_BUSIFFN
VUsbVhci_BUSIF_RemoveUsbDevice(
	IN     PVOID              BusContext,
	IN OUT PUSB_DEVICE_HANDLE DeviceHandle,
	IN     ULONG              Flags
	);

NTSTATUS
USB_BUSIFFN
VUsbVhci_BUSIF_GetUsbDescriptors(
	IN     PVOID              BusContext,
	IN OUT PUSB_DEVICE_HANDLE DeviceHandle,
	IN OUT PUCHAR             DeviceDescriptorBuffer,
	IN OUT PULONG             DeviceDescriptorBufferLength,
	IN OUT PUCHAR             ConfigDescriptorBuffer,
	IN OUT PULONG             ConfigDescriptorBufferLength
	);

NTSTATUS
USB_BUSIFFN
VUsbVhci_BUSIF_RestoreUsbDevice(
	IN     PVOID              BusContext,
	IN OUT PUSB_DEVICE_HANDLE OldDeviceHandle,
	IN OUT PUSB_DEVICE_HANDLE NewDeviceHandle
	);

NTSTATUS
USB_BUSIFFN
VUsbVhci_BUSIF_GetUsbDeviceHackFlags(
	IN     PVOID              BusContext,
	IN     PUSB_DEVICE_HANDLE DeviceHandle,
	IN OUT PULONG             HackFlags
	);

NTSTATUS
USB_BUSIFFN
VUsbVhci_BUSIF_GetPortHackFlags(
	IN     PVOID  BusContext,
	IN OUT PULONG HackFlags
	);

NTSTATUS
USB_BUSIFFN
VUsbVhci_BUSIF_QueryDeviceInformation(
	IN     PVOID              BusContext,
	IN     PUSB_DEVICE_HANDLE DeviceHandle,
	IN OUT PVOID              DeviceInformationBuffer,
	IN     ULONG              DeviceInformationBufferLength,
	IN OUT PULONG             LengthOfDataReturned
	);

NTSTATUS
USB_BUSIFFN
VUsbVhci_BUSIF_GetControllerInformation(
	IN     PVOID  BusContext,
	IN OUT PVOID  ControllerInformationBuffer,
	IN     ULONG  ControllerInformationBufferLength,
	IN OUT PULONG LengthOfDataReturned
	);

NTSTATUS
USB_BUSIFFN
VUsbVhci_BUSIF_ControllerSelectiveSuspend(
	IN PVOID   BusContext,
	IN BOOLEAN Enable
	);

NTSTATUS
USB_BUSIFFN
VUsbVhci_BUSIF_GetExtendedHubInformation(
	IN     PVOID          BusContext,
	IN     PDEVICE_OBJECT HubPhysicalDeviceObject,
	IN OUT PVOID          HubInformationBuffer,
	IN     ULONG          HubInformationBufferLength,
	IN OUT PULONG         LengthOfDataReturned
	);

NTSTATUS
USB_BUSIFFN
VUsbVhci_BUSIF_GetRootHubSymbolicName(
	IN     PVOID  BusContext,
	IN OUT PVOID  HubInformationBuffer,
	IN     ULONG  HubInformationBufferLength,
	OUT    PULONG HubNameActualLength
	);

PVOID
USB_BUSIFFN
VUsbVhci_BUSIF_GetDeviceBusContext(
	IN PVOID HubBusContext,
	IN PVOID DeviceHandle
	);

NTSTATUS
USB_BUSIFFN
VUsbVhci_BUSIF_Initialize20Hub(
	IN PVOID              HubBusContext,
	IN PUSB_DEVICE_HANDLE HubDeviceHandle,
	IN ULONG              TtCount
	);

NTSTATUS
USB_BUSIFFN
VUsbVhci_BUSIF_RootHubInitNotification(
	IN PVOID             HubBusContext,
	IN PVOID             CallbackContext,
	IN PRH_INIT_CALLBACK CallbackFunction
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
	IN PVOID BusContext,
	IN PURB  Urb
	);

VOID
USB_BUSIFFN
VUsbVhci_BUSIF_GetUSBDIVersion(
	IN     PVOID                     BusContext,
	IN OUT PUSBD_VERSION_INFORMATION VersionInformation,
	IN OUT PULONG                    HcdCapabilities
	);

NTSTATUS
USB_BUSIFFN
VUsbVhci_BUSIF_QueryBusTime(
	IN     PVOID  BusContext,
	IN OUT PULONG CurrentUsbFrame
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
	IN     PVOID  BusContext,
	IN     ULONG  Level,
	IN OUT PVOID  BusInformationBuffer,
	IN OUT PULONG BusInformationBufferLength,
	OUT    PULONG BusInformationActualLength
	);

BOOLEAN
USB_BUSIFFN
VUsbVhci_BUSIF_IsDeviceHighSpeed(
	IN PVOID BusContext
	);

#endif // !TARGETING_Win2K

#endif // !VUSBVHCI_BUSIF_H
