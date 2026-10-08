param([string]$DllPath = (Join-Path $PSScriptRoot '..\build\Release\private_pinyin_ime.dll'))
$ErrorActionPreference = 'Stop'
if (-not [Environment]::Is64BitProcess) { throw 'Run this script in 64-bit PowerShell.' }
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Unregistration removes this IME COM/TSF entries under HKLM. Run from an administrator PowerShell.'
}
$dll = (Resolve-Path -LiteralPath $DllPath).Path
$registrar = Join-Path $env:SystemRoot 'System32\regsvr32.exe'
$process = Start-Process -FilePath $registrar -ArgumentList @('/u', '/s', ('"' + $dll + '"')) -WindowStyle Hidden -PassThru -Wait
if ($process.ExitCode -ne 0) { throw "IME unregistration failed (regsvr32 exit $($process.ExitCode))." }
Write-Output 'Unregistered Private Pinyin. Existing applications may retain the loaded DLL until they exit.'
