#ifndef VIRTUSB_USBDEV_H
#define VIRTUSB_USBDEV_H

#include "virtusb.h"

VOID
VirtUsb_InitializePortContext(
	_Out_ PPORT_CONTEXT   Port,
	_In_  PUSBHUB_CONTEXT UsbHub,
	_In_  ULONG           Index
	);

VOID
VirtUsb_DestroyPortContext(
	_In_ PPORT_CONTEXT Port
	);

NTSTATUS
VirtUsb_AllocateUsbDev(
	_In_  PPDO_DEVICE_DATA PdoData,
	_Out_ PUSBDEV_CONTEXT  *UsbDev,
	_In_  PPORT_CONTEXT    Port
	);

NTSTATUS
VirtUsb_AllocateUsbHub(
	_In_  PPDO_DEVICE_DATA PdoData,
	_Out_ PUSBHUB_CONTEXT  *UsbHub,
	_In_  PPORT_CONTEXT    Port,
	_In_  ULONG            PortCount
	);

VOID
VirtUsb_DestroyUsbDev(
	_In_ PUSBDEV_CONTEXT UsbDev,
	_In_ BOOLEAN         ReUse
	);

VOID
VirtUsb_FreeUsbDev(
	_In_ PUSBDEV_CONTEXT UsbDev
	);

NTSTATUS
VirtUsb_ConvertToUsbHub(
	_Inout_ PUSBDEV_CONTEXT UsbDev,
	_In_    ULONG           PortCount
	);

NTSTATUS
VirtUsb_UsbDevInitConfigArray(
	_In_ PUSBDEV_CONTEXT UsbDev,
	_In_ ULONG           NumConfigs
	);

NTSTATUS
VirtUsb_UsbDevInitIfcArray(
	_In_ PCONF_CONTEXT Conf,
	_In_ ULONG         NumIfcs
	);

NTSTATUS
VirtUsb_UsbDevInitAIfcArray(
	_In_ PIFC_CONTEXT Ifc,
	_In_ ULONG        NumAIfcs
	);

NTSTATUS
VirtUsb_UsbDevInitEpArray(
	_In_ PAIFC_CONTEXT AIfc,
	_In_ ULONG         NumEps
	);

NTSTATUS
VirtUsb_UsbDevInitDesc(
	_In_    PUSBDEV_CONTEXT UsbDev,
	_Inout_ PUCHAR          *DstDesc,
	_In_    CONST UCHAR     *SrcDesc
	);

BOOLEAN
VirtUsb_CheckConfDescBounds(
	_In_ PUSB_CONFIGURATION_DESCRIPTOR Desc
	);

NTSTATUS
VirtUsb_UsbDevParseConfDesc(
	_In_ PCONF_CONTEXT Conf
	);

NTSTATUS
VirtUsb_UsbDevParseAIfcDesc(
	_In_ PAIFC_CONTEXT AIfc
	);

BOOLEAN
VirtUsb_IsValidDeviceHandle(
	_In_ PPDO_DEVICE_DATA   PdoData,
	_In_ PUSB_DEVICE_HANDLE DeviceHandle
	);

BOOLEAN
VirtUsb_IsValidPipeHandle(
	_In_ PUSBDEV_CONTEXT  Device,
	_In_ USBD_PIPE_HANDLE PipeHandle
	);

PUSBDEV_CONTEXT
VirtUsb_ReferenceUsbDeviceByHandle(
	_In_ PPDO_DEVICE_DATA   PdoData,
	_In_ PUSB_DEVICE_HANDLE DeviceHandle,
	_In_ PVOID              Tag
	);

VOID
VirtUsb_ReferenceUsbDevice(
	_In_ PUSBDEV_CONTEXT DeviceContext,
	_In_ PVOID           Tag
	);

VOID
VirtUsb_DereferenceUsbDevice(
	_In_ PUSBDEV_CONTEXT DeviceContext,
	_In_ PVOID           Tag,
	_In_ BOOLEAN         HasParentSpinLock
	);

#endif // VIRTUSB_USBDEV_H
