param([Parameter(Mandatory = $true)][string]$PackageDir)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$checks = 0
function Check([bool]$Condition, [string]$Message) {
    $script:checks++
    if (-not $Condition) { throw $Message }
}
function Rejects([scriptblock]$Action, [string]$Message) {
    $rejected = $false
    try { $null = & $Action } catch { $rejected = $true }
    Check $rejected $Message
}
function Write-Manifest([string]$Root, $Manifest) {
    [IO.File]::WriteAllText((Join-Path $Root 'manifest.json'), ($Manifest | ConvertTo-Json -Depth 5), [Text.UTF8Encoding]::new($false))
}
$tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\', '/')
$workspace = Join-Path $tempRoot ('private-pinyin-package-tests-' + [Guid]::NewGuid().ToString('N'))
try {
    . (Join-Path $PackageDir 'scripts/package-common.ps1')
    $package = Read-PinyinPackage $PackageDir
    $null = New-Item -ItemType Directory -Path $workspace
    $installRoot = Join-Path $workspace ('install path ' + [char]0x8bcd + [char]0x5e93)
    $installed = Copy-PinyinPackage $PackageDir $installRoot
    Check ($installed -eq (Join-Path $installRoot $package.version)) 'Package was not installed in its versioned directory.'
    $null = Read-PinyinPackage $installed
    $dll = Join-Path $installed 'private_pinyin_ime.dll'
    $originalHash = (Get-FileHash -LiteralPath $dll).Hash
    $originalDate = (Get-Item -LiteralPath $dll).LastWriteTimeUtc
    $locked = [IO.File]::Open($dll, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    try {
        $repeated = Copy-PinyinPackage $PackageDir $installRoot
        Check ($repeated -eq $installed -and (Get-Item -LiteralPath $dll).LastWriteTimeUtc -eq $originalDate) 'Reinstallation overwrote a locked DLL.'
        $nextSource = Join-Path $workspace 'next package'
        Copy-Item -LiteralPath $PackageDir -Destination $nextSource -Recurse
        $next = Read-PinyinPackage $nextSource
        $currentVersion = [version]$next.version
        $next.version = '{0}.{1}.{2}' -f $currentVersion.Major, $currentVersion.Minor, ($currentVersion.Build + 1)
        Write-Manifest $nextSource $next
        $updated = Copy-PinyinPackage $nextSource $installRoot
        Check ($updated -ne $installed -and (Test-Path -LiteralPath (Join-Path $updated 'private_pinyin_ime.dll'))) 'Upgrade did not create a separate version directory.'
        Check ((Get-FileHash -LiteralPath $dll).Hash -eq $originalHash) 'Upgrade changed the locked previous version.'
        # A different payload claiming the same version must not overwrite it.
        $next.version = $package.version
        [IO.File]::AppendAllText((Join-Path $nextSource 'README.txt'), "`nChanged fixture.")
        ($next.files | Where-Object { $_.path -eq 'README.txt' }).sha256 = (Get-FileHash -LiteralPath (Join-Path $nextSource 'README.txt')).Hash
        Write-Manifest $nextSource $next
        Rejects { Copy-PinyinPackage $nextSource $installRoot } 'Conflicting same-version payload overwrote the existing installation.'
        Check ((Get-FileHash -LiteralPath $dll).Hash -eq $originalHash) 'Failed reinstall changed existing files.'
    } finally { $locked.Dispose() }

    [IO.File]::AppendAllText((Join-Path $nextSource 'data/base.tsv'), "`n# changed fixture")
    Rejects { Read-PinyinPackage $nextSource } 'Modified dictionary passed the package integrity check.'
    # A traversal must be rejected before reading a matching outside file.
    $outside = Join-Path $workspace 'outside.txt'
    [IO.File]::WriteAllText($outside, 'sentinel')
    $next.files[0].path = '../outside.txt'
    $next.files[0].sha256 = (Get-FileHash -LiteralPath $outside).Hash
    Write-Manifest $nextSource $next
    Rejects { Copy-PinyinPackage $nextSource $installRoot } 'Outside file was copied into the installation.'
    Check ([IO.File]::ReadAllText($outside) -eq 'sentinel') 'Outside sentinel was modified.'
    Check (@(Get-ChildItem -LiteralPath $installRoot -Directory | Where-Object { $_.Name -like '*.staging-*' }).Count -eq 0) 'Installation retained an incomplete staging directory.'
    Write-Output "PASS: $checks package integrity/install/upgrade checks (isolated files only)."
} finally {
    if (-not [IO.Path]::GetFullPath($workspace).StartsWith($tempRoot + '\private-pinyin-package-tests-', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Invalid test workspace.'
    }
    if (Test-Path -LiteralPath $workspace) { Remove-Item -LiteralPath $workspace -Recurse -Force }
}
