@echo off
rem ============================================================
rem  Compila Optimizalis.exe y lo deja en la carpeta principal
rem  Requiere MinGW-w64 (g++ y windres) en el PATH.
rem  Uso:  compilar.bat        (se para al final)
rem        compilar.bat auto   (sin pausa)
rem ============================================================
cd /d "%~dp0"
if not exist optimizalis.ico powershell -NoProfile -ExecutionPolicy Bypass -File crear_icono.ps1
if not exist obj mkdir obj
rem Especificaciones de g++ sin el manifiesto por defecto de MinGW (se usa el de Optimizalis)
g++ -dumpspecs > obj\specs.orig
if errorlevel 1 goto error
powershell -NoProfile -Command "(Get-Content 'obj\specs.orig' -Raw).Replace('%%{!shared:%%:if-exists(default-manifest.o%%s)}','') | Set-Content 'obj\specs' -Encoding ascii -NoNewline"
windres optimizalis.rc -O coff -o obj\recursos.o
if errorlevel 1 goto error
g++ -specs=obj/specs -std=c++17 -O2 -Wall -municode -mwindows -static -static-libgcc -static-libstdc++ ^
    engine.cpp gui.cpp obj/recursos.o -o "..\Optimizalis.exe" ^
    -lgdiplus -lcomctl32 -ldwmapi -lshell32 -lole32 -luuid -ladvapi32 -lgdi32 -luser32
if errorlevel 1 goto error
echo Compilado correctamente: ..\Optimizalis.exe
if /i not "%~1"=="auto" pause
exit /b 0
:error
echo.
echo ERROR al compilar. Necesitas MinGW-w64 (g++ y windres) en el PATH.
if /i not "%~1"=="auto" pause
exit /b 1