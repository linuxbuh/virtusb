@echo off
if "%1"=="" goto errargs
if "%2"=="" goto errargs

rmdir /s /q %1
rmdir /s /q %2
goto done

:errargs
echo usage: %0 [IntDir] [OutDir]
echo example: %0 objchk_wxp_x86 ..\debug\objchk_wxp_x86\
exit 1

:done
