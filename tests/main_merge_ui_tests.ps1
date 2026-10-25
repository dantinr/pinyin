param([Parameter(Mandatory = $true)][string]$Executable, [Parameter(Mandatory = $true)][string]$Screenshot)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class MainMergeWindow {
    public delegate bool Visit(IntPtr window, IntPtr state);
    [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
    [DllImport("user32.dll")] public static extern bool EnumWindows(Visit visit, IntPtr state);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr parent, Visit visit, IntPtr state);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window, out uint process);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowText(IntPtr window, StringBuilder text, int size);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr window, uint message, IntPtr first, IntPtr second);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr window, uint message, IntPtr first, IntPtr second);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window, out Rectangle rectangle);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr window, IntPtr context, uint flags);
    [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr window, int identifier);
    [DllImport("user32.dll")] public static extern int GetDlgCtrlID(IntPtr window);
    [StructLayout(LayoutKind.Sequential)] public struct Rectangle { public int Left, Top, Right, Bottom; }
    public static string Text(IntPtr window) {
        var text = new StringBuilder(8192); GetWindowText(window, text, text.Capacity); return text.ToString();
    }
    public static IntPtr Find(uint process, string title) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((window, state) => {
            uint owner; GetWindowThreadProcessId(window, out owner);
            if (owner == process && Text(window) == title) { found = window; return false; }
            return true;
        }, IntPtr.Zero);
        return found;
    }
    public static string Content(IntPtr window) {
        var result = new StringBuilder();
        EnumChildWindows(window, (child, state) => { result.AppendLine(GetDlgCtrlID(child) + ": " + Text(child)); return true; }, IntPtr.Zero);
        return result.ToString();
    }
}
'@
function Wait-Dialog([uint32]$OwnerId, [string]$Title, [int]$Button = 0, [switch]$MessageBox) {
    $deadline = [DateTime]::UtcNow.AddSeconds(12)
    do {
        $window = [MainMergeWindow]::Find($OwnerId, $Title)
        if ($window -ne [IntPtr]::Zero) {
            $ready = if ($Button) { [MainMergeWindow]::GetDlgItem($window, $Button) } else {
                $primary = [MainMergeWindow]::GetDlgItem($window, 1)
                if ($primary -ne [IntPtr]::Zero) { $primary } else { [MainMergeWindow]::GetDlgItem($window, 2) }
            }
            if ($ready -ne [IntPtr]::Zero) {
                # MessageBox creates its buttons before the body text control.
                # Wait for the complete message before checking its contents.
                if ($MessageBox) {
                    $body = [MainMergeWindow]::GetDlgItem($window, 65535)
                    if ($body -eq [IntPtr]::Zero -or -not [MainMergeWindow]::Text($body)) {
                        Start-Sleep -Milliseconds 40
                        continue
                    }
                }
                return $window
            }
        }
        Start-Sleep -Milliseconds 40
    } while ([DateTime]::UtcNow -lt $deadline)
    $content = if ($window -ne [IntPtr]::Zero) { [MainMergeWindow]::Content($window) } else { 'window not found' }
    throw "Missing dialog or button $Button in ${Title}: $content"
}
function Click([IntPtr]$Window, [int]$Identifier) {
    $control = [MainMergeWindow]::GetDlgItem($Window, $Identifier)
    if ($control -eq [IntPtr]::Zero) { throw ("Missing button $Identifier in " + [MainMergeWindow]::Text($Window)) }
    if (-not [MainMergeWindow]::PostMessage($control, 0xF5, [IntPtr]::Zero, [IntPtr]::Zero)) {
        throw "Cannot click button: $Identifier"
    }
}
function Wait-Closed([IntPtr]$Window) {
    $deadline = [DateTime]::UtcNow.AddSeconds(5)
    while ([MainMergeWindow]::IsWindow($Window)) {
        if ([DateTime]::UtcNow -ge $deadline) { throw ('Dialog did not close: ' + [MainMergeWindow]::Text($Window)) }
        Start-Sleep -Milliseconds 40
    }
}
function Close-Result([IntPtr]$Window) {
    $identifier = if ([MainMergeWindow]::GetDlgItem($Window, 1) -ne [IntPtr]::Zero) { 1 } else { 2 }
    Click $Window $identifier
    Wait-Closed $Window
}
$null = [MainMergeWindow]::SetThreadDpiAwarenessContext([IntPtr](-4))
$temporaryBase = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')
$workspace = Join-Path $temporaryBase ('pinyin-main-ui-' + [Guid]::NewGuid().ToString('N'))
$testProfile = Join-Path $workspace '用户 profile'
$userRoot = Join-Path $testProfile 'PrivatePinyin'
$userFile = Join-Path $userRoot 'words.user.tsv'
$mainFile = Join-Path $userRoot 'main.tsv'
$null = New-Item -ItemType Directory -Path $userRoot -Force
[IO.File]::WriteAllText($userFile, "# private-pinyin user dictionary v1`n用户合并测试词`tyong hu he bing ce shi ci`t9`n", [Text.UTF8Encoding]::new($false))
$originalUser = (Get-FileHash -LiteralPath $userFile).Hash
$process = $null
try {
    $start = [Diagnostics.ProcessStartInfo]::new([IO.Path]::GetFullPath($Executable))
    $start.UseShellExecute = $false
    $start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $start.EnvironmentVariables['USERPROFILE'] = $testProfile
    $start.EnvironmentVariables['LOCALAPPDATA'] = Join-Path $workspace 'appdata'
    $process = [Diagnostics.Process]::Start($start)
    $null = $process.WaitForInputIdle(10000)
    $main = Wait-Dialog $process.Id '隐私拼音 · 设置'
    Click $main 1006
    $confirmation = Wait-Dialog $process.Id '确认合并到主词库' 6 -MessageBox
    $content = [MainMergeWindow]::Content($confirmation)
    if (-not $content.Contains($userFile) -or -not $content.Contains($mainFile) -or
        -not $content.Contains('备份') -or -not $content.Contains('选择次数保留')) {
        throw 'Merge confirmation did not explain its source, destination, backup and preservation rules.'
    }
    if (([MainMergeWindow]::SendMessage($confirmation, 0x400, [IntPtr]::Zero, [IntPtr]::Zero).ToInt64() -band 0xffff) -ne 7) {
        throw 'Confirmation did not default to No.'
    }
    if (Test-Path -LiteralPath $mainFile) { throw 'Main dictionary was modified before confirmation.' }
    $bounds = [MainMergeWindow+Rectangle]::new()
    $null = [MainMergeWindow]::GetWindowRect($confirmation, [ref]$bounds)
    $bitmap = [Drawing.Bitmap]::new($bounds.Right - $bounds.Left, $bounds.Bottom - $bounds.Top)
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    $context = $graphics.GetHdc()
    try { if (-not [MainMergeWindow]::PrintWindow($confirmation, $context, 2)) { throw 'Cannot capture merge confirmation.' } }
    finally { $graphics.ReleaseHdc($context); $graphics.Dispose() }
    try { $bitmap.Save([IO.Path]::GetFullPath($Screenshot), [Drawing.Imaging.ImageFormat]::Png) }
    finally { $bitmap.Dispose() }
    Click $confirmation 7
    Wait-Closed $confirmation
    if (Test-Path -LiteralPath $mainFile) { throw 'Cancelling merge created a main dictionary.' }
    if ((Get-FileHash -LiteralPath $userFile).Hash -ne $originalUser) { throw 'Cancelling merge changed user words.' }
    Click $main 1006
    $confirmation = Wait-Dialog $process.Id '确认合并到主词库' 6 -MessageBox
    Click $confirmation 6
    $result = Wait-Dialog $process.Id '合并到主词库' -MessageBox
    if (-not (Test-Path -LiteralPath $mainFile) -or -not ([IO.File]::ReadAllText($mainFile)).Contains("用户合并测试词`tyong hu he bing ce shi ci`t12000")) {
        throw 'Confirmed merge did not copy the current personal word to the main dictionary.'
    }
    if ((Get-FileHash -LiteralPath $userFile).Hash -ne $originalUser) { throw 'Merging changed personal selection counts.' }
    $backups = @(Get-ChildItem -LiteralPath (Join-Path $userRoot 'backups') -Filter 'main.*.tsv')
    if ($backups.Count -ne 1) { throw 'Confirmed merge did not create exactly one main backup.' }
    Close-Result $result
    $savedMain = (Get-FileHash -LiteralPath $mainFile).Hash
    Click $main 1006
    $confirmation = Wait-Dialog $process.Id '确认合并到主词库' 6 -MessageBox
    Click $confirmation 6
    $result = Wait-Dialog $process.Id '合并到主词库' -MessageBox
    if (-not ([MainMergeWindow]::Content($result)).Contains('新增 0 条') -or
        (Get-FileHash -LiteralPath $mainFile).Hash -ne $savedMain -or
        @(Get-ChildItem -LiteralPath (Join-Path $userRoot 'backups') -Filter 'main.*.tsv').Count -ne 1) {
        throw 'Repeated merge rewrote or duplicated data or created another backup.'
    }
    Close-Result $result
    $null = [MainMergeWindow]::PostMessage($main, 0x10, [IntPtr]::Zero, [IntPtr]::Zero)
    if (-not $process.WaitForExit(10000)) { throw 'Settings did not close after merge.' }
    Write-Output 'PASS: main merge confirmation, default No, cancel, automatic source, backup and idempotency'
} finally {
    if ($process -and -not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
    $resolved = [IO.Path]::GetFullPath($workspace)
    if ($resolved.StartsWith($temporaryBase + '\pinyin-main-ui-', [StringComparison]::OrdinalIgnoreCase)) {
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
