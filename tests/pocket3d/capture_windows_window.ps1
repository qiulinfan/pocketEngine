param(
    [string]$OutputPath = "tests/evidence/pocket3d-opengl/wslg-window.png",
    [string]$WindowTitle = "Pocket3D OpenGL Phase 1"
)

$nativeSource = @"
using System;
using System.Runtime.InteropServices;
using System.Text;

public static class Pocket3DWindowCaptureNative {
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);

    [StructLayout(LayoutKind.Sequential)]
    public struct RECT {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }

    [DllImport("user32.dll")]
    public static extern bool EnumWindows(EnumWindowsProc callback, IntPtr lParam);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern int GetWindowText(IntPtr hWnd, StringBuilder text, int count);

    [DllImport("user32.dll")]
    public static extern bool IsWindowVisible(IntPtr hWnd);

    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);

    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr hWnd);

    [DllImport("user32.dll")]
    public static extern bool ShowWindow(IntPtr hWnd, int command);

    [DllImport("user32.dll")]
    public static extern bool MoveWindow(IntPtr hWnd, int x, int y, int width,
                                         int height, bool repaint);
}
"@

Add-Type -TypeDefinition $nativeSource
Add-Type -AssemblyName System.Drawing

$matchedWindow = [IntPtr]::Zero
$callback = [Pocket3DWindowCaptureNative+EnumWindowsProc] {
    param([IntPtr]$windowHandle, [IntPtr]$parameter)
    if (-not [Pocket3DWindowCaptureNative]::IsWindowVisible($windowHandle)) {
        return $true
    }
    $text = [System.Text.StringBuilder]::new(512)
    [void][Pocket3DWindowCaptureNative]::GetWindowText(
        $windowHandle, $text, $text.Capacity)
    if ($text.ToString().StartsWith($WindowTitle,
            [System.StringComparison]::Ordinal)) {
        $script:matchedWindow = $windowHandle
        return $false
    }
    return $true
}

[void][Pocket3DWindowCaptureNative]::EnumWindows($callback, [IntPtr]::Zero)
if ($matchedWindow -eq [IntPtr]::Zero) {
    throw "Window not found: $WindowTitle"
}

[void][Pocket3DWindowCaptureNative]::ShowWindow($matchedWindow, 9)
[void][Pocket3DWindowCaptureNative]::SetForegroundWindow($matchedWindow)
$rect = [Pocket3DWindowCaptureNative+RECT]::new()
if (-not [Pocket3DWindowCaptureNative]::GetWindowRect($matchedWindow, [ref]$rect)) {
    throw "GetWindowRect failed for: $WindowTitle"
}

$initialWidth = $rect.Right - $rect.Left
$initialHeight = $rect.Bottom - $rect.Top
[void][Pocket3DWindowCaptureNative]::MoveWindow(
    $matchedWindow, 80, 80, $initialWidth, $initialHeight, $true)
Start-Sleep -Milliseconds 500
if (-not [Pocket3DWindowCaptureNative]::GetWindowRect($matchedWindow, [ref]$rect)) {
    throw "GetWindowRect failed after move for: $WindowTitle"
}

$width = $rect.Right - $rect.Left
$height = $rect.Bottom - $rect.Top
if ($width -le 0 -or $height -le 0) {
    throw "Invalid window rectangle: ${width}x${height}"
}

$absoluteOutput = [System.IO.Path]::GetFullPath($OutputPath)
$outputDirectory = [System.IO.Path]::GetDirectoryName($absoluteOutput)
[System.IO.Directory]::CreateDirectory($outputDirectory) | Out-Null

$bitmap = [System.Drawing.Bitmap]::new($width, $height)
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
try {
    $graphics.CopyFromScreen(
        $rect.Left, $rect.Top, 0, 0,
        [System.Drawing.Size]::new($width, $height))
    $bitmap.Save($absoluteOutput, [System.Drawing.Imaging.ImageFormat]::Png)
} finally {
    $graphics.Dispose()
    $bitmap.Dispose()
}

Write-Output "window_handle=$matchedWindow"
Write-Output "window_rect=$($rect.Left),$($rect.Top),${width}x${height}"
Write-Output "capture=$absoluteOutput"
