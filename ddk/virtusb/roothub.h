#ifndef VIRTUSB_ROOTHUB_H
#define VIRTUSB_ROOTHUB_H

#include "virtusb.h"

// for SET_FEATURE and CLEAR_FEATURE requests
#define FEATURE_C_HUB_LOCAL_POWER    0
#define FEATURE_C_HUB_OVER_CURRENT   1
#define FEATURE_PORT_CONNECTION      0
#define FEATURE_PORT_ENABLE          1
#define FEATURE_PORT_SUSPEND         2
#define FEATURE_PORT_OVER_CURRENT    3
#define FEATURE_PORT_RESET           4
#define FEATURE_PORT_POWER           8
#define FEATURE_PORT_LOW_SPEED       9
#define FEATURE_PORT_HIGH_SPEED     10
#define FEATURE_C_PORT_CONNECTION   16
#define FEATURE_C_PORT_ENABLE       17
#define FEATURE_C_PORT_SUSPEND      18
#define FEATURE_C_PORT_OVER_CURRENT 19
#define FEATURE_C_PORT_RESET        20
#define FEATURE_PORT_TEST           21
#define FEATURE_PORT_INDICATOR      22

#define HUBREQ_CLEAR_HUB_FEATURE     (0x2000 | USB_REQUEST_CLEAR_FEATURE)
#define HUBREQ_CLEAR_PORT_FEATURE    (0x2300 | USB_REQUEST_CLEAR_FEATURE)
#define HUBREQ_GET_HUB_DESCRIPTOR    (0xa000 | USB_REQUEST_GET_DESCRIPTOR)
#define HUBREQ_GET_HUB_STATUS        (0xa000 | USB_REQUEST_GET_STATUS)
#define HUBREQ_GET_PORT_STATUS       (0xa300 | USB_REQUEST_GET_STATUS)
#define HUBREQ_SET_HUB_FEATURE       (0x2000 | USB_REQUEST_SET_FEATURE)
#define HUBREQ_SET_PORT_FEATURE      (0x2300 | USB_REQUEST_SET_FEATURE)

PUSB_HUB_DESCRIPTOR
VirtUsb_GenHubDescriptor(
	IN ULONG PortCount
	);

NTSTATUS
VirtUsb_InitRootHubDescriptors(
	IN PUSBHUB_CONTEXT RootHub,
	IN ULONG           PortCount
	);

NTSTATUS
VirtUsb_ProcRootHubUrb(
	IN PPDO_DEVICE_DATA PdoData,
	IN PIRP             Irp
	);

BOOLEAN
VirtUsb_ReportRootHubChanges(
	IN  PPDO_DEVICE_DATA PdoData,
	OUT PULONG_PTR       BitArr
	);

VOID
VirtUsb_RootHubNotify(
	IN PPDO_DEVICE_DATA PdoData
	);

extern CONST UCHAR RootHubDeviceDescriptor[sizeof(USB_DEVICE_DESCRIPTOR)];
extern CONST UCHAR RootHubConfigurationDescriptor[sizeof(USB_CONFIGURATION_DESCRIPTOR) +
                                                  sizeof(USB_INTERFACE_DESCRIPTOR) +
                                                  sizeof(USB_ENDPOINT_DESCRIPTOR)];
extern CONST UCHAR RootHubHubDescriptor[7];

#endif // !VIRTUSB_ROOTHUB_H
