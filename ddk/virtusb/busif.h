#ifndef VIRTUSB_BUSIF_H
#define VIRTUSB_BUSIF_H

#include "virtusb.h"

VOID
VirtUsb_InterfaceReference(
	IN PVOID Context
	);

VOID
VirtUsb_InterfaceDereference(
	IN PVOID Context
	);

PUSBHUB_CONTEXT
VIRTUSB_BUSIFFN
VirtUsb_BUSIF_GetRootHubContext(
	IN PVOID Context
	);

PUSB_HUB_DESCRIPTOR
VIRTUSB_BUSIFFN
VirtUsb_BUSIF_GetRootHubHubDescriptor(
	IN PVOID Context
	);

NTSTATUS
VIRTUSB_BUSIFFN
VirtUsb_BUSIF_ReferenceUsbDeviceByHandle(
	IN  PVOID           Context,
	IN  PVOID           DeviceHandle,
	OUT PUSBDEV_CONTEXT *DeviceContext
	);

VOID
VIRTUSB_BUSIFFN
VirtUsb_BUSIF_ReferenceUsbDevice(
	IN PVOID           Context,
	IN PUSBDEV_CONTEXT DeviceContext
	);

VOID
VIRTUSB_BUSIFFN
VirtUsb_BUSIF_DereferenceUsbDevice(
	IN PVOID           Context,
	IN PUSBDEV_CONTEXT DeviceContext
	);

NTSTATUS
VIRTUSB_BUSIFFN
VirtUsb_BUSIF_CreateUsbDevice(
	IN     PVOID           Context,
	IN OUT PVOID           *DeviceHandle,
	IN     PUSBHUB_CONTEXT HubDeviceContext,
	IN     USHORT          PortStatus,
	IN     USHORT          PortNumber
	);

NTSTATUS
VIRTUSB_BUSIFFN
VirtUsb_BUSIF_InitializeUsbDevice(
	IN     PVOID           Context,
	IN OUT PUSBDEV_CONTEXT DeviceContext
	);

NTSTATUS
VIRTUSB_BUSIFFN
VirtUsb_BUSIF_RemoveUsbDevice(
	IN     PVOID           Context,
	IN OUT PUSBDEV_CONTEXT DeviceContext,
	IN     ULONG           Flags,
	IN     BOOLEAN         Dereference
	);

#endif // !VIRTUSB_BUSIF_H
