param(
    [string]$InstallRoot = (Join-Path $env:ProgramFiles 'PrivatePinyin'),
    [switch]$CheckOnly
)
$ErrorActionPreference = 'Stop'
try {
    if (-not [Environment]::Is64BitProcess -or $env:PROCESSOR_ARCHITECTURE -ne 'AMD64') {
        throw 'This package requires Windows x64 and 64-bit PowerShell.'
    }
    . (Join-Path $PSScriptRoot 'package-common.ps1')
    $source = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd('\', '/')
    $manifest = Read-PinyinPackage $source
    if ($CheckOnly) { Write-Output "Validated Private Pinyin $($manifest.version) Windows x64 package."; exit 0 }
    if (-not (Test-PinyinAdministrator)) {
        exit (Invoke-PinyinElevated $PSCommandPath @('-InstallRoot', ('"' + $InstallRoot + '"')))
    }
    $destination = Copy-PinyinPackage $source $InstallRoot
    if ((Get-PinyinRuntimeVersion) -lt [version]$manifest.runtimeVersion) {
        $runtime = Start-Process -FilePath (Join-Path $destination 'vc_redist.x64.exe') -ArgumentList @('/install', '/quiet', '/norestart') -WindowStyle Hidden -Wait -PassThru
        if ($runtime.ExitCode -ne 0 -and $runtime.ExitCode -ne 3010) {
            throw "VC++ runtime installation failed (exit $($runtime.ExitCode))."
        }
        if ($runtime.ExitCode -eq 3010) { Write-Output 'Windows requests a restart to finish the runtime update.' }
    }
    $previous = Get-PinyinRegisteredDll
    try {
        & (Join-Path $destination 'scripts/register-ime.ps1') -DllPath (Join-Path $destination 'private_pinyin_ime.dll')
    } catch {
        $registrationError = $_
        if ($previous -and (Test-Path -LiteralPath $previous -PathType Leaf)) {
            try { & (Join-Path $destination 'scripts/register-ime.ps1') -DllPath $previous }
            catch { Write-Warning 'Could not restore the previous IME registration.' }
        }
        throw $registrationError
    }
    Write-Output "Installed Private Pinyin $($manifest.version) to $destination"
    Write-Output 'Press Win+Space to select the IME. Reopen applications to load this version.'
    Write-Output 'You can delete the downloaded ZIP and extracted package. Personal words are preserved.'
    exit 0
} catch {
    Write-Error $_ -ErrorAction Continue
    exit 1
}
