param(
    $port
)

if ((Get-Command "esptool" -ErrorAction SilentlyContinue) -eq $null)
{
    Write-Host "Unable to find esptool in your path. Make sure you have Python installed and on your path. Then run `pip install esptool`."
}

# Create merge command based on partitions
$json = Get-Content .\Binaries\flasher_args.json -Raw | ConvertFrom-Json
$jsonClean = $json.flash_files -replace '[\{\}\@\;]', ''
$jsonClean = $jsonClean -replace '[\=]', ' '

cd Binaries
# "--chip esp32s3" is irrelevant, just need to be added, works for all variant
$command = "esptool --chip esp32s3 merge-bin --output merged_binary.bin $jsonClean"
Invoke-Expression $command
cd ..

