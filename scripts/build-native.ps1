[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',

    [ValidateSet('x64')]
    [string]$Architecture = 'x64',

    [string]$BuildDirectory = '',

    [ValidateSet('stable', 'preview')]
    [string]$UpdateChannel = 'stable',

    # 本地开发可跳过 clean-first 全量重建；CI 和发布始终使用干净构建。
    [switch]$NoClean,

    [switch]$RunSmokeTest
)

$ErrorActionPreference = 'Stop'

# Windows environment variable names are case-insensitive, but a process can be
# started with the same name twice in different casing (NO_PROXY and no_proxy,
# PATH and Path). MSBuild's .NET Framework host copies the inherited block into
# a case-insensitive dictionary and fails with MSB6001 before it launches
# CL.exe. Windows offers no way to remove one of those duplicates from inside a
# process, so every build command gets a rebuilt block instead: names are
# compared case-insensitively and the upper-case spelling wins.
function New-CleanEnvironmentTable {
    $table = [System.Collections.Generic.Dictionary[string, string]]::new(
        [System.StringComparer]::OrdinalIgnoreCase)
    foreach ($entry in [Environment]::GetEnvironmentVariables().GetEnumerator()) {
        $name = [string]$entry.Key
        if (-not $table.ContainsKey($name) -or $name -ceq $name.ToUpperInvariant()) {
            $table[$name] = [string]$entry.Value
        }
    }
    return $table
}

$cleanEnvironment = New-CleanEnvironmentTable

function Invoke-NativeCommand {
    param(
        [Parameter(Mandatory)]
        [string]$Executable,

        [Parameter(Mandatory)]
        [string[]]$Arguments
    )

    # Launch through ProcessStartInfo so the child receives the cleaned block;
    # stdout and stderr still stream straight to this console.
    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $Executable
    $startInfo.UseShellExecute = $false
    foreach ($argument in $Arguments) {
        $startInfo.ArgumentList.Add($argument)
    }
    $startInfo.Environment.Clear()
    foreach ($entry in $cleanEnvironment.GetEnumerator()) {
        $startInfo.Environment[$entry.Key] = $entry.Value
    }

    $process = [System.Diagnostics.Process]::Start($startInfo)
    try {
        $process.WaitForExit()
        $exitCode = $process.ExitCode
    } finally {
        $process.Dispose()
    }
    if ($exitCode -ne 0) {
        throw "Command failed with exit code $exitCode`: $Executable"
    }
}

function Invoke-GuiSelfTest {
    param(
        [Parameter(Mandatory)]
        [string]$Executable,

        [Parameter(Mandatory)]
        [string]$Argument,

        # A self-test that opens a modal dialog would otherwise hang CI until the
        # job-level timeout, so every run gets a bounded wait.
        [ValidateRange(10, 900)]
        [int]$TimeoutSeconds = 180
    )

    # The executable reports the failing assertion on stderr; capture it so CI
    # says which check broke instead of only 'exit code 1'.
    $errorLog = [System.IO.Path]::GetTempFileName()
    try {
        $process = Start-Process -FilePath $Executable -ArgumentList $Argument `
            -WindowStyle Hidden -PassThru -RedirectStandardError $errorLog
        try {
            if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
                $process.Kill()
                $null = $process.WaitForExit(5000)
                throw "Self-test did not finish within $TimeoutSeconds seconds: $Argument"
            }
            $exitCode = $process.ExitCode
        } finally {
            $process.Dispose()
        }
        if ($exitCode -ne 0) {
            $details = ''
            if (Test-Path -LiteralPath $errorLog) {
                $details = Get-Content -LiteralPath $errorLog -Raw -ErrorAction SilentlyContinue
            }
            $message = "Self-test failed with exit code $($exitCode)`: $Argument"
            if (-not [string]::IsNullOrWhiteSpace($details)) {
                $message = $message + [Environment]::NewLine + $details.Trim()
            }
            throw $message
        }
    } finally {
        Remove-Item -LiteralPath $errorLog -ErrorAction SilentlyContinue
    }
}

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$sourceDirectory = Join-Path $repositoryRoot 'native'

function Get-NativeSourceSnapshot {
    $entries = @(
        Get-ChildItem -LiteralPath $sourceDirectory -Recurse -File |
            Where-Object {
                $_.Name -eq 'CMakeLists.txt' -or
                $_.Extension -in @('.cpp', '.hpp', '.h', '.rc')
            } |
            Sort-Object FullName |
            ForEach-Object {
                $relativePath = $_.FullName.Substring($sourceDirectory.Length).TrimStart('\')
                $hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
                "$relativePath=$hash"
            }
    )
    return [string]::Join("`n", $entries)
}

$sourceSnapshot = Get-NativeSourceSnapshot

if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    $BuildDirectory = Join-Path $repositoryRoot 'build\native'
} else {
    $BuildDirectory = [System.IO.Path]::GetFullPath($BuildDirectory)
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw 'Visual Studio Installer (vswhere.exe) was not found.'
}

$instances = @(
    & $vswhere `
        -latest `
        -products '*' `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -format json |
        ConvertFrom-Json
)

if ($instances.Count -eq 0) {
    throw 'Visual Studio with the Desktop C++ toolchain was not found.'
}

$instance = $instances[0]
$visualStudioPath = [string]$instance.installationPath
$visualStudioMajor = ([version]$instance.installationVersion).Major

$generator = switch ($visualStudioMajor) {
    18 { 'Visual Studio 18 2026' }
    17 { 'Visual Studio 17 2022' }
    default { throw "Unsupported Visual Studio major version: $visualStudioMajor" }
}

$bundledCMake = Join-Path $visualStudioPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
if (Test-Path -LiteralPath $bundledCMake) {
    $cmake = $bundledCMake
} else {
    $cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
    if ($null -eq $cmakeCommand) {
        throw 'CMake was not found in Visual Studio or PATH.'
    }
    $cmake = $cmakeCommand.Source
}

Write-Host "Configuring native build with $generator ($Architecture)..."
$configureArguments = @(
    '-S', $sourceDirectory,
    '-B', $BuildDirectory,
    '-G', $generator,
    '-A', $Architecture,
    "-DMINITRANS_UPDATE_CHANNEL=$UpdateChannel"
)
Invoke-NativeCommand -Executable $cmake -Arguments $configureArguments

Write-Host "Building $Configuration..."
$buildArguments = @(
    '--build', $BuildDirectory,
    '--config', $Configuration,
    '--parallel'
)
if (-not $NoClean) {
    $buildArguments += '--clean-first'
}
Invoke-NativeCommand -Executable $cmake -Arguments $buildArguments

if ($sourceSnapshot -ne (Get-NativeSourceSnapshot)) {
    throw 'Native sources changed while compilation was running. Discard this build and run the command again.'
}

$executable = Join-Path $BuildDirectory "$Configuration\MiniTranslator.exe"
if (-not (Test-Path -LiteralPath $executable)) {
    throw "The expected executable was not produced: $executable"
}

if ($RunSmokeTest) {
    Write-Host 'Running native startup smoke test...'
    Invoke-GuiSelfTest -Executable $executable -Argument '--self-test'
    Write-Host 'Running native updater smoke test...'
    Invoke-GuiSelfTest -Executable $executable -Argument '--update-self-test'
}

Write-Host "Built: $executable"
