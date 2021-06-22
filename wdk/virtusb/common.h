#ifndef VUSB_COMMON_H
#define VUSB_COMMON_H

#include <ntddk.h>
#include <initguid.h> // required for GUID definitions
#include <usb.h>

// {7AC0B8E3-4AAD-4ea7-9A99-BA15BC110120}
DEFINE_GUID(VIRTUSB_BUS_INTERFACE_VHCI_GUID,
            0x7ac0b8e3, 0x4aad, 0x4ea7, 0x9a, 0x99, 0xba, 0x15, 0xbc, 0x11, 0x1, 0x20);

#define VIRTUSB_BUSIFFN NTAPI

typedef enum _USBDEV_STATE
{
	DeviceAttached = 0, // device connected&powered (reset may be in process)
	DefaultDevState,    // reset done but no address asssigned
	Addressed,          // device is in ADDRESS-state but not configured
	Configured          // device is IN CONFIGURED-state
} USBDEV_STATE;

// Some predeclarations
typedef struct _FILE_CONTEXT *PFILE_CONTEXT;
typedef struct _AIFC_CONTEXT *PAIFC_CONTEXT;
typedef struct _IFC_CONTEXT *PIFC_CONTEXT;
typedef struct _CONF_CONTEXT *PCONF_CONTEXT;
typedef struct _USBDEV_CONTEXT *PUSBDEV_CONTEXT;
typedef struct _USBHUB_CONTEXT *PUSBHUB_CONTEXT;

typedef struct _PORT_CONTEXT
{
	// A back pointer to the hub context
	PUSBHUB_CONTEXT ParentHub;

	// The index of this port in its parent hub-context
	ULONG           Index;

	// Used to wait on device diconnection until hub driver knows about the state
	KEVENT          HubDriverTouchedEvent;

	// See USB_PORT_STATUS_*-definitions in usb200.h.
	USHORT          Status;
	USHORT          Change;
	UCHAR           Flags;

	PUSBDEV_CONTEXT UsbDevice;
} PORT_CONTEXT, *PPORT_CONTEXT;

typedef struct _PIPE_CONTEXT
{
	PUSBDEV_CONTEXT          Device;
	BOOLEAN                  IsEP0;

	// For EP0 all the following are ALWAYS zero

	PAIFC_CONTEXT            AltSetting;
	ULONG                    Index;

	PUSB_ENDPOINT_DESCRIPTOR Descriptor; // pointer into configuration descriptor
} PIPE_CONTEXT, *PPIPE_CONTEXT;

typedef struct _AIFC_CONTEXT
{
	PIFC_CONTEXT              Interface;
	ULONG                     Index;

	PUSB_INTERFACE_DESCRIPTOR Descriptor; // pointer into configuration descriptor
	ULONG                     EndpointCount;
	PPIPE_CONTEXT             Endpoint;
} AIFC_CONTEXT, *PAIFC_CONTEXT;

typedef struct _IFC_CONTEXT
{
	PCONF_CONTEXT             Configuration;
	ULONG                     Index;

	PUSB_INTERFACE_DESCRIPTOR Descriptor; // pointer into configuration descriptor
	ULONG                     ActiveAltSetting; // AltSetting[ActiveAltSetting] is active; not to be interpreted as the bAlternateSetting-field of the descriptor struct.
	ULONG                     AltSettingCount;
	PAIFC_CONTEXT             AltSetting;
} IFC_CONTEXT, *PIFC_CONTEXT;

typedef struct _CONF_CONTEXT
{
	PUSBDEV_CONTEXT               Device;
	ULONG                         Index;

	PUSB_CONFIGURATION_DESCRIPTOR Descriptor;
	ULONG                         InterfaceCount;
	PIFC_CONTEXT                  Interface;
} CONF_CONTEXT, *PCONF_CONTEXT;

typedef struct _USBDEV_CONTEXT
{
	// A back pointer to the pdoo
	PDEVICE_OBJECT         ParentPdo;

	// A back pointer to the hub context
	PUSBHUB_CONTEXT        ParentHub;

	// A back pointer to the hub context
	PUSBHUB_CONTEXT        RootHub;

	// A back pointer to the port context
	PPORT_CONTEXT          ParentPort;

	// Used by the VUsbVhci-driver to store a pointer
	// to the PDO of the usb device. This field is
	// untouched by this (the VirtUsb) driver, and initialized
	// with NULL after the struct has been allocated.
	PVOID                  HcdContext;

	// Used by the VUsbVhci-driver to store a pointer
	// to its FdoData. This field is untouched by this
	// (the VirtUsb) driver, and initialized with NULL
	// after the struct has been allocated.
	PVOID                  HcdContext2;

	KSPIN_LOCK             Lock;

	USB_DEVICE_SPEED       Speed;
	USB_DEVICE_TYPE        Type;
	USBDEV_STATE           State;
	BOOLEAN                IsHub;

	// Address currently set to device (0x00 before SET_ADDRESS)
	UCHAR                  Address;

	// Address choosen for this device (copied to Address after SET_ADDRESS is done)
	UCHAR                  AssignedAddress;

	BOOLEAN                Ready;

	// Notify event: Gets set, if no Irp is referencing this device.
	//               Gets cleared, if one or more Irps are referencing this device.
	KEVENT                 DeviceRemovableEvent;

	// Reference counter for DeviceRemovableEvent
	LONG                   RefCount;

	PUSB_DEVICE_DESCRIPTOR Descriptor;
	PIPE_CONTEXT           EndpointZero;
	ULONG                  ActiveConfiguration; // Configuration[ActiveConfiguration] is active; not to be interpreted as the bConfigurationValue-field of the descriptor struct.
	ULONG                  ConfigurationCount;
	PCONF_CONTEXT          Configuration;
} USBDEV_CONTEXT, *PUSBDEV_CONTEXT;

typedef struct _USBHUB_CONTEXT
{
	USBDEV_CONTEXT;
	PUSB_HUB_DESCRIPTOR HubDescriptor;
	ULONG               PortCount;
	PPORT_CONTEXT       Port;
} USBHUB_CONTEXT, *PUSBHUB_CONTEXT;

typedef PUSBHUB_CONTEXT
	(VIRTUSB_BUSIFFN *PVIRTUSB_BUSIFFN_GET_ROOT_HUB_CONTEXT)(
		_In_ PVOID
		);

typedef PUSB_HUB_DESCRIPTOR
	(VIRTUSB_BUSIFFN *PVIRTUSB_BUSIFFN_GET_ROOT_HUB_HUB_DESCRIPTOR)(
		_In_ PVOID
		);

typedef NTSTATUS
	(VIRTUSB_BUSIFFN *PVIRTUSB_BUSIFFN_REFERENCE_USB_DEVICE_BY_HANDLE)(
		_In_  PVOID,
		_In_  PVOID,
		_Out_ PUSBDEV_CONTEXT *
		);

typedef VOID
	(VIRTUSB_BUSIFFN *PVIRTUSB_BUSIFFN_REFERENCE_USB_DEVICE)(
		_In_ PVOID,
		_In_ PUSBDEV_CONTEXT
		);

typedef VOID
	(VIRTUSB_BUSIFFN *PVIRTUSB_BUSIFFN_DEREFERENCE_USB_DEVICE)(
		_In_ PVOID,
		_In_ PUSBDEV_CONTEXT
		);

typedef NTSTATUS
	(VIRTUSB_BUSIFFN *PVIRTUSB_BUSIFFN_CREATE_USB_DEVICE)(
		_In_    PVOID,
		_Inout_ PVOID *,
		_In_    PUSBHUB_CONTEXT,
		_In_    USHORT,
		_In_    USHORT
		);

typedef NTSTATUS
	(VIRTUSB_BUSIFFN *PVIRTUSB_BUSIFFN_INITIALIZE_USB_DEVICE)(
		_In_    PVOID,
		_Inout_ PUSBDEV_CONTEXT
		);

typedef NTSTATUS
	(VIRTUSB_BUSIFFN *PVIRTUSB_BUSIFFN_REMOVE_USB_DEVICE)(
		_In_    PVOID,
		_Inout_ PUSBDEV_CONTEXT,
		_In_    ULONG,
		_In_    BOOLEAN
		);

#define VIRTUSB_BUSIF_VHCI_VERSION_0         0x0000
#define VIRTUSB_BUSIF_VHCI_VERSION_1         0x0001

typedef struct _VIRTUSB_BUS_INTERFACE_VHCI_V0
{
	USHORT Size;
	USHORT Version;

	PVOID Context;
	PINTERFACE_REFERENCE   InterfaceReference;
	PINTERFACE_DEREFERENCE InterfaceDereference;
} VIRTUSB_BUS_INTERFACE_VHCI_V0, *PVIRTUSB_BUS_INTERFACE_VHCI_V0;

typedef struct _VIRTUSB_BUS_INTERFACE_VHCI_V1
{
	USHORT Size;
	USHORT Version;

	PVOID Context;
	PINTERFACE_REFERENCE   InterfaceReference;
	PINTERFACE_DEREFERENCE InterfaceDereference;

	PVIRTUSB_BUSIFFN_GET_ROOT_HUB_CONTEXT        GetRootHubContext;
	PVIRTUSB_BUSIFFN_GET_ROOT_HUB_HUB_DESCRIPTOR GetRootHubHubDescriptor;

	PVIRTUSB_BUSIFFN_REFERENCE_USB_DEVICE_BY_HANDLE ReferenceUsbDeviceByHandle;
	PVIRTUSB_BUSIFFN_REFERENCE_USB_DEVICE           ReferenceUsbDevice;
	PVIRTUSB_BUSIFFN_DEREFERENCE_USB_DEVICE         DereferenceUsbDevice;
	PVIRTUSB_BUSIFFN_CREATE_USB_DEVICE              CreateUsbDevice;
	PVIRTUSB_BUSIFFN_INITIALIZE_USB_DEVICE          InitializeUsbDevice;
	PVIRTUSB_BUSIFFN_REMOVE_USB_DEVICE              RemoveUsbDevice;
} VIRTUSB_BUS_INTERFACE_VHCI_V1, *PVIRTUSB_BUS_INTERFACE_VHCI_V1;

// Just make sure we are speaking of the same...
C_ASSERT(sizeof(UCHAR)  == 1 && sizeof(CHAR)  == 1);
C_ASSERT(sizeof(WCHAR)  == 2);
C_ASSERT(sizeof(USHORT) == 2 && sizeof(SHORT) == 2);
C_ASSERT(sizeof(ULONG)  == 4 && sizeof(LONG)  == 4);
C_ASSERT(sizeof(ULONG_PTR) == sizeof(LONG_PTR));
C_ASSERT(sizeof(ULONG_PTR) == sizeof(PVOID));
C_ASSERT(sizeof(ULONG_PTR) >= sizeof(ULONG));
C_ASSERT(sizeof(ULONGLONG) == 8 && sizeof(LONGLONG) == 8);
C_ASSERT(sizeof(UINT64)    == sizeof(INT64));
C_ASSERT(sizeof(ULONGLONG) == sizeof(UINT64));
C_ASSERT(sizeof(ULONG_PTR) <= sizeof(ULONGLONG));

#define _common_bit_templ_ \
	ULONG i = bit / (sizeof *ptr * 8); \
	ULONG b = bit % (sizeof *ptr * 8);

static FORCEINLINE BOOLEAN test_bit(ULONG bit, PULONG_PTR ptr)
{
	_common_bit_templ_
	return !!(ptr[i] & ((ULONG_PTR)1 << b));
}

static FORCEINLINE VOID set_bit(ULONG bit, PULONG_PTR ptr)
{
	_common_bit_templ_
	ptr[i] |= (ULONG_PTR)1 << b;
}

static FORCEINLINE VOID clear_bit(ULONG bit, PULONG_PTR ptr)
{
	_common_bit_templ_
	ptr[i] &= ~((ULONG_PTR)1 << b);
}

static FORCEINLINE VOID change_bit(ULONG bit, PULONG_PTR ptr)
{
	_common_bit_templ_
	ptr[i] ^= (ULONG_PTR)1 << b;
}

static FORCEINLINE BOOLEAN test_and_set_bit(ULONG bit, PULONG_PTR ptr)
{
	BOOLEAN r = test_bit(bit, ptr);
	set_bit(bit, ptr);
	return r;
}

static FORCEINLINE BOOLEAN test_and_clear_bit(ULONG bit, PULONG_PTR ptr)
{
	BOOLEAN r = test_bit(bit, ptr);
	clear_bit(bit, ptr);
	return r;
}

#endif // !VUSB_COMMON_H
