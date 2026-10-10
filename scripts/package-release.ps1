param(
    [string]$BuildDir = 'build-package',
    [string]$OutputDir = 'out/releases',
    [string]$CMakePath = '',
    [string]$RedistPath = '',
    [string]$ReleaseTag = ''
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Invoke-PackCommand([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "Command failed (exit $LASTEXITCODE): $Program" }
}
function Assert-PackX64([string]$Path) {
    $reader = [IO.BinaryReader]::new([IO.File]::OpenRead($Path))
    try {
        if ($reader.ReadUInt16() -ne 0x5a4d) { throw "Not a PE binary: $Path" }
        $reader.BaseStream.Position = 0x3c
        $offset = $reader.ReadInt32()
        if ($offset -lt 64 -or $offset -gt $reader.BaseStream.Length - 6) { throw "Invalid PE header: $Path" }
        $reader.BaseStream.Position = $offset
        if ($reader.ReadUInt32() -ne 0x4550 -or $reader.ReadUInt16() -ne 0x8664) { throw "Not an x64 binary: $Path" }
    } finally { $reader.Dispose() }
}

$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$build = if ([IO.Path]::IsPathRooted($BuildDir)) { [IO.Path]::GetFullPath($BuildDir) } else { [IO.Path]::GetFullPath((Join-Path $repo $BuildDir)) }
$output = if ([IO.Path]::IsPathRooted($OutputDir)) { [IO.Path]::GetFullPath($OutputDir) } else { [IO.Path]::GetFullPath((Join-Path $repo $OutputDir)) }
if (-not [Environment]::Is64BitProcess -or $env:PROCESSOR_ARCHITECTURE -ne 'AMD64') {
    throw 'Packaging requires Windows x64 and 64-bit PowerShell.'
}
$pwsh = Get-Command pwsh -ErrorAction SilentlyContinue
if (-not $pwsh) { throw 'Install PowerShell 7 to run the CLI integration tests before packaging.' }
if (-not $CMakePath) {
    $cmake = Get-Command cmake -ErrorAction SilentlyContinue
    if ($cmake) { $CMakePath = $cmake.Source }
    else {
        $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
        if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Install Visual Studio C++ build tools and CMake.' }
        $vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if (-not $vs) { throw 'Visual Studio C++ build tools were not found.' }
        $CMakePath = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    }
}
$CMakePath = (Get-Command $CMakePath -ErrorAction Stop).Source
$ctest = Join-Path (Split-Path -Parent $CMakePath) 'ctest.exe'
if (-not (Test-Path -LiteralPath $ctest)) { throw 'ctest.exe must be installed alongside cmake.exe.' }

# Use an isolated build; never overwrite the DLL currently registered in Windows.
$key = 'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Classes\CLSID\{4EA569F1-AF1C-4AA6-8AB8-A6CCAEC31E15}\InprocServer32'
if (Test-Path -LiteralPath $key) {
    $registered = (Get-Item -LiteralPath $key).GetValue('')
    if ($registered -and [IO.Path]::GetFullPath($registered).StartsWith($build.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'The selected build directory contains the registered DLL. Choose a separate BuildDir.'
    }
}
Invoke-PackCommand $CMakePath @('-S', $repo, '-B', $build, '-A', 'x64', '-DBUILD_TESTING=ON', ('-DPWSH_EXECUTABLE=' + $pwsh.Source))
$cache = Get-Content -LiteralPath (Join-Path $build 'CMakeCache.txt') -Raw
$version = [regex]::Match($cache, '(?m)^CMAKE_PROJECT_VERSION:STATIC=(\d+\.\d+\.\d+)\s*$').Groups[1].Value
if (-not $version) { throw 'Cannot read the project version from CMake.' }
$sourceCommit = (& git -C $repo rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Cannot read the source commit.' }
$sourceDirty = !!(& git -C $repo status --porcelain)
if ($ReleaseTag) {
    if ($ReleaseTag -cne ('v' + $version)) { throw "ReleaseTag must match CMake version: v$version" }
    if ($sourceDirty) { throw 'Commit source changes before building a tagged release.' }
    $tagCommit = & git -C $repo rev-parse --verify ($ReleaseTag + '^{commit}') 2>$null
    if ($LASTEXITCODE -ne 0 -or $tagCommit -ne $sourceCommit) { throw 'ReleaseTag must exist and point to this source commit.' }
}
Invoke-PackCommand $CMakePath @('--build', $build, '--config', 'Release', '--parallel', '4')
Invoke-PackCommand $ctest @('--test-dir', $build, '-C', 'Release', '--output-on-failure')

if (-not $RedistPath) {
    $vs = [regex]::Match($cache, '(?m)^CMAKE_GENERATOR_INSTANCE:INTERNAL=(.+)$').Groups[1].Value.Trim()
    if (-not $vs) { throw 'Specify RedistPath for non-Visual Studio generators.' }
    $redistRoot = Join-Path $vs 'VC\Redist\MSVC'
    $versions = Get-ChildItem -LiteralPath $redistRoot -Directory | Where-Object { $_.Name -match '^\d+\.\d+\.\d+$' } |
        Sort-Object { [version]$_.Name } -Descending
    foreach ($directory in $versions) {
        $candidate = Join-Path $directory.FullName 'vc_redist.x64.exe'
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { $RedistPath = $candidate; break }
    }
    if (-not $RedistPath) { throw 'VC++ x64 offline redistributable not found; specify RedistPath.' }
}
$redist = Get-Item -LiteralPath $RedistPath
$signature = Get-AuthenticodeSignature -LiteralPath $redist.FullName
if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch '(CN|O)=Microsoft Corporation(,|$)') {
    throw 'The redistributable must have a valid Microsoft Authenticode signature.'
}
$runtimeVersion = $redist.VersionInfo.FileVersion
$null = [version]$runtimeVersion
$runtimeDir = Join-Path $build 'Release'
$dllVersion = (Get-Item -LiteralPath (Join-Path $runtimeDir 'private_pinyin_ime.dll')).VersionInfo.ProductVersion
if ($dllVersion -ne $version) { throw 'DLL resource version does not match the project version.' }
$packageName = "private-pinyin-$version-windows-x64"
$workspace = Join-Path $output ('.package-' + [Guid]::NewGuid().ToString('N'))
$stage = Join-Path $workspace $packageName
try {
    $null = New-Item -ItemType Directory -Path $stage -Force
    # Explicit payload: no build trees, tests, PDBs or personal dictionaries.
    $sources = [ordered]@{
        'private_pinyin_ime.dll' = (Join-Path $runtimeDir 'private_pinyin_ime.dll')
        'private_pinyin.exe' = (Join-Path $runtimeDir 'private_pinyin.exe')
        'private_pinyin_demo.exe' = (Join-Path $runtimeDir 'private_pinyin_demo.exe')
        'private_pinyin_settings.exe' = (Join-Path $runtimeDir 'private_pinyin_settings.exe')
        'data/base.tsv' = (Join-Path $repo 'data/base.tsv')
        'data/README.md' = (Join-Path $repo 'data/README.md')
        'docs/agent-dictionaries.md' = (Join-Path $repo 'docs/agent-dictionaries.md')
        'scripts/register-ime.ps1' = (Join-Path $repo 'scripts/register-ime.ps1')
        'scripts/unregister-ime.ps1' = (Join-Path $repo 'scripts/unregister-ime.ps1')
        'scripts/install.ps1' = (Join-Path $repo 'packaging/scripts/install.ps1')
        'scripts/uninstall.ps1' = (Join-Path $repo 'packaging/scripts/uninstall.ps1')
        'scripts/package-common.ps1' = (Join-Path $repo 'packaging/scripts/package-common.ps1')
        'install.cmd' = (Join-Path $repo 'packaging/install.cmd')
        'uninstall.cmd' = (Join-Path $repo 'packaging/uninstall.cmd')
        'README.txt' = (Join-Path $repo 'packaging/README.txt')
        'vc_redist.x64.exe' = $redist.FullName
    }
    $files = foreach ($entry in $sources.GetEnumerator()) {
        $target = Join-Path $stage $entry.Key
        $null = New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force
        Copy-Item -LiteralPath $entry.Value -Destination $target
        [ordered]@{ path = $entry.Key; sha256 = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant() }
    }
    foreach ($binary in @('private_pinyin_ime.dll', 'private_pinyin.exe', 'private_pinyin_demo.exe', 'private_pinyin_settings.exe')) { Assert-PackX64 (Join-Path $stage $binary) }
    $manifest = [ordered]@{
        format = 1; version = $version; architecture = 'x64'; sourceCommit = $sourceCommit
        sourceDirty = $sourceDirty; runtimeVersion = $runtimeVersion; files = @($files)
    }
    [IO.File]::WriteAllText((Join-Path $stage 'manifest.json'), ($manifest | ConvertTo-Json -Depth 5), [Text.UTF8Encoding]::new($false))
    $zip = Join-Path $output ($packageName + '.zip')
    Compress-Archive -LiteralPath $stage -DestinationPath $zip -CompressionLevel Optimal -Force
    # Exercise the contents of the ZIP, including paths containing spaces.
    $verify = Join-Path $workspace 'verify path'
    Expand-Archive -LiteralPath $zip -DestinationPath $verify
    $extracted = Join-Path $verify $packageName
    $powershell = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
    foreach ($script in @('install.ps1', 'uninstall.ps1')) {
        Invoke-PackCommand $powershell @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $extracted ('scripts/' + $script)), '-CheckOnly')
    }
    Invoke-PackCommand $powershell @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $repo 'tests/package_tests.ps1'), '-PackageDir', $extracted)
    Invoke-PackCommand (Join-Path $extracted 'private_pinyin.exe') @('--sentence', 'wozaibeijingshangban')
    Invoke-PackCommand (Join-Path $runtimeDir 'tsf_com_tests.exe') @((Join-Path $extracted 'private_pinyin_ime.dll'))
    $hash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
    [IO.File]::WriteAllText(($zip + '.sha256'), ($hash + '  ' + [IO.Path]::GetFileName($zip) + "`n"), [Text.UTF8Encoding]::new($false))
    Write-Output "Created: $zip"
    Write-Output "SHA256: $hash"
} finally {
    # Delete only the unique temporary workspace created by this invocation.
    if (-not [IO.Path]::GetFullPath($workspace).StartsWith($output.TrimEnd('\') + '\.package-', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Invalid packaging workspace.'
    }
    if (Test-Path -LiteralPath $workspace) { Remove-Item -LiteralPath $workspace -Recurse -Force }
}
