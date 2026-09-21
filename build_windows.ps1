$ErrorActionPreference = "Stop"

$configuration = "Release"
$outputSuffix = ""
$scriptArguments = @($args)
$exampleTargets = @(
  "echo_server",
  "echo_client",
  "manual_conn_client",
  "all_one_thread_server",
  "http_server",
  "http_client",
  "file_server",
  "file_client",
  "graceful_shutdown_server",
  "timer_poll"
)

function Show-Usage {
  Write-Host "Usage: .\build_windows.ps1 [debug|release] {clean|lib|examples|all|example_name}"
  Write-Host ""
  Write-Host "Actions:"
  Write-Host "  clean          Recreate the Windows CMake build directory"
  Write-Host "  lib            Build only the cxpnet library"
  Write-Host "  examples       Build all example targets"
  Write-Host "  all            Build the whole source tree"
  Write-Host "  example_name   Build one example target, such as echo_server"
}

function Invoke-CMake([string[]]$CMakeArguments) {
  & cmake @CMakeArguments
  if ($LASTEXITCODE -ne 0) {
    throw "cmake failed with exit code $LASTEXITCODE"
  }
}

if ($scriptArguments.Count -eq 0) {
  Show-Usage
  exit 1
}

if ($scriptArguments[0] -eq "debug") {
  $configuration = "Debug"
  $outputSuffix = "d"
  $scriptArguments = @($scriptArguments | Select-Object -Skip 1)
} elseif ($scriptArguments[0] -eq "release") {
  $scriptArguments = @($scriptArguments | Select-Object -Skip 1)
}

if ($scriptArguments.Count -eq 0) {
  Show-Usage
  exit 1
}

$action = $scriptArguments[0]
$repoRoot = [IO.Path]::GetFullPath($PSScriptRoot)
$buildRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot "build"))
$buildDir = [IO.Path]::GetFullPath((Join-Path $buildRoot "windows"))
$trimCharacters = [char[]]@([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
$allowedPrefix = $buildRoot.TrimEnd($trimCharacters) + [IO.Path]::DirectorySeparatorChar

if (-not $buildDir.StartsWith($allowedPrefix, [StringComparison]::OrdinalIgnoreCase)) {
  throw "Invalid build directory: $buildDir"
}

function Initialize-CMakeProject {
  Write-Host "Updating Windows CMake project..."
  Invoke-CMake -CMakeArguments @(
    "-S", $repoRoot,
    "-B", $buildDir,
    "-G", "Visual Studio 17 2022",
    "-A", "x64"
  )
}

function Invoke-Build([string[]]$BuildArguments) {
  $parallel = [Math]::Max(1, [Environment]::ProcessorCount)
  Invoke-CMake -CMakeArguments (@(
    "--build", $buildDir,
    "--config", $configuration,
    "--parallel", $parallel
  ) + $BuildArguments)
}

function Get-ExampleBinaryPath([string]$ExampleName) {
  $filename = "$ExampleName$outputSuffix.exe"
  return Join-Path $buildDir "examples/$ExampleName/$configuration/$filename"
}

if ($action -eq "clean") {
  Write-Host "Cleaning Windows build directory..."
  if (Test-Path -LiteralPath $buildDir) {
    Remove-Item -LiteralPath $buildDir -Recurse -Force
  }
  Initialize-CMakeProject
  Write-Host "Clean and CMake generation completed."
  exit 0
}

Initialize-CMakeProject

if ($action -eq "lib") {
  Write-Host "Building cxpnet library ($configuration)..."
  Invoke-Build -BuildArguments @("--target", "cxpnet")
  Write-Host "Library built successfully."
  exit 0
}

if ($action -eq "examples") {
  Write-Host "Building all examples ($configuration)..."
  foreach ($exampleName in $exampleTargets) {
    Invoke-Build -BuildArguments @("--target", $exampleName)
  }
  Write-Host "All examples built successfully. Binaries:"
  foreach ($exampleName in $exampleTargets) {
    Write-Host "  $(Get-ExampleBinaryPath $exampleName)"
  }
  exit 0
}

if ($action -eq "all") {
  Write-Host "Building cxpnet library and all examples ($configuration)..."
  Invoke-Build -BuildArguments @()
  Write-Host "Build completed. Example binaries:"
  foreach ($exampleName in $exampleTargets) {
    Write-Host "  $(Get-ExampleBinaryPath $exampleName)"
  }
  exit 0
}

$exampleDirectory = Join-Path $repoRoot "examples/$action"
if (-not (Test-Path -LiteralPath (Join-Path $exampleDirectory "CMakeLists.txt"))) {
  Write-Host "Error: example '$action' not found in examples/" -ForegroundColor Red
  Show-Usage
  exit 1
}

Write-Host "Building example: $action ($configuration)"
Invoke-Build -BuildArguments @("--target", $action)
Write-Host "Example '$action' built successfully."
Write-Host "Binary: $(Get-ExampleBinaryPath $action)"
