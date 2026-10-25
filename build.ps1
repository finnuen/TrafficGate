# Builds TrafficGate.exe
# Output is named with version: dist\TrafficGate_v<version>.exe
#
#   .\build.ps1                 # builds and auto-bumps version if there are changes
#   .\build.ps1 -NoVersionBump  # rebuilds recorded version unchanged
#   .\build.ps1 -Version 1.0    # sets version explicitly

param(
    [switch]$NoVersionBump,
    [string]$Version = "",
    [string]$OutDir = (Join-Path $PSScriptRoot 'dist')
)

$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot

try {
    $headerPath = Join-Path $PSScriptRoot 'src\TrafficGate.h'
    $rcPath = Join-Path $PSScriptRoot 'src\TrafficGate.rc'

    $headerContent = Get-Content $headerPath -Raw
    if ($headerContent -notmatch '#define\s+APP_VERSION\s+L"([^"]+)"') {
        throw 'Could not find APP_VERSION in src/TrafficGate.h'
    }
    $curVer = $Matches[1]

    # Determine target version
    $targetVer = $curVer
    if ($Version -ne "") {
        $targetVer = $Version
    } elseif (-not $NoVersionBump) {
        # Auto-bump minor (e.g. 1.0 -> 1.1)
        if ($curVer -match '^(\d+)\.(\d+)$') {
            $major = [int]$Matches[1]
            $minor = [int]$Matches[2] + 1
            $targetVer = "$major.$minor"
        } elseif ($curVer -match '^(\d+)\.(\d+)\.(\d+)$') {
            $major = [int]$Matches[1]
            $minor = [int]$Matches[2]
            $patch = [int]$Matches[3] + 1
            $targetVer = "$major.$minor.$patch"
        }
    }

    Write-Host "Building TrafficGate v$targetVer ..."

    # Update src/TrafficGate.h if changed
    if ($targetVer -ne $curVer) {
        $headerContent = $headerContent -replace '#define\s+APP_VERSION\s+L"[^"]+"', "#define APP_VERSION         L`"$targetVer`""
        $headerContent = $headerContent -replace '#define\s+APP_VERSION_STR\s+"[^"]+"', "#define APP_VERSION_STR     `"$targetVer`""
        Set-Content -Path $headerPath -Value $headerContent -NoNewline
    }

    # Split version for Windows RC (major,minor,0,0)
    $vParts = $targetVer.Split('.')
    $rcMajor = if ($vParts.Length -ge 1) { $vParts[0] } else { "1" }
    $rcMinor = if ($vParts.Length -ge 2) { $vParts[1] } else { "0" }
    $rcPatch = if ($vParts.Length -ge 3) { $vParts[2] } else { "0" }
    $rcBuild = if ($vParts.Length -ge 4) { $vParts[3] } else { "0" }
    $rcQuad = "$rcMajor,$rcMinor,$rcPatch,$rcBuild"

    $rcContent = Get-Content $rcPath -Raw
    $rcContent = $rcContent -replace 'FILEVERSION\s+[\d,]+', "FILEVERSION     $rcQuad"
    $rcContent = $rcContent -replace 'PRODUCTVERSION\s+[\d,]+', "PRODUCTVERSION  $rcQuad"
    $rcContent = $rcContent -replace 'VALUE\s+"FileVersion",\s*"[^"]+"', "VALUE `"FileVersion`",      `"$targetVer`""
    $rcContent = $rcContent -replace 'VALUE\s+"ProductVersion",\s*"[^"]+"', "VALUE `"ProductVersion`",   `"$targetVer`""
    $rcContent = $rcContent -replace 'VALUE\s+"OriginalFilename",\s*"[^"]+"', "VALUE `"OriginalFilename`", `"TrafficGate_v$targetVer.exe`""
    Set-Content -Path $rcPath -Value $rcContent -NoNewline

    # Compile resource
    $resObj = Join-Path $PSScriptRoot 'src\TrafficGate_res.o'
    Write-Host "Compiling resource $rcPath ..."
    & windres -i $rcPath -o $resObj -I (Join-Path $PSScriptRoot 'src')
    if ($LASTEXITCODE -ne 0) { throw "windres failed with exit code $LASTEXITCODE" }

    # Compile binary with g++
    New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
    $binaryName = "TrafficGate_v$targetVer.exe"
    $targetExe = Join-Path $OutDir $binaryName

    $flags = @(
        '-std=c++17', '-O2', '-s', '-static',
        '-static-libgcc', '-static-libstdc++',
        '-fno-exceptions', '-fno-rtti',
        '-mwindows', '-municode',
        '-ffunction-sections', '-fdata-sections',
        '-DNDEBUG', '-DUNICODE', '-D_UNICODE',
        '-DWINVER=0x0601', '-D_WIN32_WINNT=0x0601'
    )
    $link = @('-Wl,--gc-sections')
    $libs = @(
        '-lgdiplus', '-lgdi32', '-liphlpapi',
        '-lshlwapi', '-lshell32', '-ladvapi32',
        '-lcomctl32', '-lole32'
    )

    $srcCpp = Join-Path $PSScriptRoot 'src\TrafficGate.cpp'
    Write-Host "Compiling $binaryName ..."
    & g++ @flags -o $targetExe $srcCpp $resObj @link @libs
    if ($LASTEXITCODE -ne 0) { throw "g++ failed with exit code $LASTEXITCODE" }

    # Clean intermediate res object
    if (Test-Path $resObj) {
        Add-Type -AssemblyName Microsoft.VisualBasic
        [Microsoft.VisualBasic.FileIO.FileSystem]::DeleteFile($resObj, 'OnlyErrorDialogs', 'SendToRecycleBin')
    }

    # Verify binary and check dependencies
    if (-not (Test-Path $targetExe)) {
        throw "Binary not found at $targetExe"
    }

    $fi = Get-Item $targetExe
    Write-Host ("Build succeeded: {0} ({1:N0} bytes)" -f $targetExe, $fi.Length)

    # Check that no gcc runtime dlls leaked
    $imports = & objdump -p $targetExe | Select-String "DLL Name" | ForEach-Object { $_.Line.Trim() }
    foreach ($bad in @('libstdc++', 'libgcc', 'libwinpthread')) {
        if ($imports -like "*$bad*") {
            throw "Static link failed: $bad found in executable imports!"
        }
    }
    Write-Host "Verified import table: clean static binary linking only system DLLs."
}
finally {
    Pop-Location
}
