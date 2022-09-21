# virtusb
[1] BUILDING KERNEL-DRIVER (virtusb)


[1.1] REQUIREMENTS

I'm using WinXP with SP3 and the WinDDK version 3790.1830 for building the
driver. Other versions may also work. If you encounter any problems, try
using the same as I.

Here is a download-link to the DDK, which I am using:
http://download.microsoft.com/download/9/0/f/90f019ac-8243-48d3-91cf-81fc4093ecfd/1830_usa_ddk.iso
It is called "Windows Server 2003 DDK".


[1.2.1] USING COMMAND-LINE TO BUILD DRIVER

1. Set up a command-line with appropriate environment-variables set, like you
   would for building the examples, which were shipped with the DDK.
2. Change-dir into the virtusb-subdirectory -- that's the only one with a
   makefile in it.
3. Enter "build" or "nmake" or whatever you have to enter in the
   build-environment of your DDK-version. 


[1.2.2] USING VISUAL STUDIO 2005 TO BUILD DRIVER

If you have installed the same version of the DDK as I have, then you
should be able to build the driver by just clicking the "Build" button in
Visual Studio 2005. But before this works, you have to set the
environment-variable "DDKDIR" to the directory of your DDK-installation.
Don't forget to restart Visual Studio after you have set the
environment-variable, if you have started it before.
