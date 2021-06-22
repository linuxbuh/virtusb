#ifndef VIRTUSB_USBDEV_H
#define VIRTUSB_USBDEV_H

#include "virtusb.h"

VOID
VirtUsb_InitializePortContext(
	OUT PPORT_CONTEXT   Port,
	IN  PUSBHUB_CONTEXT UsbHub,
	IN  ULONG           Index
	);

VOID
VirtUsb_DestroyPortContext(
	IN PPORT_CONTEXT Port
	);

NTSTATUS
VirtUsb_AllocateUsbDev(
	IN  PPDO_DEVICE_DATA PdoData,
	OUT PUSBDEV_CONTEXT  *UsbDev,
	IN  PPORT_CONTEXT    Port
	);

NTSTATUS
VirtUsb_AllocateUsbHub(
	IN  PPDO_DEVICE_DATA PdoData,
	OUT PUSBHUB_CONTEXT  *UsbHub,
	IN  PPORT_CONTEXT    Port,
	IN  ULONG            PortCount
	);

VOID
VirtUsb_DestroyUsbDev(
	IN PUSBDEV_CONTEXT UsbDev,
	IN BOOLEAN         ReUse
	);

VOID
VirtUsb_FreeUsbDev(
	IN PUSBDEV_CONTEXT UsbDev
	);

NTSTATUS
VirtUsb_ConvertToUsbHub(
	IN OUT PUSBDEV_CONTEXT UsbDev,
	IN     ULONG           PortCount
	);

NTSTATUS
VirtUsb_UsbDevInitConfigArray(
	IN PUSBDEV_CONTEXT UsbDev,
	IN ULONG           NumConfigs
	);

NTSTATUS
VirtUsb_UsbDevInitIfcArray(
	IN PCONF_CONTEXT Conf,
	IN ULONG         NumIfcs
	);

NTSTATUS
VirtUsb_UsbDevInitAIfcArray(
	IN PIFC_CONTEXT Ifc,
	IN ULONG        NumAIfcs
	);

NTSTATUS
VirtUsb_UsbDevInitEpArray(
	IN PAIFC_CONTEXT AIfc,
	IN ULONG         NumEps
	);

NTSTATUS
VirtUsb_UsbDevInitDesc(
	IN     PUSBDEV_CONTEXT UsbDev,
	IN OUT PUCHAR          *DstDesc,
	IN     CONST UCHAR     *SrcDesc
	);

BOOLEAN
VirtUsb_CheckConfDescBounds(
	IN PUSB_CONFIGURATION_DESCRIPTOR Desc
	);

NTSTATUS
VirtUsb_UsbDevParseConfDesc(
	IN PCONF_CONTEXT Conf
	);

NTSTATUS
VirtUsb_UsbDevParseAIfcDesc(
	IN PAIFC_CONTEXT AIfc
	);

BOOLEAN
VirtUsb_IsValidDeviceHandle(
	IN PPDO_DEVICE_DATA   PdoData,
	IN PUSB_DEVICE_HANDLE DeviceHandle
	);

BOOLEAN
VirtUsb_IsValidPipeHandle(
	IN PUSBDEV_CONTEXT  Device,
	IN USBD_PIPE_HANDLE PipeHandle
	);

PUSBDEV_CONTEXT
VirtUsb_ReferenceUsbDeviceByHandle(
	IN PPDO_DEVICE_DATA   PdoData,
	IN PUSB_DEVICE_HANDLE DeviceHandle,
	IN PVOID              Tag
	);

VOID
VirtUsb_ReferenceUsbDevice(
	IN PUSBDEV_CONTEXT DeviceContext,
	IN PVOID           Tag
	);

VOID
VirtUsb_DereferenceUsbDevice(
	IN PUSBDEV_CONTEXT DeviceContext,
	IN PVOID           Tag,
	IN BOOLEAN         HasParentSpinLock
	);

#endif // VIRTUSB_USBDEV_H
