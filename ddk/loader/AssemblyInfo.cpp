#include "stdafx.h"

using namespace System;
using namespace System::Reflection;
using namespace System::Runtime::CompilerServices;
using namespace System::Runtime::InteropServices;
using namespace System::Security::Permissions;

//
// Allgemeine Informationen über eine Assembly werden über die folgenden
// Attribute gesteuert. Ändern Sie diese Attributwerte, um die Informationen zu ändern,
// die mit einer Assembly verknüpft sind.
//
[assembly:AssemblyTitle("loader")];
[assembly:AssemblyDescription("")];
[assembly:AssemblyConfiguration("")];
[assembly:AssemblyCompany("")];
[assembly:AssemblyProduct("virtusb")];
[assembly:AssemblyCopyright("Copyright © by Michael Singer, 2009-2016")];
[assembly:AssemblyTrademark("")];
[assembly:AssemblyCulture("")];

//
// Versionsinformationen für eine Assembly bestehen aus den folgenden vier Werten:
//
//      Hauptversion
//      Nebenversion
//      Buildnummer
//      Revision
//
// Sie können alle Werte angeben oder für die Revisions- und Buildnummer den Standard
// übernehmen, indem Sie "*" eingeben:

[assembly:AssemblyVersion("1.0.0.1")];

[assembly:ComVisible(false)];

[assembly:CLSCompliant(true)];

[assembly:SecurityPermission(SecurityAction::RequestMinimum, UnmanagedCode = true)];
