param(
    [string]$RepoUrl = "https://github.com/qiulinfan/pocketEngine.git",
    [string]$Branch = "main",
    [string]$InstallRoot = "",
    [switch]$SkipShortcuts,
    [switch]$LaunchEditor
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Require-Command {
    param([string]$Name)

    if (Get-Command $Name -ErrorAction SilentlyContinue) {
        return
    }

    throw "Required command '$Name' was not found in PATH."
}

function Ensure-GitCheckout {
    param(
        [string]$RepoUrl,
        [string]$Branch,
        [string]$InstallRoot
    )

    if (-not (Test-Path $InstallRoot)) {
        $parentDir = Split-Path -Parent $InstallRoot
        if (-not (Test-Path $parentDir)) {
            New-Item -ItemType Directory -Path $parentDir | Out-Null
        }

        Write-Host "Cloning PocketEngine into $InstallRoot"
        & git clone --branch $Branch --depth 1 $RepoUrl $InstallRoot
        if ($LASTEXITCODE -ne 0) {
            throw "git clone failed."
        }
        return
    }

    $gitDir = Join-Path $InstallRoot ".git"
    if (-not (Test-Path $gitDir)) {
        throw "Install root '$InstallRoot' already exists but is not a git checkout."
    }

    Write-Host "Updating existing checkout in $InstallRoot"
    & git -C $InstallRoot checkout $Branch
    if ($LASTEXITCODE -ne 0) {
        throw "git checkout $Branch failed."
    }

    & git -C $InstallRoot pull --ff-only origin $Branch
    if ($LASTEXITCODE -ne 0) {
        throw "git pull --ff-only origin $Branch failed."
    }
}

function New-DesktopShortcut {
    param(
        [string]$ShortcutPath,
        [string]$TargetPath,
        [string]$Description
    )

    $workingDirectory = Split-Path -Parent $TargetPath
    $shell = New-Object -ComObject WScript.Shell
    $shortcut = $shell.CreateShortcut($ShortcutPath)
    $shortcut.TargetPath = $TargetPath
    $shortcut.WorkingDirectory = $workingDirectory
    $shortcut.Description = $Description
    $shortcut.IconLocation = "$TargetPath,0"
    $shortcut.Save()
}

if (-not $InstallRoot) {
    $InstallRoot = Join-Path $env:ProgramData "PocketEngine"
}

Require-Command "git"
Require-Command "cmake"

Ensure-GitCheckout -RepoUrl $RepoUrl -Branch $Branch -InstallRoot $InstallRoot

Push-Location $InstallRoot
try {
    Write-Host "Configuring Visual Studio build files"
    & cmake --preset vs2022-x64
    if ($LASTEXITCODE -ne 0) {
        throw "cmake configure failed."
    }

    Write-Host "Building Release"
    & cmake --build --preset vs-release
    if ($LASTEXITCODE -ne 0) {
        throw "cmake build failed."
    }
}
finally {
    Pop-Location
}

$runtimeExe = Join-Path $InstallRoot "build\vs2022-x64\src\app\runtime\Release\game.exe"
$editorExe = Join-Path $InstallRoot "build\vs2022-x64\src\app\editor\Release\pocket.exe"

if (-not (Test-Path $runtimeExe)) {
    throw "Runtime executable was not found at $runtimeExe"
}

if (-not (Test-Path $editorExe)) {
    throw "Editor executable was not found at $editorExe"
}

if (-not $SkipShortcuts) {
    $desktopDir = [Environment]::GetFolderPath("Desktop")
    $editorShortcut = Join-Path $desktopDir "PocketEngine Editor.lnk"
    $runtimeShortcut = Join-Path $desktopDir "PocketEngine Runtime.lnk"

    New-DesktopShortcut -ShortcutPath $editorShortcut `
        -TargetPath $editorExe `
        -Description "PocketEngine editor (Release)"
    New-DesktopShortcut -ShortcutPath $runtimeShortcut `
        -TargetPath $runtimeExe `
        -Description "PocketEngine runtime (Release)"

    Write-Host "Created desktop shortcuts:"
    Write-Host "  $editorShortcut"
    Write-Host "  $runtimeShortcut"
}

Write-Host "PocketEngine Release is ready:"
Write-Host "  Editor : $editorExe"
Write-Host "  Runtime: $runtimeExe"

if ($LaunchEditor) {
    Start-Process -FilePath $editorExe -WorkingDirectory (Split-Path -Parent $editorExe)
}
