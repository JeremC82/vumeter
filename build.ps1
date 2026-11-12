# Compile le VU-metre (ESP32 classique) avec les memes bibliotheques que l'horloge
param([string]$Fqbn = "esp32:esp32:esp32")
$proj = $PSScriptRoot
$cli = "$env:LOCALAPPDATA\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"
$yaml = "C:\Users\Jeremy\Documents\projet vscode\clockV1\build\arduino-cli.yaml"
& $cli --config-file $yaml compile --fqbn $Fqbn --warnings default --output-dir "$proj\build" "$proj\vumeter" 2>&1 | Tee-Object "$proj\build\compile.log" | Select-String -Pattern 'vumeter\.ino:\d+:\d+: (warning|error)|error:|Le croquis|Les variables|Error during'
if ($LASTEXITCODE -ne 0) { "ECHEC compilation"; exit 1 }
Copy-Item "$proj\build\vumeter.ino.bin" "$proj\vumeter.bin" -Force
"vumeter.bin : {0} octets" -f (Get-Item "$proj\vumeter.bin").Length
