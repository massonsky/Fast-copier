param(
    [switch]$ReleaseOnly,
    [switch]$DebugOnly
)

$ErrorActionPreference = "Stop"

function Ensure-ConanProfile {
    $profilePath = Join-Path $env:USERPROFILE ".conan2\profiles\default"
    if (-Not (Test-Path $profilePath)) {
        Write-Host "[setup_conan] Default profile missing. Running 'conan profile detect'."
        conan profile detect | Out-Null
    }
}

function Invoke-ConanInstall {
    param([string]$BuildType)

    Write-Host "[setup_conan] Running conan install for $BuildType"
    conan install . --build=missing -s build_type=$BuildType
}

Ensure-ConanProfile

if (-Not $ReleaseOnly) {
    Invoke-ConanInstall -BuildType "Debug"
}

if (-Not $DebugOnly) {
    Invoke-ConanInstall -BuildType "Release"
}