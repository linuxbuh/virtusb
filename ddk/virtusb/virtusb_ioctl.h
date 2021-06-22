#ifndef VIRTUSB_IOCTL_H
#define VIRTUSB_IOCTL_H

#include <stddef.h>
#include <initguid.h> // required for GUID definitions

#ifdef __cplusplus
extern "C" {
#endif

#ifndef __USBDI_H__
typedef LONG USBD_STATUS;
#endif

// {1697DB2E-AF9B-4ffe-A83B-05DE2C2E33E5}
DEFINE_GUID(GUID_DEVINTERFACE_VIRTUSB_BUS,
            0x1697db2e, 0xaf9b, 0x4ffe, 0xa8, 0x3b, 0x5, 0xde, 0x2c, 0x2e, 0x33, 0xe5);

// {7DEAB311-76FE-4cdc-BF60-08F2CEF16DD7}
DEFINE_GUID(GUID_DEVCLASS_VIRTUSB_BUS,
            0x7deab311, 0x76fe, 0x4cdc, 0xbf, 0x60, 0x8, 0xf2, 0xce, 0xf1, 0x6d, 0xd7);

// VIRTUSB_REGISTER: Used as input buffer of IOCREGISTER.
// If output buffers length is greater than 0, then it receives a single UINT32, which
// represents the ID of the file context.
typedef struct _VIRTUSB_REGISTER
{
	UCHAR  Version;               // Set to 1
	UCHAR  PortCount;             // Max number of devices
} VIRTUSB_REGISTER, *PVIRTUSB_REGISTER;

// VIRTUSB_PORT_STAT: May be used as input buffer of IOCPORTSTAT.
typedef struct _VIRTUSB_PORT_STAT
{
	USHORT Status;
#define VIRTUSB_PORT_STAT_CONNECT         0x0001
#define VIRTUSB_PORT_STAT_ENABLE          0x0002
#define VIRTUSB_PORT_STAT_SUSPEND         0x0004
#define VIRTUSB_PORT_STAT_OVER_CURRENT    0x0008
#define VIRTUSB_PORT_STAT_RESET           0x0010
#define VIRTUSB_PORT_STAT_POWER           0x0100
#define VIRTUSB_PORT_STAT_LOW_SPEED       0x0200
#define VIRTUSB_PORT_STAT_HIGH_SPEED      0x0400
#define VIRTUSB_PORT_STAT_TEST            0x0800
#define VIRTUSB_PORT_STAT_INDICATOR       0x1000
	USHORT Change;
#define VIRTUSB_PORT_STAT_C_CONNECT       0x0001
#define VIRTUSB_PORT_STAT_C_ENABLE        0x0002
#define VIRTUSB_PORT_STAT_C_SUSPEND       0x0004
#define VIRTUSB_PORT_STAT_C_OVER_CURRENT  0x0008
#define VIRTUSB_PORT_STAT_C_RESET         0x0010
	UCHAR  PortIndex;
	UCHAR  Flags;
#define VIRTUSB_PORT_STAT_FLAGS_RESUMING  0x01
	UCHAR  _reserved1, _reserved2;
} VIRTUSB_PORT_STAT, *PVIRTUSB_PORT_STAT;

typedef struct _VIRTUSB_SETUP_PACKET
{
	UCHAR  bmRequestType;
	UCHAR  bRequest;
	USHORT wValue;
	USHORT wIndex;
	USHORT wLength;
} VIRTUSB_SETUP_PACKET, *PVIRTUSB_SETUP_PACKET;

typedef struct _VIRTUSB_URB
{
	UINT64               Handle;
	VIRTUSB_SETUP_PACKET SetupPacket;     // only for control urbs
	ULONG                BufferLength;    // number of bytes allocated for the buffer
	ULONG                PacketCount;     // number of iso packets
	USHORT               Interval;
	USHORT               Flags;
#define VIRTUSB_URB_FLAGS_SHORT_TRANSFER_OK       2
#define VIRTUSB_URB_FLAGS_START_ISO_TRANSFER_ASAP 4
	UCHAR                Address;
	UCHAR                Endpoint;        // endpoint incl. direction
	UCHAR                Type;
#define VIRTUSB_URB_TYPE_ISO     0
#define VIRTUSB_URB_TYPE_INT     1
#define VIRTUSB_URB_TYPE_CONTROL 2
#define VIRTUSB_URB_TYPE_BULK    3
	UCHAR                _reserved;
} VIRTUSB_URB, *PVIRTUSB_URB;

// VIRTUSB_WORK: Used as output buffer of IOCFETCHWORK.
typedef struct _VIRTUSB_WORK
{
	union
	{
		VIRTUSB_URB       Urb;
		VIRTUSB_PORT_STAT PortStat;
	};

	UCHAR Type;
#define VIRTUSB_WORK_TYPE_PORT_STAT   0  // state of the port has changed
#define VIRTUSB_WORK_TYPE_PROCESS_URB 1  // hand an urb to the (virtual) hardware
#define VIRTUSB_WORK_TYPE_CANCEL_URB  2  // cancel urb if it isn't processed already
	ULONG _reserved; // forces same struct-size on 32 and 64 bit
} VIRTUSB_WORK, *PVIRTUSB_WORK;

typedef struct _VIRTUSB_ISO_PACKET_DATA
{
	ULONG Offset;
	ULONG PacketLength;
} VIRTUSB_ISO_PACKET_DATA, *PVIRTUSB_ISO_PACKET_DATA;

// VIRTUSB_URB_DATA: Used as input buffer of IOCFETCHDATA.
// Output buffer receives the content of the URB its buffer.
// Special case for ISO-URBs: Packet-offsets and -lengths are written into the
//                            input buffer!
typedef struct _VIRTUSB_URB_DATA
{
	UINT64                  Handle;                    // IN:  handle which identifies the urb
	ULONG                   _reserved;
	ULONG                   PacketCount;               // IN:  number of iso packets
	VIRTUSB_ISO_PACKET_DATA IsoPackets[ANYSIZE_ARRAY]; // OUT: iso packets
} VIRTUSB_URB_DATA, *PVIRTUSB_URB_DATA;
#define SIZEOF_VIRTUSB_URB_DATA(num_iso_packets) ((ULONG)FIELD_OFFSET(VIRTUSB_URB_DATA, IsoPackets[num_iso_packets]))

typedef struct _VIRTUSB_ISO_PACKET_GIVEBACK
{
	ULONG       PacketActual;
	USBD_STATUS Status;
} VIRTUSB_ISO_PACKET_GIVEBACK, *PVIRTUSB_ISO_PACKET_GIVEBACK;

// VIRTUSB_GIVEBACK: Used as input buffer of IOCGIVEBACK.
// For IN-URBs: Output buffer is also used as input; it contains the content of the
//              URB its buffer. The length of the output buffer has to match BufferActual.
typedef struct _VIRTUSB_GIVEBACK
{
	UINT64                      Handle;                    // IN:  handle which identifies the urb
	ULONG                       BufferActual;              // IN:  number of bytes which were actually transfered
	                                                       //      (for IN-ISOs BufferActual has to be equal to
	                                                       //      BufferLength; for OUT-ISOs this value will be ignored)
	ULONG                       PacketCount;               // IN:  for ISO (has to match with the value from the urb)
	ULONG                       ErrorCount;                // IN:  for ISO
	USBD_STATUS                 Status;                    // IN:  (ignored for ISO URBs)
	VIRTUSB_ISO_PACKET_GIVEBACK IsoPackets[ANYSIZE_ARRAY]; // IN:  iso packets
} VIRTUSB_GIVEBACK, *PVIRTUSB_GIVEBACK;
#define SIZEOF_VIRTUSB_GIVEBACK(num_iso_packets) ((ULONG)FIELD_OFFSET(VIRTUSB_GIVEBACK, IsoPackets[num_iso_packets]))

#define VIRTUSB_IOC_DEVICE_TYPE 0xaf9b
#define VIRTUSB_IOCREGISTER  CTL_CODE(VIRTUSB_IOC_DEVICE_TYPE, 0x0800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define VIRTUSB_IOCPORTSTAT  CTL_CODE(VIRTUSB_IOC_DEVICE_TYPE, 0x0801, METHOD_NEITHER,  FILE_ANY_ACCESS)
#define VIRTUSB_IOCFETCHWORK CTL_CODE(VIRTUSB_IOC_DEVICE_TYPE, 0x0802, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define VIRTUSB_IOCGIVEBACK  CTL_CODE(VIRTUSB_IOC_DEVICE_TYPE, 0x0803, METHOD_NEITHER,  FILE_ANY_ACCESS)
#define VIRTUSB_IOCFETCHDATA CTL_CODE(VIRTUSB_IOC_DEVICE_TYPE, 0x0804, METHOD_NEITHER,  FILE_ANY_ACCESS)

#ifdef __cplusplus
}
#endif

#endif // !VIRTUSB_IOCTL_H
