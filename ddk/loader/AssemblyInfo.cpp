#include "stdafx.h"

using namespace System;
using namespace System::Reflection;
using namespace System::Runtime::CompilerServices;
using namespace System::Runtime::InteropServices;
using namespace System::Security::Permissions;

//
// Allgemeine Informationen �ber eine Assembly werden �ber die folgenden
// Attribute gesteuert. �ndern Sie diese Attributwerte, um die Informationen zu �ndern,
// die mit einer Assembly verkn�pft sind.
//
[assembly:AssemblyTitle("loader")];
[assembly:AssemblyDescription("")];
[assembly:AssemblyConfiguration("")];
[assembly:AssemblyCompany("")];
[assembly:AssemblyProduct("virtusb")];
[assembly:AssemblyCopyright("Copyright � by Michael Singer, 2009-2016")];
[assembly:AssemblyTrademark("")];
[assembly:AssemblyCulture("")];

//
// Versionsinformationen f�r eine Assembly bestehen aus den folgenden vier Werten:
//
//      Hauptversion
//      Nebenversion
//      Buildnummer
//      Revision
//
// Sie k�nnen alle Werte angeben oder f�r die Revisions- und Buildnummer den Standard
// �bernehmen, indem Sie "*" eingeben:

[assembly:AssemblyVersion("1.0.0.1")];

[assembly:ComVisible(false)];

[assembly:CLSCompliant(true)];

[assembly:SecurityPermission(SecurityAction::RequestMinimum, UnmanagedCode = true)];
