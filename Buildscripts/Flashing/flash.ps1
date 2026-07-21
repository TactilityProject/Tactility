param(
    $port
)

if ((Get-Command "esptool" -ErrorAction SilentlyContinue) -eq $null)
{
    Write-Host "Unable to find esptool in your path. Make sure you have Python installed and on your path. Then run `pip install esptool`."
}

# Create flash command based on partitions
$json = Get-Content .\Binaries\flasher_args.json -Raw | ConvertFrom-Json
$jsonClean = $json.flash_files -replace '[\{\}\@\;]', ''
$jsonClean = $jsonClean -replace '[\=]', ' '

cd Binaries
$command = "esptool --port $port erase-flash"
Invoke-Expression $command
$command = "esptool --port $port write-flash $jsonClean"
Invoke-Expression $command
cd ..

