#ifndef VIRTUSB_BUSIF_H
#define VIRTUSB_BUSIF_H

#include "virtusb.h"

VOID
VirtUsb_InterfaceReference(
	_In_ PVOID Context
	);

VOID
VirtUsb_InterfaceDereference(
	_In_ PVOID Context
	);

PUSBHUB_CONTEXT
VIRTUSB_BUSIFFN
VirtUsb_BUSIF_GetRootHubContext(
	_In_ PVOID Context
	);

PUSB_HUB_DESCRIPTOR
VIRTUSB_BUSIFFN
VirtUsb_BUSIF_GetRootHubHubDescriptor(
	_In_ PVOID Context
	);

NTSTATUS
VIRTUSB_BUSIFFN
VirtUsb_BUSIF_ReferenceUsbDeviceByHandle(
	_In_  PVOID           Context,
	_In_  PVOID           DeviceHandle,
	_Out_ PUSBDEV_CONTEXT *DeviceContext
	);

VOID
VIRTUSB_BUSIFFN
VirtUsb_BUSIF_ReferenceUsbDevice(
	_In_ PVOID           Context,
	_In_ PUSBDEV_CONTEXT DeviceContext
	);

VOID
VIRTUSB_BUSIFFN
VirtUsb_BUSIF_DereferenceUsbDevice(
	_In_ PVOID           Context,
	_In_ PUSBDEV_CONTEXT DeviceContext
	);

NTSTATUS
VIRTUSB_BUSIFFN
VirtUsb_BUSIF_CreateUsbDevice(
	_In_    PVOID           Context,
	_Inout_ PVOID           *DeviceHandle,
	_In_    PUSBHUB_CONTEXT HubDeviceContext,
	_In_    USHORT          PortStatus,
	_In_    USHORT          PortNumber
	);

NTSTATUS
VIRTUSB_BUSIFFN
VirtUsb_BUSIF_InitializeUsbDevice(
	_In_    PVOID           Context,
	_Inout_ PUSBDEV_CONTEXT DeviceContext
	);

NTSTATUS
VIRTUSB_BUSIFFN
VirtUsb_BUSIF_RemoveUsbDevice(
	_In_    PVOID           Context,
	_Inout_ PUSBDEV_CONTEXT DeviceContext,
	_In_    ULONG           Flags,
	_In_    BOOLEAN         Dereference
	);

#endif // !VIRTUSB_BUSIF_H
