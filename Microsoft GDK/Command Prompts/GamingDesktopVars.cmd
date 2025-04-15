@echo off
REM ====================================================================
REM Set path variables
REM ==========================================================================

IF /I "%1"=="GamingDesktopVS2017" (
  REM Deprecated, but present for backward compatibility among scripts.
  CALL :SetVS2017
)ELSE IF /I "%1"=="GamingDesktopVS2019" (
  CALL :SetVS2019
)ELSE IF /I "%1"=="GamingDesktopVS2022" (
  CALL :SetVS2022
)ELSE (
  ECHO You must specify GamingDesktopVS2017, GamingDesktopVS2019 or GamingDesktopVS2022 on the command line.
  EXIT /B 1
)

IF "%GRDKEDITION%"=="" (
    IF NOT "%2"=="" (
       SET GRDKEDITION=%2
    )ELSE (
        REM Get the most recent GRDK edition installed.
        CALL :GetLatestGRDK
    )
)

IF %DTARGETVSVER% GEQ 15.0 (

    SET GRDKVSPRERELEASE=-prerelease

    REM Possible values include Microsoft.VisualStudio.Product.Professional,
    REM Microsoft.VisualStudio.Product.Community and Microsoft.VisualStudio.Product.Enterprise
    REM separated by spaces for multiple products.
    IF "%GRDKVSPRODUCTS%"=="" (
        IF NOT "%3"=="" (
            SET GRDKVSPRODUCTS= -products %3
        )
    ) ELSE (
        SET GRDKVSPRODUCTS= -products %GRDKVSPRODUCTS%
    )

    IF "%GRDKVSVERSION%"=="" (
        IF NOT "%4"=="" (
            IF NOT "%4"=="NOPRE" (
                SET GRDKVSVERSION= -version %4
            )
        )
    ) ELSE (
        SET GRDKVSVERSION= -version %GRDKVSVERSION%
    )

    IF "%GRDKVSNOPRERELEASE%"=="" (
        IF "%4"=="NOPRE" (
            SET GRDKVSPRERELEASE=
        )ELSE (
            IF "%5"=="NOPRE" (
                SET GRDKVSPRERELEASE=
            )
        )
    ) ELSE (
        SET GRDKVSPRERELEASE=
    )
)

IF "%GRDKEDITION%"=="" (
    REM If no GRDK edition found then assume Durango.
    SET XDKEDITION=000000
)

Echo Setting environment for using Microsoft Desktop %DTARGETVS% Gaming Tools
TITLE Desktop %DTARGETVS% Gaming Command Prompt

IF "%GameDK%"=="" (
    CALL :GetGDKInstallPath
)
IF "%GameDK%"=="" (
    ECHO Microsoft Gaming Development Kit directory is not found on this machine.
    EXIT /B 1
)

IF NOT "%GRDKEDITION%"=="000000" (
    IF "%GamingGRDKBuild%"=="" (
        CALL :GetGRDKBuildInstallPath
    )

    IF "%GamingGRDKEditionVersionFriendlyName%"=="" (
        CALL :GetGRDKEditionVersionFriendlyName
    )
)
    
IF NOT "%GRDKEDITION%"=="000000" (
    IF "%GamingGRDKBuild%"=="" (
        ECHO Microsoft Gaming Development Kit build for %GRDKEDITION% is not found on this machine.
        EXIT /B 1
    )

    IF NOT "%GamingGRDKEditionVersionFriendlyName%"=="" (
        TITLE %GamingGRDKEditionVersionFriendlyName% Desktop %DTARGETVS% Gaming Command Prompt
    )
)

IF "%WindowsSDKDir%" == "" (
    CALL :GetWindowsSDKDir
)

IF "%WindowsSDKDir%"=="" (
    ECHO Warning: Windows Software Development Kit directory is not found on this machine.
)

IF %GRDKEDITION% LSS 200500 (
    IF "%GamingWindowsSDKDir%" == "" (
        CALL :GetGamingWindowsSDKDir
    )
)

IF %GRDKEDITION% LSS 200500 (
    IF "%GamingWindowsSDKDir%"=="" (
        ECHO Warning: Gaming Windows Software Development Kit directory is not found on this machine.
    )
)

IF NOT "%WindowsSDKDir%" == "" (
    if "%WindowsSDKVersion%" == "" (
        CALL :GetWindowsSDKVersion
    )
)

IF NOT "%WindowsSDKDir%" == "" (
    IF "WindowsSDKVersion"=="" (
        ECHO Warning: Windows Software Development Kit version not specified.
    )
)

IF %GRDKEDITION% LSS 200500 (
    IF NOT "%GamingWindowsSDKDir%" == "" (
        if "%GamingWindowsSDKVersion%" == "" (
            CALL :GetWindowsSDKVersion Gaming
        )
    )
)

IF %GRDKEDITION% LSS 200500 (
    IF NOT "%GamingWindowsSDKDir%" == "" (
        IF "GamingWindowsSDKVersion"=="" (
            ECHO Warning: Gaming Windows Software Development Kit version not specified.
        ) else (
            Set "WindowsIncludeRoot=%GamingWindowsSDKDir%Include\%GamingWindowsSDKVersion%\"
            Set "WindowsLibRoot=%GamingWindowsSDKDir%Lib\%GamingWindowsSDKVersion%\"
        )
    )
) else (
    Set "WindowsIncludeRoot=%WindowsSDKDir%Include\%WindowsSDKVersion%\"
    Set "WindowsLibRoot=%WindowsSDKDir%Lib\%WindowsSDKVersion%\"
)

IF "%VSInstallDir%"=="" (
    CALL :GetVSInstallDir
)
IF "%VSInstallDir%"=="" (
    ECHO Warning: %DTARGETVS% is not found on this machine.
)

IF "%VCInstallDir%"=="" (
    if "%VCToolsVersion%"=="" (
        CALL :GetVCToolsVersion
    )

    if "%VCToolsRedistVersion%"=="" (
        CALL :GetVCToolsRedistVersion
    )

    CALL :GetVCInstallDir
    CALL :GetVCToolsInstallDir
    CALL :GetVCToolsRedistDir
)
IF "%VCInstallDir%"=="" (
    ECHO Warning: %DTARGETVS% VC is not found on this machine.
)

IF "%FrameworkDir%"=="" (
    CALL :GetFrameworkDir
)
IF "%FrameworkDir%"=="" (
    ECHO Warning: .Net Framework is not found on this machine.
)

IF "%FrameworkVersion%"=="" (
    CALL :GetFrameworkVer
)
IF "%FrameworkVersion%"=="" (
    IF EXIST "%FrameworkDir%v4.0.30319" (
        SET "FrameworkVersion=v4.0.30319"
    )
)

IF "%MSBuildInstallDir15%"=="" (
    CALL :GetMSBuildInstallDir15
)

IF "%MSBuildInstallDir16%"=="" (
    CALL :GetMSBuildInstallDir16
)

IF "%MSBuildInstallDir17%"=="" (
    CALL :GetMSBuildInstallDir17
)

REM Default to x64
if "%VSCMD_ARG_HOST_ARCH%" == "" (
    set "VSCMD_ARG_HOST_ARCH=x64"
)

REM ==========================================================================
REM Set path 
REM ==========================================================================

IF EXIST "%VSInstallDir%Team Tools\Performance Tools" (
    SET "PATH=%VSInstallDir%Team Tools\Performance Tools\x64;%VSInstallDir%Team Tools\Performance Tools;%PATH%"
)

IF EXIST "%VSInstallDir%" (
    SET "PATH=%VSInstallDir%Common7\Tools;%VSInstallDir%Common7\IDE;%PATH%"
)

IF EXIST "%VSInstallDir%Common7\Tools\vsdevcmd\ext\cmake.bat" (
    CALL "%VSInstallDir%Common7\Tools\vsdevcmd\ext\cmake.bat"
)

IF EXIST "%VSInstallDir%Common7\Tools\vsdevcmd\ext\clang_cl.bat" (
    CALL "%VSInstallDir%Common7\Tools\vsdevcmd\ext\clang_cl.bat"
)

IF EXIST "%VCInstallDir%" (
    SET "PATH=%VSInstallDir%Common7\IDE\VC\vcpackages\;%PATH%"
)

IF EXIST "%FrameworkDir%" (
    SET "PATH=%FrameworkDir%%FrameworkVersion%;%PATH%"
)

IF EXIST "%WindowsSDKDir%bin\%WindowsSDKVersion%\x64" (
    SET "PATH=%WindowsSDKDir%bin\%WindowsSDKVersion%\x64;%PATH%"
)

SET "PATH=%GameDK%bin;%PATH%"

IF NOT "%GRDKEDITION%"=="000000" (
    SET "PATH=%GamingGRDKBuild%bin;%PATH%"
)

IF EXIST "%PIXPath%" (
    SET "PATH=%PIXPath%;%PATH%"
)

REM Normal compiler for VS2017/GXDL.
IF EXIST "%VCToolsInstallDir%" (
    SET "PATH=%VCToolsInstallDir%bin\Hostx64\x64;%PATH%"
)

REM ==========================================================================
REM Set Include
REM ==========================================================================

IF EXIST "%VCToolsInstallDir%" (
    SET "INCLUDE=%VCToolsInstallDir%INCLUDE;%INCLUDE%"
)
SET "INCLUDE=%GamingGRDKBuild%gamekit\include;%INCLUDE%"

IF EXIST "%WindowsIncludeRoot%" (
    SET "INCLUDE=%WindowsIncludeRoot%um;%WindowsIncludeRoot%shared;%WindowsIncludeRoot%winrt;%WindowsIncludeRoot%cppwinrt;%WindowsIncludeRoot%ucrt;%INCLUDE%"
)

REM ==========================================================================
REM Set Lib
REM ==========================================================================
SET "LIB=%GamingGRDKBuild%gamekit\lib\amd64;%VCToolsInstallDir%\lib\x64;%LIB%"

IF EXIST "%WindowsLibRoot%" (
    SET "LIB=%WindowsLibRoot%um\x64;%WindowsLibRoot%ucrt\x64;%LIB%"
)

REM ==========================================================================
REM Set LibPath
REM ==========================================================================
IF EXIST "%VCToolsInstallDir%" (
    SET "LIBPATH=%VCToolsInstallDir%LIB\x64;%LIBPATH%"
)
IF EXIST "%FrameworkDir%" (
    SET "LIBPATH=%FrameworkDir%%FrameworkVersion%;%LIBPATH%"
)

SET Platform=Gaming.Desktop.x64

REM ==========================================================================
REM Set the MSBuild path after everything else so that it appears first in the
REM path. VS2017 must not accidentally target msbuild.exe in the .NET
REM Framework since it stopped shipping there in dev12 and later.
REM ==========================================================================
 IF /I "%DPLATFORM%"=="VS2017" (
    IF EXIST "%MSBuildInstallDir15%" (
        SET "PATH=%MSBuildInstallDir15%;%PATH%"
    )
)ELSE IF /I "%DPLATFORM%"=="VS2019" (
    IF EXIST "%MSBuildInstallDir16%" (
        SET "PATH=%MSBuildInstallDir16%;%PATH%"
    )
)ELSE IF /I "%DPLATFORM%"=="VS2022" (
    IF EXIST "%MSBuildInstallDir17%" (
        SET "PATH=%MSBuildInstallDir17%;%PATH%"
    )
)

SET CommandPromptType=Native
SET VisualStudioVersion=%DTARGETVSVER%
CD /D "%GameDK%bin"

GOTO :EOF

:GetVSInstallDir

set VSWHERELOCATION="%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

if not exist %VSWHERELOCATION% (
    Set LegacyVSLocation="true"
)

IF /I "%DPLATFORM%"=="VS2017" (
    FOR /f "tokens=1,2*" %%a in ('reg query "HKLM\Software\Microsoft\GDK\%GRDKEDITION%\GRDK" /v "VSIXEditionID" /reg:32 2^>NUL') DO SET GamingGRDKVSIXID=%%c
)ELSE IF /I "%DPLATFORM%"=="VS2019" (
    FOR /f "tokens=1,2*" %%a in ('reg query "HKLM\Software\Microsoft\GDK\%GRDKEDITION%\GRDK" /v "VSIX2019EditionID" /reg:32 2^>NUL') DO SET GamingGRDKVSIXID=%%c
)ELSE IF /I "%DPLATFORM%"=="VS2022" (
    FOR /f "tokens=1,2*" %%a in ('reg query "HKLM\Software\Microsoft\GDK\%GRDKEDITION%\GRDK" /v "VSIX2022EditionID" /reg:32 2^>NUL') DO SET GamingGRDKVSIXID=%%c
)

IF defined LegacyVSLocation (
    FOR /f "tokens=1,2*" %%a in ('reg query "HKLM\Software\Microsoft\VisualStudio\SxS\VS7" /v "%DTARGETVSVER%" /reg:32 2^>NUL') DO SET VSInstallDir=%%c
) ELSE (
    FOR /f "usebackq tokens=1* delims=: " %%i in (`%VSWHERELOCATION% -latest -requires %GamingGRDKVSIXID% %GRDKVSVERSION% %GRDKVSPRODUCTS% %GRDKVSPRERELEASE%`) do (
        IF /i "%%i"=="installationPath" set VSInstallDir=%%j
    )
)

IF defined VSInstallDir IF not "!VSInstallDir:~-1!"=="\" set VSInstallDir=%VSInstallDir%\

GOTO :EOF

:GetVCInstallDir
SET "VCInstallDir=%VSInstallDir%VC\"
GOTO :EOF

:GetVCToolsVersion
set VCToolsDefaultConfigFile="%VSInstallDir%VC\Auxiliary\Build\Microsoft.VCToolsVersion.default.txt"

if not exist %VCToolsDefaultConfigFile% (
    ECHO Warning: Could not find default VC++ tools config file %VCToolsDefaultConfigFile%.
    GOTO :EOF
)

for /F %%A in ('type %VCToolsDefaultConfigFile%') do set VCToolsVersion=%%A

if "%VCToolsVersion%"=="" (
    ECHO Warning: Could not determine default VC++ tools version.
)
GOTO :EOF

:GetVCToolsInstallDir
if "%VCToolsVersion%" NEQ "" (
    SET "VCToolsInstallDir=%VSInstallDir%VC\Tools\MSVC\%VCToolsVersion%\"
)ELSE (
    ECHO Warning: Could not set VC Tools Install Directory.
)
GOTO :EOF

:GetVCToolsRedistVersion
set VCToolsRedistDefaultConfigFile="%VSInstallDir%VC\Auxiliary\Build\Microsoft.VCRedistVersion.default.txt"

if not exist %VCToolsRedistDefaultConfigFile% (
    ECHO Warning: Could not find default VC++ redist config file %VCToolsRedistDefaultConfigFile%.
    GOTO :EOF
)

for /F %%A in ('type %VCToolsRedistDefaultConfigFile%') do set VCToolsRedistVersion=%%A

if "%VCToolsRedistVersion%"=="" (
    ECHO Warning: Could not determine default VC++ redist version.
)
GOTO :EOF

:GetVCToolsRedistDir
if "%VCToolsRedistVersion%" NEQ "" (
    SET "VCToolsRedistDir=%VSInstallDir%VC\Redist\MSVC\%VCToolsRedistVersion%\"
)ELSE (
    ECHO Warning: Could not set VC Redist Directory.
)
GOTO :EOF

:GetLatestGRDK
REM Sets GRDKEDITION to latest GRDK's edition number.
FOR /f "tokens=1,2*" %%a in ('reg query "HKLM\Software\Microsoft\GDK" /v "GRDKLatest" /reg:32 2^>NUL') DO SET GRDKEDITION=%%c
GOTO :EOF

:GetGRDKBuildInstallPath
REM Sets GamingGRDKBuild to location of Gaming GRDK build tools.
FOR /f "tokens=1,2*" %%a in ('reg query "HKLM\Software\Microsoft\GDK\%GRDKEDITION%\GRDK" /v "InstallPath" /reg:32 2^>NUL') DO SET GamingGRDKBuild=%%c
GOTO :EOF

:GetGRDKEditionVersionFriendlyName
REM Sets GamingGRDKEditionVersionFriendlyName to the friendly version name of the selected build tools.
FOR /f "tokens=1,2*" %%a in ('reg query "HKLM\Software\Microsoft\GDK\%GRDKEDITION%\GRDK" /v "EditionVersionFriendlyName" /reg:32 2^>NUL') DO SET GamingGRDKEditionVersionFriendlyName=%%c
GOTO :EOF

:GetGDKInstallPath
REM Sets GameDK to location of GRDK
FOR /f "tokens=1,2*" %%a in ('reg query "HKLM\Software\Microsoft\GDK" /v "GRDKInstallPath" /reg:32 2^>NUL') DO SET GameDK=%%c
GOTO :EOF

:GetMSBuildInstallDir15
REM Sets MSBuild15 directory.
IF /I "%DTARGETVSVER%" GEQ "15.0" (
  SET "MSBuildInstallDir15=%VSInstallDir%MSBuild\15.0\Bin"
)
GOTO :EOF

:GetMSBuildInstallDir16
REM Sets MSBuild16 directory.
IF /I "%DTARGETVSVER%" GEQ "16.0" (
  SET "MSBuildInstallDir16=%VSInstallDir%MSBuild\Current\Bin"
)
GOTO :EOF

:GetMSBuildInstallDir17
REM Sets MSBuild17 directory.
IF /I "%DTARGETVSVER%" GEQ "17.0" (
  SET "MSBuildInstallDir17=%VSInstallDir%MSBuild\Current\Bin"
)
GOTO :EOF

:GetFrameworkDir
FOR /f "tokens=1,2*" %%a in ('reg query "HKLM\Software\Microsoft\.NETFramework" /v "InstallRoot" /reg:32 2^>NUL') DO SET FrameworkDir=%%c
GOTO :EOF

:GetFrameworkVer
FOR /f "tokens=1,2*" %%a in ('reg query "HKLM\Software\Microsoft\VisualStudio\SxS\VC7" /v "FrameworkVer32" /reg:32 2^>NUL') DO SET FrameworkVersion=%%c
GOTO :EOF

:SetVS2017
SET DPLATFORM=VS2017
SET DTARGETVS=Visual Studio 2017
SET DTARGETVSVER=15.0
GOTO :EOF

:SetVS2019
SET DPLATFORM=VS2019
SET DTARGETVS=Visual Studio 2019
SET DTARGETVSVER=16.0
GOTO :EOF

:SetVS2022
SET DPLATFORM=VS2022
SET DTARGETVS=Visual Studio 2022
SET DTARGETVSVER=17.0
GOTO :EOF

:GetWindowsSDKDir
@REM Get Windows 10 SDK installed folder
for /F "tokens=1,2*" %%i in ('reg query "HKLM\SOFTWARE\Wow6432Node\Microsoft\Microsoft SDKs\Windows\v10.0" /v "InstallationFolder" 2^>NUL') DO (
    if "%%i"=="InstallationFolder" (
        SET WindowsSdkDir=%%~k
    )
)

GOTO :EOF

:GetGamingWindowsSDKDir
If exist "%GameDK%%GRDKEDITION%\WindowsSDK" (
 Set "GamingWindowsSDKDir=%GameDK%%GRDKEDITION%\WindowsSDK\"
)

IF not defined GamingWindowsSDKDir (
    Set "GamingWindowsSDKDir=%WindowsSdkDir%"
)

GOTO :EOF

:GetWindowsSDKVersion
REM From VS 2017 winsdk.bat GetWin10SdkDirHelper
@REM get windows 10 sdk version number
setlocal enableDelayedExpansion

IF "%1" == "Gaming" (
    set "IsGaming=1" 
)

if "%IsGaming%" == "1" (
    Set "LocalWindowsSDKDir=%GamingWindowsSDKDir%"
) else (
    Set "LocalWindowsSDKDir=%WindowsSDKDir%"
)

@REM Due to the SDK installer changes beginning with the 10.0.15063.0 (RS2 SDK), there is a chance that the
@REM Windows SDK installed may not have the full set of bits required for all application scenarios.
@REM We check for the existence of a file we know to be included in the "App" and "Desktop" portions
@REM of the Windows SDK, depending on the Developer Command Prompt's -app_platform configuration.
@REM If "windows.h" (UWP) or "winsdkver.h" (Desktop) are not found, the directory will be skipped as
@REM a candidate default value for [WindowsSdkDir].
set __check_file=winsdkver.h
if /I "%VSCMD_ARG_APP_PLAT%"=="UWP" set __check_file=Windows.h

if not "%LocalWindowsSDKDir%"=="" for /f %%i IN ('dir "%LocalWindowsSDKDir%include\" /b /ad-h /on') DO (
    @REM Skip if Windows.h|winsdkver (based upon -app_platform configuration) is not found in %%i\um.  
    if EXIST "%LocalWindowsSDKDir%include\%%i\um\%__check_file%" (
        set result=%%i
        if "!result:~0,3!"=="10." (
            set SDK=!result!
            if "!result!"=="%VSCMD_ARG_WINSDK%" set findSDK=1
        )
    )
)

if "%findSDK%"=="1" set SDK=%VSCMD_ARG_WINSDK%
set LocalWindowsSDKVersion=%SDK%

endlocal & if "%IsGaming%" == "1" (set "GamingWindowsSDKVersion=%SDK%") else (set "WindowsSDKVersion=%SDK%")
if not "%IsGaming%" == "1" (
  if not "%VSCMD_ARG_WINSDK%"=="" (
    @REM if the user specified a version of the SDK and it wasn't found, then use the
    @REM user-specified version to set environment variables.

    if not "%VSCMD_ARG_WINSDK%"=="%WindowsSDKVersion%" (
      if "%VSCMD_DEBUG%" GEQ "1" echo [DEBUG:%~nx0] specified /winsdk=%VSCMD_ARG_WINSDK% was not found or was incomplete
      set WindowsSDKVersion=%VSCMD_ARG_WINSDK%
      set WindowsSDKNotFound=1
    )
  ) else (
    @REM if no full Windows 10 SDKs were found, unset WindowsSDKDir and exit with error.

    if "%WindowsSDKVersion%"=="" (
      set WindowsSDKNotFound=1
      set WindowsSDKDir=
    )
  )
)

GOTO :EOF
:end
