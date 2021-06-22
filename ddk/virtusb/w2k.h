#ifndef VIRTUSB_W2K_H
#define VIRTUSB_W2K_H

// 3ABF6F2D-71C4-462a-8A92-1E6861E6AF27
DEFINE_GUID(GUID_DEVINTERFACE_USB_HOST_CONTROLLER,
            0x3abf6f2d, 0x71c4, 0x462a, 0x8a, 0x92, 0x1e, 0x68, 0x61, 0xe6, 0xaf, 0x27);

#define GUID_DEVINTERFACE_USB_HUB GUID_CLASS_USBHUB

// The InterlockedOr system call is required when the function driver processes the
// IRP_MN_WAIT_WAKE power IRP. Because InterlockedOr is not defined in the Windows
// 2000 DDK header files, it must be defined to use the intrinsic system call
// if the function driver is compiled under Windows 2000.
#ifndef InterlockedOr
#define InterlockedOr _InterlockedOr
#endif

#define RTL_NUMBER_OF(A) (sizeof(A)/sizeof((A)[0]))

typedef PVOID PUSB_DEVICE_HANDLE;

typedef enum _USB_DEVICE_SPEED
{
	UsbLowSpeed = 0,
	UsbFullSpeed,
	UsbHighSpeed
} USB_DEVICE_SPEED;

typedef enum _USB_DEVICE_TYPE
{
	Usb11Device = 0,
	Usb20Device
} USB_DEVICE_TYPE;

#define USB_DEVICE_QUALIFIER_DESCRIPTOR_TYPE            0x06
#define USB_OTHER_SPEED_CONFIGURATION_DESCRIPTOR_TYPE   0x07

// standard definiions for the port status
// word of the HUB port register

#define USB_PORT_STATUS_CONNECT         0x0001
#define USB_PORT_STATUS_ENABLE          0x0002
#define USB_PORT_STATUS_SUSPEND         0x0004
#define USB_PORT_STATUS_OVER_CURRENT    0x0008
#define USB_PORT_STATUS_RESET           0x0010
#define USB_PORT_STATUS_POWER           0x0100
#define USB_PORT_STATUS_LOW_SPEED       0x0200
#define USB_PORT_STATUS_HIGH_SPEED      0x0400

typedef union _BM_REQUEST_TYPE
{
	struct _BM
	{
		UCHAR Recipient:2;
		UCHAR Reserved:3;
		UCHAR Type:2;
		UCHAR Dir:1;
	};
	UCHAR B;
} BM_REQUEST_TYPE, *PBM_REQUEST_TYPE;

typedef struct _USB_DEFAULT_PIPE_SETUP_PACKET
{
	BM_REQUEST_TYPE bmRequestType;
	UCHAR bRequest;

	union _wValue
	{
		struct
		{
			UCHAR LowByte;
			UCHAR HiByte;
		};
		USHORT W;
	} wValue;

	union _wIndex
	{
		struct
		{
			UCHAR LowByte;
			UCHAR HiByte;
		};
		USHORT W;
	} wIndex;
	USHORT wLength;
} USB_DEFAULT_PIPE_SETUP_PACKET, *PUSB_DEFAULT_PIPE_SETUP_PACKET;

// setup packet is eight bytes -- defined by spec
C_ASSERT(sizeof(USB_DEFAULT_PIPE_SETUP_PACKET) == 8);

typedef struct _USB_BUS_INFORMATION_LEVEL_0
{
	ULONG TotalBandwidth;
	ULONG ConsumedBandwidth;
} USB_BUS_INFORMATION_LEVEL_0, *PUSB_BUS_INFORMATION_LEVEL_0;

typedef struct _USB_BUS_INFORMATION_LEVEL_1
{
	ULONG TotalBandwidth;
	ULONG ConsumedBandwidth;
	ULONG ControllerNameLength;
	WCHAR ControllerNameUnicodeString[1];
} USB_BUS_INFORMATION_LEVEL_1, *PUSB_BUS_INFORMATION_LEVEL_1;

//bmRequest.Dir
#define BMREQUEST_HOST_TO_DEVICE        0
#define BMREQUEST_DEVICE_TO_HOST        1

//bmRequest.Type
#define BMREQUEST_STANDARD              0
#define BMREQUEST_CLASS                 1
#define BMREQUEST_VENDOR                2

//bmRequest.Recipient
#define BMREQUEST_TO_DEVICE             0
#define BMREQUEST_TO_INTERFACE          1
#define BMREQUEST_TO_ENDPOINT           2
#define BMREQUEST_TO_OTHER              3

#define USBD_DEFAULT_PIPE_TRANSFER 0x00000008

#define USBD_KEEP_DEVICE_DATA   0x00000001
#define USBD_MARK_DEVICE_BUSY   0x00000002

#define USBD_STATUS_DEVICE_GONE            USBD_STATUS_INTERNAL_HC_ERROR
#define USBD_STATUS_INSUFFICIENT_RESOURCES USBD_STATUS_INTERNAL_HC_ERROR

#define PROTECTED_POOL              0
#define ExFreePoolWithTag(ptr, tag) ExFreePool(ptr)

#define min(a, b) ((a) > (b) ? (b) : (a))
#define max(a, b) ((a) > (b) ? (a) : (b))

#pragma warning(push)
#pragma warning(disable:4793)
FORCEINLINE
VOID
KeMemoryBarrier(
	VOID
	)
{
	LONG Barrier;
	__asm
	{
		xchg Barrier, eax
	}
}
#pragma warning(pop)

#endif // !VIRTUSB_W2K_H
