param([Parameter(Mandatory)][string]$Executable)
$ErrorActionPreference = 'Stop'
$encoding = [Text.UTF8Encoding]::new($false)
$temporaryBase = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$testRoot = [IO.Path]::GetFullPath((Join-Path $temporaryBase ('private-pinyin-personal-' + [guid]::NewGuid())))
[IO.Directory]::CreateDirectory($testRoot) | Out-Null
$script:checks = 0
function Assert-Check([bool]$Condition, [string]$Message) {
    $script:checks++
    if (-not $Condition) { throw $Message }
}
function Invoke-PersonalCLI([string[]]$Arguments) {
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = [IO.Path]::GetFullPath($Executable)
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.StandardOutputEncoding = $encoding
    $info.StandardErrorEncoding = $encoding
    $info.Environment['LOCALAPPDATA'] = Join-Path $testRoot 'local-appdata'
    $info.ArgumentList.Add('personal')
    foreach ($argument in $Arguments) { $info.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $info
    try {
        $process.Start() | Out-Null
        $out = $process.StandardOutput.ReadToEndAsync()
        $err = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit(15000)) { $process.Kill($true); throw 'personal CLI timed out' }
        $output = $out.GetAwaiter().GetResult()
        $errorText = $err.GetAwaiter().GetResult()
        return @{Code = $process.ExitCode; Json = ($output | ConvertFrom-Json); Error = $errorText}
    } finally { $process.Dispose() }
}
try {
    $user = Join-Path $testRoot 'local-appdata\PrivatePinyin\words.user.tsv'
    $source = Join-Path $testRoot '中文 合并.tsv'
    $export = Join-Path $testRoot '中文 导出.tsv'
    $run = Invoke-PersonalCLI @('help')
    Assert-Check ($run.Code -eq 0 -and $run.Json.commands -contains 'merge' -and $run.Json.schema -eq 1) 'discovery help failed'
    $run = Invoke-PersonalCLI @('export', '--file', $export)
    Assert-Check ($run.Code -eq 0 -and $run.Json.entries -eq 0 -and -not (Test-Path -LiteralPath (Split-Path -Parent $user))) 'empty export created live storage'
    $original = [IO.File]::ReadAllText($export, $encoding)
    $run = Invoke-PersonalCLI @('export', '--file', $export)
    Assert-Check ($run.Code -ne 0 -and -not $run.Json.ok -and [IO.File]::ReadAllText($export, $encoding) -eq $original) 'export silently overwrote an existing backup'
    [IO.File]::WriteAllText($source, "# private-pinyin user dictionary v1`n电脑`tDIAN NAO`t3`n电脑`tdian nao`t5`n重载`tchong zai`t2`n重载`tzhong zai`t7`n", $encoding)
    $run = Invoke-PersonalCLI @('validate', '--file', $source)
    Assert-Check ($run.Code -eq 0 -and $run.Json.records -eq 4 -and $run.Json.entries -eq 3 -and $run.Json.duplicates -eq 1) 'validation did not report canonical duplicates'
    Assert-Check (-not (Test-Path -LiteralPath $user)) 'validation opened private user storage'
    $run = Invoke-PersonalCLI @('merge', '--file', $source)
    Assert-Check ($run.Code -eq 0 -and $run.Json.added -eq 3 -and $run.Json.total -eq 3 -and $run.Json.backup -eq '') 'first import failed'
    $first = [IO.File]::ReadAllText($user, $encoding)
    [IO.File]::WriteAllText($source, "电脑`tdian nao`t2`n重载`tchong zai`t8`n合并测试词`the bing ce shi ci`t4`n", $encoding)
    $run = Invoke-PersonalCLI @('merge', '--file', $source)
    Assert-Check ($run.Code -eq 0 -and $run.Json.added -eq 1 -and $run.Json.updated -eq 1 -and $run.Json.unchanged -eq 1 -and $run.Json.total -eq 4) 'merge statistics or max-count rule failed'
    Assert-Check ((Test-Path -LiteralPath $run.Json.backup) -and [IO.File]::ReadAllText($run.Json.backup, $encoding) -eq $first) 'automatic backup was missing or changed'
    $merged = [IO.File]::ReadAllText($user, $encoding)
    $run = Invoke-PersonalCLI @('merge', '--file', $source)
    Assert-Check ($run.Code -eq 0 -and $run.Json.unchanged -eq 3 -and $run.Json.backup -eq '' -and [IO.File]::ReadAllText($user, $encoding) -eq $merged) 'repeated merge accumulated counts'
    $run = Invoke-PersonalCLI @('export', '--file', $export, '--overwrite')
    Assert-Check ($run.Code -eq 0 -and $run.Json.entries -eq 4 -and [IO.File]::ReadAllText($export, $encoding) -eq $merged) 'explicit export overwrite lost counts'
    $custom = Join-Path $testRoot '独立 用户\words.tsv'
    $run = Invoke-PersonalCLI @('merge', '--user', $custom, '--file', $export)
    Assert-Check ($run.Code -eq 0 -and $run.Json.total -eq 4 -and [IO.File]::ReadAllText($custom, $encoding) -eq $merged) 'custom user path round-trip failed'
    $run = Invoke-PersonalCLI @('export', '--user', $custom, '--file', $custom, '--overwrite')
    Assert-Check ($run.Code -ne 0 -and -not $run.Json.ok -and [IO.File]::ReadAllText($custom, $encoding) -eq $merged) 'self export could overwrite live storage'
    foreach ($arguments in @(
        ,@('merge', '--file', $export, '--overwrite'),
        ,@('validate', '--file', $export, '--user', $custom),
        ,@('merge', '--file', $export, '--file', $export),
        ,@('merge', '--file'),
        ,@('export', '--file', '--overwrite'),
        ,@('remove', '--file', $export),
        ,@('help', '--user', $custom)
    )) {
        $run = Invoke-PersonalCLI $arguments
        Assert-Check ($run.Code -ne 0 -and -not $run.Json.ok) 'invalid command or options were accepted'
    }
    [IO.File]::WriteAllText($source, "# private-pinyin supplementary dictionary v1`n测试`tce shi`t99999`n", $encoding)
    $run = Invoke-PersonalCLI @('merge', '--file', $source)
    Assert-Check ($run.Code -ne 0 -and -not $run.Json.ok -and [IO.File]::ReadAllText($user, $encoding) -eq $merged) 'ordinary sorting weights became personal usage'
    [IO.File]::WriteAllText($source, "坏词`tinvalid`t1`n", $encoding)
    $run = Invoke-PersonalCLI @('merge', '--file', $source)
    Assert-Check ($run.Code -ne 0 -and -not $run.Json.ok -and [IO.File]::ReadAllText($user, $encoding) -eq $merged) 'invalid import damaged personal storage'
    $run = Invoke-PersonalCLI @('merge', '--file', (Join-Path $testRoot 'absent.tsv'))
    Assert-Check ($run.Code -ne 0 -and -not $run.Json.ok) 'missing source was accepted'
    Assert-Check (-not (Test-Path -LiteralPath (Join-Path (Split-Path -Parent $user) 'dictionaries'))) 'personal command modified supplementary storage'
    Write-Output "PASS: $script:checks personal CLI checks"
} finally {
    $resolved = [IO.Path]::GetFullPath($testRoot)
    if ($resolved.StartsWith($temporaryBase.TrimEnd('\') + '\private-pinyin-personal-', [StringComparison]::OrdinalIgnoreCase)) {
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
