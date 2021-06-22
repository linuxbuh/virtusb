#pragma once

#pragma unmanaged
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <tchar.h>
#include <setupapi.h>
#include <regstr.h>
#include <cfgmgr32.h>
#include <string.h>
#include <malloc.h>

// from WINDDK (newdev.h):
#define INSTALLFLAG_FORCE           0x00000001      // Force the installation of the specified driver
#define INSTALLFLAG_READONLY        0x00000002      // Do a read-only install (no file copy)
#define INSTALLFLAG_NONINTERACTIVE  0x00000004      // No UI shown at all. API will fail if any UI must be shown.
#define INSTALLFLAG_BITS            0x00000007

#pragma managed
