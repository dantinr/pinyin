param([string]$DllPath = (Join-Path $PSScriptRoot '..\build\Release\private_pinyin_ime.dll'))
$ErrorActionPreference = 'Stop'
if (-not [Environment]::Is64BitProcess) { throw 'Run this script in 64-bit PowerShell.' }
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Registration writes this IME COM/TSF entries under HKLM. Run from an administrator PowerShell.'
}
$dll = (Resolve-Path -LiteralPath $DllPath).Path
$dictionary = Join-Path (Split-Path -Parent $dll) 'data\base.tsv'
if (-not (Test-Path -LiteralPath $dictionary -PathType Leaf)) { throw "Missing dictionary: $dictionary" }
$registrar = Join-Path $env:SystemRoot 'System32\regsvr32.exe'
$process = Start-Process -FilePath $registrar -ArgumentList @('/s', ('"' + $dll + '"')) -WindowStyle Hidden -PassThru -Wait
if ($process.ExitCode -ne 0) { throw "IME registration failed (regsvr32 exit $($process.ExitCode))." }
Write-Output 'Registered Private Pinyin (x64). Choose it from the Windows input selector; reopen test applications if needed.'
Write-Output 'The DLL and data directory must stay at this location until unregistered. No default input method was changed.'
