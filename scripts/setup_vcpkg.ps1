param(
    [string[]]$Triplets,
    [switch]$CleanOutdated
)

$ErrorActionPreference = "Stop"

$runningOnWindows = [System.Runtime.InteropServices.RuntimeInformation]::IsOSPlatform([System.Runtime.InteropServices.OSPlatform]::Windows)

function Resolve-VcpkgExecutable {
    param([bool]$IsWindowsPlatform)

    $exeName = if ($IsWindowsPlatform) { "vcpkg.exe" } else { "vcpkg" }

    if ($env:VCPKG_ROOT) {
        $candidate = Join-Path $env:VCPKG_ROOT $exeName
        if (Test-Path $candidate) {
            return $candidate
        }
    }

    $cmd = Get-Command vcpkg -ErrorAction SilentlyContinue
    if ($cmd) {
        return $cmd.Source
    }

    throw "vcpkg executable not found. Set VCPKG_ROOT or add vcpkg to PATH."
}

$vcpkgExe = Resolve-VcpkgExecutable -IsWindowsPlatform $runningOnWindows

if (-not $Triplets -or $Triplets.Count -eq 0) {
    if ($runningOnWindows) {
        $Triplets = @("x64-windows")
    }
    else {
        $Triplets = @("x64-linux")
    }
}

foreach ($triplet in $Triplets) {
    if ($CleanOutdated) {
        Write-Host "[setup_vcpkg] Removing outdated packages for triplet '$triplet'."
        & $vcpkgExe remove --outdated --triplet $triplet | Out-Null
    }

    Write-Host "[setup_vcpkg] Installing manifest dependencies for triplet '$triplet'."
    & $vcpkgExe install --triplet $triplet | Out-Null
}
