param([Parameter(Mandatory)][string]$Executable)

$ErrorActionPreference = 'Stop'
$encoding = [Text.UTF8Encoding]::new($false)
$tempBase = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$root = [IO.Path]::GetFullPath((Join-Path $tempBase ('private-pinyin-cli-' + [guid]::NewGuid())))
[IO.Directory]::CreateDirectory($root) | Out-Null
$script:checks = 0

function Assert-Check([bool]$Condition, [string]$Message) {
    $script:checks++
    if (-not $Condition) { throw $Message }
}

function Invoke-CLI([string[]]$CliArguments, [string]$InputText = '') {
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = [IO.Path]::GetFullPath($Executable)
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardInput = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.StandardInputEncoding = $encoding
    $info.StandardOutputEncoding = $encoding
    $info.StandardErrorEncoding = $encoding
    # Override only the child environment: no test touches the actual personal dictionary.
    $info.Environment['LOCALAPPDATA'] = Join-Path $root 'local-appdata'
    # Stable fixture: production dictionary growth must not change the test candidates.
    $info.ArgumentList.Add('--dict')
    $info.ArgumentList.Add((Join-Path $PSScriptRoot 'fixtures\base-demo.tsv'))
    foreach ($argument in $CliArguments) { $info.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $info
    try {
        $process.Start() | Out-Null
        $outTask = $process.StandardOutput.ReadToEndAsync()
        $errTask = $process.StandardError.ReadToEndAsync()
        $process.StandardInput.Write($InputText)
        $process.StandardInput.Close()
        if (-not $process.WaitForExit(10000)) {
            $process.Kill($true)
            throw 'CLI timed out'
        }
        return @{ Code = $process.ExitCode; Output = $outTask.GetAwaiter().GetResult(); Error = $errTask.GetAwaiter().GetResult() }
    } finally { $process.Dispose() }
}

try {
    $user = Join-Path $root '中文 用户目录\words.user.tsv'
    $run = Invoke-CLI -CliArguments @('--user', $user) -InputText "nihao`n1`n/add yin'si'shu'ru'fa 隐私输入法`n/quit`n"
    Assert-Check ($run.Code -eq 0 -and $run.Output.Contains('确认：你好')) 'stateless selection failed'
    Assert-Check (-not (Test-Path -LiteralPath ([IO.Path]::GetDirectoryName($user)))) 'stateless session created user directory'
    Assert-Check (-not (Test-Path -LiteralPath (Join-Path $root 'local-appdata'))) 'default mode accessed user storage'
    Assert-Check ($run.Output.Contains('自造词需要')) 'stateless session accepted persistent custom word'

    $batchUser = Join-Path $root 'batch\words.user.tsv'
    $run = Invoke-CLI -CliArguments @('--query', "xi'an", '--learn', '--user', $batchUser)
    Assert-Check ($run.Code -eq 0 -and $run.Output.Contains('西安') -and -not $run.Output.Contains('先')) 'batch boundary query failed'
    Assert-Check (-not (Test-Path -LiteralPath ([IO.Path]::GetDirectoryName($batchUser)))) 'batch query wrote personal data'

    $run = Invoke-CLI -CliArguments @('--sentence', 'wozaibeijing', '--learn', '--user', $batchUser)
    Assert-Check ($run.Code -eq 0 -and $run.Output.Contains('1. 我在北京') -and $run.Output.Contains('[wo zai bei jing]')) 'offline sentence query failed'
    Assert-Check (-not (Test-Path -LiteralPath ([IO.Path]::GetDirectoryName($batchUser)))) 'sentence query accessed personal storage'
    $run = Invoke-CLI -CliArguments @('--query', 'wozaibeijing')
    Assert-Check ($run.Code -eq 0 -and $run.Output.Contains('没有完整匹配')) 'exact query unexpectedly generated sentences'
    $run = Invoke-CLI -CliArguments @('--sentence', 'wozaibeijing', '--query', 'nihao')
    Assert-Check ($run.Code -ne 0) 'contradictory query modes were accepted'
    $run = Invoke-CLI -CliArguments @('--sentence', 'wo1')
    Assert-Check ($run.Code -ne 0) 'invalid sentence query was accepted'

    $run = Invoke-CLI -CliArguments @('--learn', '--user', $user) -InputText "/add yin'si'shu'ru'fa 隐私输入法`nyinsishurufa`n1`n/quit`n"
    Assert-Check ($run.Code -eq 0 -and $run.Error -eq '') ('learning session failed: ' + $run.Error)
    Assert-Check ($run.Output.Contains('确认：隐私输入法')) 'custom word selection failed'
    $saved = [IO.File]::ReadAllText($user, $encoding)
    Assert-Check ($saved.Contains("隐私输入法`tyin si shu ru fa`t2")) 'custom word/count was not persisted'
    Assert-Check (-not $saved.Contains('yinsishurufa')) 'raw query was persisted'

    $run = Invoke-CLI -CliArguments @('--learn', '--user', $user) -InputText "yinsishurufa`n/quit`n"
    Assert-Check ($run.Code -eq 0 -and $run.Output.Contains('本地选择 2 次')) 'persisted custom word failed on restart'

    $run = Invoke-CLI -CliArguments @('--learn', '--user', $user) -InputText "/clear`nyinsishurufa`n/quit`n"
    Assert-Check ($run.Code -eq 0 -and $run.Output.Contains('没有完整匹配')) 'clear left custom word in memory'
    Assert-Check (-not ([IO.File]::ReadAllText($user, $encoding)).Contains('隐私输入法')) 'clear left user record on disk'

    [IO.File]::WriteAllText($user, 'corrupt user file', $encoding)
    $before = [IO.File]::ReadAllText($user, $encoding)
    $run = Invoke-CLI -CliArguments @('--no-learn', '--user', $user) -InputText "nihao`n/quit`n"
    Assert-Check ($run.Code -eq 0 -and $run.Output.Contains('你好')) 'private mode loaded corrupt user dictionary'
    Assert-Check ([IO.File]::ReadAllText($user, $encoding) -eq $before) 'private mode modified user dictionary'
    $run = Invoke-CLI -CliArguments @('--learn', '--user', $user)
    Assert-Check ($run.Code -ne 0 -and $run.Error.Contains('line 1')) 'corrupt user dictionary did not fail visibly'
    Assert-Check ([IO.File]::ReadAllText($user, $encoding) -eq $before) 'failed startup overwrote corrupt dictionary'

    $run = Invoke-CLI -CliArguments @('--query', 'LÜSE')
    Assert-Check ($run.Code -eq 0 -and $run.Output.Contains('绿色')) 'Unicode command-line query failed'
    $run = Invoke-CLI -CliArguments @('--query', 'nihao1')
    Assert-Check ($run.Code -ne 0) 'invalid batch query did not return failure'
    $run = Invoke-CLI -CliArguments @('--learn', '--no-learn')
    Assert-Check ($run.Code -ne 0) 'contradictory privacy flags were accepted'

    $imeUser = Join-Path $root 'ime-settings\words.user.tsv'
    $flag = $imeUser + '.ime-learning.disabled'
    $run = Invoke-CLI -CliArguments @('--ime-learning', 'status', '--user', $imeUser)
    Assert-Check ($run.Code -eq 0 -and $run.Output.Contains('已开启')) 'IME learning was not enabled by default'
    Assert-Check (-not (Test-Path -LiteralPath ([IO.Path]::GetDirectoryName($imeUser)))) 'reading default settings created personal storage'
    $run = Invoke-CLI -CliArguments @('--ime-learning', 'on', '--user', $imeUser)
    Assert-Check ($run.Code -eq 0 -and -not (Test-Path -LiteralPath $flag)) 'IME learning did not enable'
    Assert-Check (-not (Test-Path -LiteralPath $imeUser)) 'enabling IME learning invented a personal word record'
    $run = Invoke-CLI -CliArguments @('--learn', '--user', $imeUser) -InputText "/add ru'he 如何`n/quit`n"
    Assert-Check ($run.Code -eq 0 -and ([IO.File]::ReadAllText($imeUser, $encoding)).Contains('如何')) 'shared CLI/IME user file failed'
    $run = Invoke-CLI -CliArguments @('--ime-learning', 'off', '--user', $imeUser)
    Assert-Check ($run.Code -eq 0 -and (Test-Path -LiteralPath $flag)) 'IME learning did not disable'
    $run = Invoke-CLI -CliArguments @('--ime-learning', 'status', '--user', $imeUser)
    Assert-Check ($run.Code -eq 0 -and $run.Output.Contains('已关闭')) 'IME learning opt-out did not persist across processes'
    Assert-Check (([IO.File]::ReadAllText($imeUser, $encoding)).Contains('如何')) 'disabling IME learning deleted personal words'
    $run = Invoke-CLI -CliArguments @('--ime-learning', 'clear', '--user', $imeUser)
    Assert-Check ($run.Code -eq 0 -and -not ([IO.File]::ReadAllText($imeUser, $encoding)).Contains('如何')) 'IME user-data clear failed'
    Assert-Check (Test-Path -LiteralPath $flag) 'clearing words discarded the explicit learning opt-out'
    $run = Invoke-CLI -CliArguments @('--ime-learning', 'on', '--query', 'nihao', '--user', $imeUser)
    Assert-Check ($run.Code -ne 0 -and (Test-Path -LiteralPath $flag)) 'query unexpectedly enabled persistent IME learning'
    $run = Invoke-CLI -CliArguments @('--ime-learning', 'on', '--sentence', 'wozaibeijing', '--user', $imeUser)
    Assert-Check ($run.Code -ne 0 -and (Test-Path -LiteralPath $flag)) 'sentence query unexpectedly enabled persistent IME learning'
    $run = Invoke-CLI -CliArguments @('--ime-learning', 'invalid', '--user', $imeUser)
    Assert-Check ($run.Code -ne 0) 'invalid IME learning setting was accepted'
    $run = Invoke-CLI -CliArguments @('--ime-learning', 'on', '--user', $imeUser)
    Assert-Check ($run.Code -eq 0 -and -not (Test-Path -LiteralPath $flag)) 'IME learning opt-out was not removed when re-enabled'
    $run = Invoke-CLI -CliArguments @('--ime-learning', 'status', '--user', $imeUser)
    Assert-Check ($run.Code -eq 0 -and $run.Output.Contains('已开启')) 'IME learning did not stay enabled after removing the opt-out'
    Write-Output "PASS: $script:checks CLI checks"
} finally {
    # Resolve and verify the exact uniquely-created directory before recursive cleanup.
    $cleanup = [IO.Path]::GetFullPath($root)
    $prefix = $tempBase.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    if (-not $cleanup.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase) -or
        [IO.Path]::GetFileName($cleanup) -notlike 'private-pinyin-cli-*') {
        throw 'Refusing cleanup outside the test temporary directory'
    }
    Remove-Item -LiteralPath $cleanup -Recurse -Force
}
