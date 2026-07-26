param(
    [ValidateSet(0, 1)]
    [int]$DebugCommands = 1
)

$ErrorActionPreference = "Stop"

$workspace = Split-Path -Parent $PSScriptRoot
$subscriptionId = "517378dd-26db-4ba0-b115-c1c510035c72"
$endpoint = "https://eus.codesigning.azure.net"
$accountName = "dubious-latchkey"
$profileName = "TheMetaGamePublic"

function Assert-LastExitCode([string]$operation) {
    if ($LASTEXITCODE -ne 0) {
        throw "$operation failed with exit code $LASTEXITCODE."
    }
}

function Assert-FileAvailable([string]$path) {
    if (-not (Test-Path -LiteralPath $path)) {
        return
    }

    try {
        $stream = [System.IO.File]::Open(
            $path,
            [System.IO.FileMode]::Open,
            [System.IO.FileAccess]::ReadWrite,
            [System.IO.FileShare]::None)
        $stream.Dispose()
    }
    catch {
        throw "Close '$path' before building a signed release."
    }
}

function Find-AzureCli {
    $command = Get-Command az -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    $installedPath =
        "C:\Program Files\Microsoft SDKs\Azure\CLI2\wbin\az.cmd"
    if (Test-Path -LiteralPath $installedPath) {
        return $installedPath
    }

    throw "Azure CLI is not installed. Install Microsoft.AzureCLI with winget."
}

function Find-SignTool {
    $sdkBin = "C:\Program Files (x86)\Windows Kits\10\bin"
    $tools = Get-ChildItem -LiteralPath $sdkBin -Directory |
        Where-Object { $_.Name -match "^\d+\.\d+\.\d+\.\d+$" } |
        ForEach-Object {
            $candidate = Join-Path $_.FullName "x64\signtool.exe"
            if (Test-Path -LiteralPath $candidate) {
                Get-Item -LiteralPath $candidate
            }
        } |
        Sort-Object { [version]$_.Directory.Parent.Name } -Descending

    $tool = $tools | Select-Object -First 1
    if (-not $tool) {
        throw "A recent x64 Windows SDK SignTool installation was not found."
    }
    return $tool.FullName
}

function Invoke-Sign([string]$signTool, [string]$dlib,
                     [string]$metadata, [string]$file) {
    & $signTool sign `
        /v `
        /fd SHA256 `
        /tr "http://timestamp.acs.microsoft.com" `
        /td SHA256 `
        /dlib $dlib `
        /dmdf $metadata `
        $file
    Assert-LastExitCode "Signing $file"

    & $signTool verify /pa /all /v $file
    Assert-LastExitCode "Signature verification for $file"
}

Push-Location $workspace
try {
    $gameExecutable = Join-Path $workspace "release\the-meta-game.exe"
    $resetExecutable = Join-Path $workspace "release\reset-game.exe"
    Assert-FileAvailable $gameExecutable
    Assert-FileAvailable $resetExecutable

    & (Join-Path $workspace "build.bat") "DEBUG_COMMANDS=$DebugCommands"
    Assert-LastExitCode "Build"

    $azureCli = Find-AzureCli
    $azureCliDirectory = Split-Path -Parent $azureCli
    if ($env:PATH -notlike "*$azureCliDirectory*") {
        $env:PATH = "$azureCliDirectory;$env:PATH"
    }

    & $azureCli account set --subscription $subscriptionId
    if ($LASTEXITCODE -ne 0) {
        throw "Azure CLI is not authenticated. Run 'az login' once, then retry."
    }
    & $azureCli account get-access-token `
        --subscription $subscriptionId `
        --resource "https://management.azure.com/" `
        --output none 2>$null
    if ($LASTEXITCODE -ne 0) {
        throw "Azure CLI login has expired. Run 'az login' once, then retry."
    }

    $signTool = Find-SignTool
    $dlib = Join-Path $env:LOCALAPPDATA `
        "Microsoft\MicrosoftArtifactSigningClientTools\Azure.CodeSigning.Dlib.dll"
    if (-not (Test-Path -LiteralPath $dlib)) {
        throw "Artifact Signing Client Tools are not installed."
    }

    $metadataPath =
        Join-Path $workspace "build\artifact-signing-metadata.json"
    $metadata = [ordered]@{
        Endpoint = $endpoint
        CodeSigningAccountName = $accountName
        CertificateProfileName = $profileName
        CorrelationId = "the-meta-game-release"
        ExcludeCredentials = @(
            "EnvironmentCredential"
            "WorkloadIdentityCredential"
            "ManagedIdentityCredential"
            "SharedTokenCacheCredential"
            "VisualStudioCredential"
            "VisualStudioCodeCredential"
            "AzurePowerShellCredential"
            "AzureDeveloperCliCredential"
            "InteractiveBrowserCredential"
        )
    } | ConvertTo-Json
    [System.IO.File]::WriteAllText($metadataPath, $metadata)

    Invoke-Sign $signTool $dlib $metadataPath $gameExecutable
    Invoke-Sign $signTool $dlib $metadataPath $resetExecutable

    $gameFile = Get-Item -LiteralPath $gameExecutable
    $version = $gameFile.VersionInfo.ProductVersion
    if (-not $version) {
        throw "The game executable has no ProductVersion metadata."
    }
    if ($version.EndsWith(".0")) {
        $version = $version.Substring(0, $version.Length - 2)
    }

    $distRoot = Join-Path $workspace "dist"
    $packageName = "the-meta-game-$version-windows"
    $packageRoot = Join-Path $distRoot $packageName
    $zipPath = Join-Path $distRoot "$packageName-signed.zip"

    $expectedPrefix =
        [System.IO.Path]::GetFullPath((Join-Path $workspace "dist")) +
        [System.IO.Path]::DirectorySeparatorChar
    $resolvedPackageRoot = [System.IO.Path]::GetFullPath($packageRoot)
    if (-not $resolvedPackageRoot.StartsWith(
            $expectedPrefix,
            [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to package outside the workspace dist directory."
    }

    if (Test-Path -LiteralPath $packageRoot) {
        Remove-Item -LiteralPath $packageRoot -Recurse -Force
    }
    if (Test-Path -LiteralPath $zipPath) {
        Remove-Item -LiteralPath $zipPath -Force
    }

    New-Item -ItemType Directory -Path $packageRoot | Out-Null
    Copy-Item -LiteralPath $gameExecutable -Destination $packageRoot
    Copy-Item -LiteralPath (Join-Path $workspace "release\golden_audio") `
        -Destination $packageRoot -Recurse
    Copy-Item -LiteralPath (Join-Path $workspace "release\golden_scripts") `
        -Destination $packageRoot -Recurse
    Copy-Item -LiteralPath (Join-Path $workspace "README.md") `
        -Destination $packageRoot
    Copy-Item -LiteralPath (Join-Path $workspace "LICENSE") `
        -Destination $packageRoot

    Compress-Archive -LiteralPath $packageRoot `
        -DestinationPath $zipPath -CompressionLevel Optimal

    $signature = Get-AuthenticodeSignature $gameExecutable
    if ($signature.Status -ne "Valid") {
        throw "The packaged game signature is not valid."
    }

    Write-Host ""
    Write-Host "Signed release ready:"
    Write-Host $zipPath
    Get-FileHash -LiteralPath $zipPath -Algorithm SHA256 |
        Format-List Algorithm, Hash, Path
}
finally {
    Pop-Location
}
