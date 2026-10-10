param([Parameter(Mandatory)][string]$Executable)
$ErrorActionPreference = 'Stop'
$encoding = [Text.UTF8Encoding]::new($false)
$temporaryBase = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$testRoot = [IO.Path]::GetFullPath((Join-Path $temporaryBase ('private-pinyin-agent-' + [guid]::NewGuid())))
[IO.Directory]::CreateDirectory($testRoot) | Out-Null
$script:checks = 0
function Assert-Check([bool]$Condition, [string]$Message) {
    $script:checks++
    if (-not $Condition) { throw $Message }
}
function Invoke-AgentCLI([string[]]$Arguments) {
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = [IO.Path]::GetFullPath($Executable)
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.StandardOutputEncoding = $encoding
    $info.StandardErrorEncoding = $encoding
    $info.Environment['LOCALAPPDATA'] = Join-Path $testRoot 'local-appdata'
    $info.ArgumentList.Add('lexicon')
    foreach ($argument in $Arguments) { $info.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $info
    try {
        $process.Start() | Out-Null
        $out = $process.StandardOutput.ReadToEndAsync()
        $err = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit(15000)) { $process.Kill($true); throw 'Agent CLI timed out' }
        $output = $out.GetAwaiter().GetResult()
        $errorText = $err.GetAwaiter().GetResult()
        return @{Code = $process.ExitCode; Json = ($output | ConvertFrom-Json); Error = $errorText}
    } finally { $process.Dispose() }
}
try {
    $managed = Join-Path $testRoot 'local-appdata\PrivatePinyin\dictionaries'
    $fixture = Join-Path $PSScriptRoot 'fixtures\base-demo.tsv'
    $run = Invoke-AgentCLI @('list', '--base', $fixture)
    Assert-Check ($run.Code -eq 0 -and $run.Json.schema -eq 1 -and $run.Json.dictionaries.Count -eq 1 -and $run.Json.dictionaries[0].readOnly) 'initial machine-readable list failed'
    Assert-Check (-not (Test-Path -LiteralPath $managed)) 'listing created private storage'
    $source = Join-Path $testRoot '中文 空格词库.tsv'
    [IO.File]::WriteAllText($source, "# source: original fixture`n辅库测试词`tfu ku ce shi ci`t300`n辅库测试词`tfu ku ce shi ci`t100`n", $encoding)
    $run = Invoke-AgentCLI @('validate', '--file', $source)
    Assert-Check ($run.Code -eq 0 -and $run.Json.records -eq 2 -and $run.Json.entries -eq 1 -and $run.Json.duplicates -eq 1) 'validation did not expose duplicates'
    $run = Invoke-AgentCLI @('import', '--name', 'computer', '--file', $source)
    Assert-Check ($run.Code -eq 0 -and $run.Json.entries -eq 1) 'Unicode-path dictionary import failed'
    $target = Join-Path $managed 'computer.tsv'
    $first = [IO.File]::ReadAllText($target, $encoding)
    $run = Invoke-AgentCLI @('import', '--name', 'computer', '--file', $source)
    Assert-Check ($run.Code -eq 0 -and [IO.File]::ReadAllText($target, $encoding) -eq $first) 'repeated import changed records'
    $run = Invoke-AgentCLI @('query', '--base', $fixture, '--pinyin', 'fukuceshici')
    Assert-Check ($run.Code -eq 0 -and $run.Json.candidates[0].text -eq '辅库测试词' -and $run.Json.candidates[0].weight -eq 300) 'merged query missed imported word'
    $run = Invoke-AgentCLI @('upsert', '--name', 'computer', '--text', '辅库测试词', '--pinyin', 'fu ku ce shi ci', '--weight', '50')
    Assert-Check ($run.Code -eq 0) 'explicit weight update failed'
    $run = Invoke-AgentCLI @('query', '--base', $fixture, '--pinyin', 'fukuceshici')
    Assert-Check ($run.Json.candidates[0].weight -eq 50) 'explicit weight could not decrease'
    $run = Invoke-AgentCLI @('disable', '--name', 'computer')
    Assert-Check ($run.Code -eq 0 -and -not $run.Json.enabled) 'disable failed'
    $run = Invoke-AgentCLI @('query', '--base', $fixture, '--pinyin', 'fukuceshici')
    Assert-Check ($run.Code -eq 0 -and @($run.Json.candidates | Where-Object text -eq '辅库测试词').Count -eq 0) 'disabled file still participated in query'
    $run = Invoke-AgentCLI @('enable', '--name', 'computer')
    Assert-Check ($run.Code -eq 0 -and $run.Json.enabled) 'enable failed'
    $backup = Join-Path $testRoot '词库 备份.tsv'
    $run = Invoke-AgentCLI @('export', '--name', 'computer', '--file', $backup)
    Assert-Check ($run.Code -eq 0 -and (Test-Path -LiteralPath $backup)) 'export failed'
    $run = Invoke-AgentCLI @('export', '--name', 'computer', '--file', $backup)
    Assert-Check ($run.Code -ne 0 -and -not $run.Json.ok) 'export overwrote an existing backup'
    [IO.File]::WriteAllText($source, "坏词`tinvalid`t100`n", $encoding)
    $unchanged = [IO.File]::ReadAllText($target, $encoding)
    $run = Invoke-AgentCLI @('import', '--name', 'computer', '--file', $source)
    Assert-Check ($run.Code -ne 0 -and -not $run.Json.ok -and [IO.File]::ReadAllText($target, $encoding) -eq $unchanged) 'invalid import damaged the active file'
    foreach ($name in @('../outside', 'base', 'user', 'com1')) {
        $run = Invoke-AgentCLI @('upsert', '--name', $name, '--text', '测试', '--pinyin', 'ce shi')
        Assert-Check ($run.Code -ne 0 -and -not $run.Json.ok) 'unsafe or reserved dictionary name was accepted'
    }
    $run = Invoke-AgentCLI @('remove', '--name', 'computer', '--text', '辅库测试词', '--pinyin', 'fu ku ce shi ci')
    Assert-Check ($run.Code -eq 0 -and $run.Json.removed) 'entry removal failed'
    $run = Invoke-AgentCLI @('validate', '--file', $target)
    Assert-Check ($run.Code -eq 0 -and $run.Json.entries -eq 0) 'empty dictionary after removal was invalid'
    $custom = Join-Path $testRoot '独立 辅库'
    $word = '包含"引号\和路径'
    $run = Invoke-AgentCLI @('upsert', '--root', $custom, '--name', 'custom', '--text', $word, '--pinyin', 'ce shi')
    Assert-Check ($run.Code -eq 0) 'custom dictionary root failed'
    $run = Invoke-AgentCLI @('query', '--root', $custom, '--base', $fixture, '--pinyin', 'ceshi')
    Assert-Check (@($run.Json.candidates | Where-Object text -eq $word).Count -eq 1) 'JSON escaping lost word characters'
    $run = Invoke-AgentCLI @('list', '--base', $fixture)
    Assert-Check ($run.Json.dictionaries.Count -eq 2 -and $run.Json.dictionaries[1].entries -eq 0) 'list mixed independent roots or wrong counts'
    Assert-Check (-not (Test-Path -LiteralPath (Join-Path $testRoot 'local-appdata\PrivatePinyin\words.user.tsv'))) 'Agent commands accessed the learned user store'
    $run = Invoke-AgentCLI @('import', '--name', 'computer', '--file', $backup, '--replace')
    Assert-Check ($run.Code -eq 0 -and $run.Json.entries -eq 1) 'explicit restore failed'
    $run = Invoke-AgentCLI @('help')
    Assert-Check ($run.Code -eq 0 -and $run.Json.commands -contains 'upsert') 'Agent discovery help was not machine-readable'
    Write-Output "PASS: $script:checks Agent CLI checks"
} finally {
    $resolved = [IO.Path]::GetFullPath($testRoot)
    if ($resolved.StartsWith($temporaryBase.TrimEnd('\') + '\private-pinyin-agent-', [StringComparison]::OrdinalIgnoreCase)) {
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
