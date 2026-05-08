# Downloads basic example files from HuggingFace
# No external dependencies required — uses built-in PowerShell (Windows 5.1+)

$HF_Base = "https://huggingface.co/datasets/lucianparisi/atmos-data/resolve/main"

$Files = @(
    @{ Url = "$HF_Base/driveExample1.wav";   Name = "Echo_Example_1.wav" },
    @{ Url = "$HF_Base/driveExample2.wav";   Name = "Echo_Example_2.wav" },
    @{ Url = "$HF_Base/SWALE-ATMOS-LFE.wav"; Name = "LucianParisi_Swale_Atmos_Mix.wav" }
)

$ScriptDir     = Split-Path -Parent $MyInvocation.MyCommand.Definition
$SourceDataDir = Join-Path $ScriptDir "../sourceData"

if (-not (Test-Path $SourceDataDir)) {
    New-Item -ItemType Directory -Path $SourceDataDir | Out-Null
}

foreach ($File in $Files) {
    $OutputPath = Join-Path $SourceDataDir $File.Name

    Write-Host ""
    Write-Host "Downloading example file to: $OutputPath"
    Write-Host "This may take a while for large files..."
    Write-Host ""

    Invoke-WebRequest -Uri $File.Url -OutFile $OutputPath

    Write-Host ""
    Write-Host "Download complete!"
    Write-Host "Saved to: $OutputPath"

    if (Test-Path $OutputPath) {
        $SizeMB = [math]::Round((Get-Item $OutputPath).Length / 1MB, 1)
        Write-Host "File verified: $SizeMB MB"
    } else {
        Write-Warning "File not found at $OutputPath"
    }
}
