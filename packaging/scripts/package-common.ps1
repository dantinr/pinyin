Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Read-PinyinPackage([string]$Root) {
    $rootPath = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
    $manifest = Get-Content -LiteralPath (Join-Path $rootPath 'manifest.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($manifest.format -ne 1 -or $manifest.architecture -ne 'x64' -or $manifest.version -notmatch '^\d+\.\d+\.\d+$') {
        throw 'Unsupported package manifest or version.'
    }
    $null = [version]$manifest.runtimeVersion
    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($file in $manifest.files) {
        if ($file.path -notmatch '^[a-zA-Z0-9_.\-/]+$' -or $file.sha256 -notmatch '^[0-9a-fA-F]{64}$') {
            throw 'Invalid package file record.'
        }
        $path = [IO.Path]::GetFullPath((Join-Path $rootPath $file.path))
        if (-not $path.StartsWith($rootPath + '\', [StringComparison]::OrdinalIgnoreCase) -or -not $seen.Add($path)) {
            throw 'Package file is outside its directory or duplicated.'
        }
        if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or
            (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $file.sha256) {
            throw "Missing or modified package file: $($file.path)"
        }
    }
    foreach ($required in @('private_pinyin_ime.dll', 'private_pinyin.exe', 'private_pinyin_demo.exe',
                           'data/base.tsv', 'vc_redist.x64.exe', 'scripts/register-ime.ps1', 'scripts/unregister-ime.ps1',
                           'scripts/install.ps1', 'scripts/uninstall.ps1', 'scripts/package-common.ps1', 'install.cmd', 'uninstall.cmd')) {
        if (-not $seen.Contains([IO.Path]::GetFullPath((Join-Path $rootPath $required)))) {
            throw "Incomplete package: $required"
        }
    }
    return $manifest
}

function Test-PinyinAdministrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Invoke-PinyinElevated([string]$Script, [string[]]$Arguments = @()) {
    $powershell = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
    $command = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', ('"' + $Script + '"')) + $Arguments
    # This is the installation window explicitly opened by the user.
    $child = Start-Process -FilePath $powershell -Verb RunAs -ArgumentList $command -Wait -PassThru
    return $child.ExitCode
}

function Get-PinyinRegisteredDll {
    $key = 'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Classes\CLSID\{4EA569F1-AF1C-4AA6-8AB8-A6CCAEC31E15}\InprocServer32'
    if (-not (Test-Path -LiteralPath $key)) { return $null }
    return (Get-Item -LiteralPath $key).GetValue('')
}

function Get-PinyinRuntimeVersion {
    $latest = [version]'0.0'
    foreach ($prefix in @('SOFTWARE', 'SOFTWARE\WOW6432Node')) {
        $key = 'Registry::HKEY_LOCAL_MACHINE\' + $prefix + '\Microsoft\VisualStudio\14.0\VC\Runtimes\x64'
        if (-not (Test-Path -LiteralPath $key)) { continue }
        $runtime = Get-ItemProperty -LiteralPath $key
        if ($runtime.Installed -ne 1) { continue }
        $version = [version]$runtime.Version.TrimStart('v', 'V')
        if ($version -gt $latest) { $latest = $version }
    }
    return $latest
}

function Copy-PinyinPackage([string]$Source, [string]$InstallRoot) {
    $sourcePath = [IO.Path]::GetFullPath($Source).TrimEnd('\', '/')
    $manifest = Read-PinyinPackage $sourcePath
    $root = [IO.Path]::GetFullPath($InstallRoot).TrimEnd('\', '/')
    $destination = Join-Path $root $manifest.version
    if ($destination -eq $sourcePath) { return $destination }
    if (Test-Path -LiteralPath $destination) {
        $null = Read-PinyinPackage $destination
        if ((Get-FileHash -LiteralPath (Join-Path $sourcePath 'manifest.json')).Hash -ne
            (Get-FileHash -LiteralPath (Join-Path $destination 'manifest.json')).Hash) {
            throw 'This version is already installed with different files. Use a new release version.'
        }
        return $destination
    }
    $staging = Join-Path $root ($manifest.version + '.staging-' + [Guid]::NewGuid().ToString('N'))
    try {
        $null = New-Item -ItemType Directory -Path $staging -Force
        foreach ($relative in @($manifest.files.path) + @('manifest.json')) {
            $target = Join-Path $staging $relative
            $null = New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force
            Copy-Item -LiteralPath (Join-Path $sourcePath $relative) -Destination $target
        }
        $null = Read-PinyinPackage $staging
        # Both absolute directories must remain inside the selected install root.
        foreach ($path in @($staging, $destination)) {
            if (-not [IO.Path]::GetFullPath($path).StartsWith($root + '\', [StringComparison]::OrdinalIgnoreCase)) {
                throw 'Invalid installation directory.'
            }
        }
        Move-Item -LiteralPath $staging -Destination $destination
        return $destination
    } finally {
        if (Test-Path -LiteralPath $staging) {
            if (-not [IO.Path]::GetFullPath($staging).StartsWith($root + '\', [StringComparison]::OrdinalIgnoreCase)) {
                throw 'Invalid staging directory.'
            }
            Remove-Item -LiteralPath $staging -Recurse -Force
        }
    }
}
