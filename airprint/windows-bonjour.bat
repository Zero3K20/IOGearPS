@echo off
:: windows-bonjour.bat -- OPTIONAL: advertise the IOGear PS-1206U as an AirPrint
:: printer on Windows using Apple's Bonjour (dns-sd.exe).
::
:: NOTE: This script is only needed for iOS 13 or earlier / macOS 10.15 or earlier.
::       iOS 14+ and macOS 11+ (Big Sur and later) discover and print to the
::       PS-1206U automatically via its built-in Bonjour (_ipp._tcp) -- no PC
::       software or scripts required.
::
:: REQUIREMENTS (only if you need to support older Apple devices)
:: --------------------------------------------------------------
:: Apple Bonjour for Windows must be installed.  It is included with:
::   - iTunes for Windows  (free)  https://www.apple.com/itunes/
::   - Apple Bonjour Print Services for Windows (free)
::     https://support.apple.com/kb/DL999
::
:: After installation dns-sd.exe is usually at:
::   C:\Program Files\Bonjour\dns-sd.exe
::
:: USAGE
:: -----
:: 1. Edit the PRINTER_IP line below with your PS-1206U's IP address.
:: 2. Double-click this file (or run it from a Command Prompt window).
::    Keep the window open -- closing it stops the advertisement.
:: 3. On older iOS/macOS, the printer should appear as an AirPrint printer.
::
:: To run at Windows startup, create a Scheduled Task that runs this script
:: "At startup" with the option "Run whether user is logged on or not".

:: ============================================================
:: EDIT THIS LINE: set PRINTER_IP to your PS-1206U's IP address
set PRINTER_IP=192.168.1.100
:: ============================================================

set SERVICE_NAME=IOGear PS-1206U
set DNS_SD=dns-sd.exe

:: Verify dns-sd.exe is available
where %DNS_SD% >nul 2>&1
if errorlevel 1 (
    set DNS_SD=C:\Program Files\Bonjour\dns-sd.exe
)
if not exist "%DNS_SD%" (
    echo.
    echo  ERROR: dns-sd.exe not found.
    echo.
    echo  Please install one of:
    echo    - iTunes for Windows
    echo    - Apple Bonjour Print Services for Windows
    echo      https://support.apple.com/kb/DL999
    echo.
    pause
    exit /b 1
)

echo.
echo  Advertising "%SERVICE_NAME%" as AirPrint on %PRINTER_IP%:631
echo  (Keep this window open to maintain the advertisement)
echo  Press Ctrl+C to stop.
echo.

:: Register the _ipp._tcp AirPrint service.
:: dns-sd -R <name> <type> <domain> <port> [txtRecord ...]
"%DNS_SD%" -R "%SERVICE_NAME%" _ipp._tcp local 631 ^
    txtvers=1 ^
    qtotal=1 ^
    rp=ipp ^
    ty=IOGear PS-1206U Print Server ^
    adminurl=http://%PRINTER_IP%/ ^
    note=IOGear PS-1206U ^
    priority=25 ^
    product=(IOGear PS-1206U) ^
    pdl=application/octet-stream,application/pdf,image/jpeg,image/png,image/urf ^
    Color=F ^
    Duplex=F ^
    usb_MFG=IOGear ^
    usb_MDL=PS-1206U ^
    URF=none

:: dns-sd will block until Ctrl+C is pressed.
