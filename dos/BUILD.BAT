@echo off
rem ====================================================================
rem  PROMPRO v1.6 - Turbo C++ 1.0 build (MS-DOS, medium model)
rem
rem  usage:  BUILD [TCDIR]        TCDIR defaults to C:\TC
rem
rem  The program is too big for one 64K code segment, so the same source
rem  file is compiled four times (-DPP_PART=1..4); each pass keeps one
rem  quarter of the code, and the four objects are linked together.
rem ====================================================================
set TCD=C:\TC
if not "%1"=="" set TCD=%1
set OPT=-mm -d -O -I%TCD%\INCLUDE -L%TCD%\LIB

%TCD%\BIN\TCC %OPT% -c -DPP_PART=1 -oP1.OBJ PROMPRO.C
if errorlevel 1 goto fail
%TCD%\BIN\TCC %OPT% -c -DPP_PART=2 -oP2.OBJ PROMPRO.C
if errorlevel 1 goto fail
%TCD%\BIN\TCC %OPT% -c -DPP_PART=3 -oP3.OBJ PROMPRO.C
if errorlevel 1 goto fail
%TCD%\BIN\TCC %OPT% -c -DPP_PART=4 -oP4.OBJ PROMPRO.C
if errorlevel 1 goto fail

%TCD%\BIN\TCC %OPT% -ePROMPRO P1.OBJ P2.OBJ P3.OBJ P4.OBJ
if errorlevel 1 goto fail

del P1.OBJ
del P2.OBJ
del P3.OBJ
del P4.OBJ
echo.
echo Built PROMPRO.EXE
goto end

:fail
echo.
echo *** BUILD FAILED ***

:end
