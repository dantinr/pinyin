param([switch]$CheckOnly)
$ErrorActionPreference = 'Stop'
try {
    if (-not [Environment]::Is64BitProcess -or $env:PROCESSOR_ARCHITECTURE -ne 'AMD64') {
        throw 'This package requires Windows x64 and 64-bit PowerShell.'
    }
    . (Join-Path $PSScriptRoot 'package-common.ps1')
    $root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd('\', '/')
    $manifest = Read-PinyinPackage $root
    if ($CheckOnly) { Write-Output "Validated uninstall entry for Private Pinyin $($manifest.version)."; exit 0 }
    $registered = Get-PinyinRegisteredDll
    # The downloaded ZIP has the same manifest as its installed copy. Find that
    # copy from the existing registration instead of registering the ZIP path.
    $installed = $root
    if ($registered) {
        $installed = Split-Path -Parent $registered
        $active = Read-PinyinPackage $installed
        if ($active.version -ne $manifest.version -or
            (Get-FileHash -LiteralPath (Join-Path $root 'manifest.json')).Hash -ne
            (Get-FileHash -LiteralPath (Join-Path $installed 'manifest.json')).Hash) {
            throw 'A different IME version is active. Run uninstall.cmd from that installed version.'
        }
        if ([IO.Path]::GetFullPath($registered) -ne (Join-Path $installed 'private_pinyin_ime.dll')) {
            throw 'The registered DLL does not match this package.'
        }
    }
    if (-not $registered) { Write-Output 'Private Pinyin is not registered.'; exit 0 }
    if (-not (Test-PinyinAdministrator)) { exit (Invoke-PinyinElevated $PSCommandPath) }
    & (Join-Path $installed 'scripts/unregister-ime.ps1') -DllPath $registered
    Write-Output 'Personal words and the shared VC++ runtime were kept.'
    Write-Output "After closing applications, you can delete this version's directory: $installed"
    exit 0
} catch {
    Write-Error $_ -ErrorAction Continue
    exit 1
}
