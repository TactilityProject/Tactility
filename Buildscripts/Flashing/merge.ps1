param(
    # "--chip esp32s3" is irrelevant, just need to be added, fallback to "esp32s3"
    [string]$chip = "esp32s3"
)

if ($null -eq (Get-Command "esptool" -ErrorAction SilentlyContinue))
{
    Write-Host "Unable to find esptool in your path. Make sure you have Python installed and on your path. Then run `pip install esptool`."
    exit 1
}

# Create merge command based on partitions
$json = Get-Content .\Binaries\flasher_args.json -Raw | ConvertFrom-Json
$jsonClean = $json.flash_files -replace '[\{\}\@\;]', ''
$jsonClean = $jsonClean -replace '[\=]', ' '

$mergeArgs = @('--chip', $chip, 'merge-bin', '--output', 'merged_binary.bin') + ($jsonClean -split '\s+' | Where-Object { $_ })
Push-Location Binaries
& esptool @mergeArgs
$exitCode = $LASTEXITCODE
Pop-Location

if ($exitCode -ne 0) {
    exit $exitCode
}

