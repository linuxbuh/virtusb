@echo off
if "%DDKDIR%"=="" goto envnotset
echo DDKDIR=%DDKDIR%
if "%1"=="" goto errargs
if "%2"=="" goto errargs

pushd %DDKDIR%
call bin\setenv.bat %DDKDIR% %3 %4 %5 %6
popd
nmake
if exist %2 goto existout
mkdir %2
:existout
if exist %1\amd64\vusbvhci.sys copy /y %1\amd64\vusbvhci.sys %2\
if exist %1\i386\vusbvhci.sys  copy /y %1\i386\vusbvhci.sys %2\
if exist %1\amd64\vusbvhci.pdb copy /y %1\amd64\vusbvhci.pdb %2\
if exist %1\i386\vusbvhci.pdb  copy /y %1\i386\vusbvhci.pdb %2\
if exist vusbvhci.inf          copy /y vusbvhci.inf %2\
goto done

:envnotset
echo DDKDIR environment variable not set
exit 1

:errargs
echo usage: %0 [IntDir] [OutDir] [ArgsForDdkSetEnv]
echo example: %0 objchk_wxp_x86 ..\debug\objchk_wxp_x86\ chk wxp
exit 1

:done
