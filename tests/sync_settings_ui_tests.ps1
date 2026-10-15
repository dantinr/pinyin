param([Parameter(Mandatory = $true)][string]$Executable, [Parameter(Mandatory = $true)][string]$Screenshot)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class SyncWindow {
    public delegate bool Visit(IntPtr window, IntPtr state);
    [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
    [DllImport("user32.dll")] public static extern bool EnumWindows(Visit visit, IntPtr state);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window, out uint process);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowText(IntPtr window, StringBuilder text, int size);
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr window, int identifier);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr window, uint message, IntPtr first, IntPtr second);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr window, uint message, IntPtr first, IntPtr second);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window, out Rectangle rectangle);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr window, IntPtr context, uint flags);
    [StructLayout(LayoutKind.Sequential)] public struct Rectangle { public int Left, Top, Right, Bottom; }
    public static IntPtr Find(uint process, string title) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((window, state) => {
            uint owner; GetWindowThreadProcessId(window, out owner);
            var text = new StringBuilder(512); GetWindowText(window, text, text.Capacity);
            if (owner == process && text.ToString().Contains(title)) { found = window; return false; }
            return true;
        }, IntPtr.Zero);
        return found;
    }
    public static string Text(IntPtr window) {
        var text = new StringBuilder(8192); GetWindowText(window, text, text.Capacity); return text.ToString();
    }
}
'@
$null = [SyncWindow]::SetThreadDpiAwarenessContext([IntPtr](-4))
$temporaryBase = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')
$workspace = Join-Path $temporaryBase ('pinyin-sync-ui-' + [Guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $workspace
$process = $null
try {
    $start = [Diagnostics.ProcessStartInfo]::new([IO.Path]::GetFullPath($Executable))
    $start.UseShellExecute = $false
    $start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $start.EnvironmentVariables['USERPROFILE'] = Join-Path $workspace 'profile'
    $start.EnvironmentVariables['LOCALAPPDATA'] = Join-Path $workspace 'appdata'
    $process = [Diagnostics.Process]::Start($start)
    $null = $process.WaitForInputIdle(10000)
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        $main = [SyncWindow]::Find($process.Id, '隐私拼音 · 设置')
        if ($main -eq [IntPtr]::Zero) { Start-Sleep -Milliseconds 50 }
    } while ($main -eq [IntPtr]::Zero -and [DateTime]::UtcNow -lt $deadline)
    if ($main -eq [IntPtr]::Zero) { throw 'Settings window was not created.' }
    $null = [SyncWindow]::PostMessage($main, 0x111, [IntPtr]1025, [IntPtr]::Zero)
    do {
        $dialog = [SyncWindow]::Find($process.Id, '个人词库云同步')
        if ($dialog -eq [IntPtr]::Zero) { Start-Sleep -Milliseconds 50 }
    } while ($dialog -eq [IntPtr]::Zero -and [DateTime]::UtcNow -lt $deadline)
    if ($dialog -eq [IntPtr]::Zero) { throw 'Sync settings dialog did not open.' }
    if ([SyncWindow]::SendMessage([SyncWindow]::GetDlgItem($dialog, 1018), 0xF0, [IntPtr]::Zero, [IntPtr]::Zero) -ne [IntPtr]::Zero) {
        throw 'A fresh sync settings dialog enabled networking by default.'
    }
    $deadline = [DateTime]::UtcNow.AddSeconds(5)
    do {
        $status = [SyncWindow]::Text([SyncWindow]::GetDlgItem($dialog, 1023))
        if (-not $status.Contains('云同步已关闭')) { Start-Sleep -Milliseconds 50 }
    } while (-not $status.Contains('云同步已关闭') -and [DateTime]::UtcNow -lt $deadline)
    if (-not $status.Contains('云同步已关闭')) { throw 'Disabled sync status was not shown.' }
    $bounds = [SyncWindow+Rectangle]::new()
    $null = [SyncWindow]::GetWindowRect($dialog, [ref]$bounds)
    foreach ($identifier in @(1018, 1019, 1020, 1021, 1022, 1023, 1024, 1)) {
        $control = [SyncWindow]::GetDlgItem($dialog, $identifier)
        if ($control -eq [IntPtr]::Zero) { throw "Missing sync control: $identifier" }
        if ($identifier -in @(1021, 1022, 1) -and [string]::IsNullOrEmpty([SyncWindow]::Text($control))) {
            throw "Missing sync button text: $identifier"
        }
        $rectangle = [SyncWindow+Rectangle]::new()
        $null = [SyncWindow]::GetWindowRect($control, [ref]$rectangle)
        if ($rectangle.Left -lt $bounds.Left -or $rectangle.Right -gt $bounds.Right -or
            $rectangle.Top -lt $bounds.Top -or $rectangle.Bottom -gt $bounds.Bottom) { throw "Clipped sync control: $identifier" }
    }
    $bitmap = [Drawing.Bitmap]::new($bounds.Right - $bounds.Left, $bounds.Bottom - $bounds.Top)
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    $context = $graphics.GetHdc()
    try { if (-not [SyncWindow]::PrintWindow($dialog, $context, 2)) { throw 'Could not capture sync settings dialog.' } }
    finally { $graphics.ReleaseHdc($context); $graphics.Dispose() }
    try { $bitmap.Save([IO.Path]::GetFullPath($Screenshot), [Drawing.Imaging.ImageFormat]::Png) }
    finally { $bitmap.Dispose() }
    $null = [SyncWindow]::PostMessage($dialog, 0x111, [IntPtr]1, [IntPtr]::Zero)
    $null = [SyncWindow]::PostMessage($main, 0x10, [IntPtr]::Zero, [IntPtr]::Zero)
    if (-not $process.WaitForExit(10000)) { throw 'Settings did not close after cancel.' }
    if (Test-Path -LiteralPath (Join-Path $workspace 'profile\PrivatePinyin\words.user.tsv.sync.settings')) {
        throw 'Opening and closing sync settings persisted an enabled sync configuration.'
    }
    Write-Output 'PASS: sync settings controls, offline default, status, layout and cancel'
} finally {
    if ($process -and -not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
    $resolved = [IO.Path]::GetFullPath($workspace)
    if ([IO.Path]::GetDirectoryName($resolved) -eq $temporaryBase) { Remove-Item -LiteralPath $resolved -Recurse -Force }
}
