# Compile le VU-metre (ESP32 classique) avec les memes bibliotheques que l'horloge
#   .\build.ps1        -> vumeter.bin       (demo par cavalier GPIO18)
#   .\build.ps1 -Demo  -> vumeter_demo.bin  (demo toujours active, sans cavalier)
param([string]$Fqbn = "esp32:esp32:esp32", [switch]$Demo)
$proj = $PSScriptRoot
$cli = "$env:LOCALAPPDATA\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"
$yaml = "C:\Users\Jeremy\Documents\projet vscode\clockV1\build\arduino-cli.yaml"
$out = if ($Demo) { "$proj\build\demo" } else { "$proj\build" }
$bin = if ($Demo) { "$proj\vumeter_demo.bin" } else { "$proj\vumeter.bin" }
$extra = if ($Demo) { @("--build-property", "compiler.cpp.extra_flags=-DVU_DEMO_FORCE") } else { @() }
New-Item -ItemType Directory -Force $out | Out-Null
& $cli --config-file $yaml compile --fqbn $Fqbn --warnings default @extra --output-dir $out "$proj\vumeter" 2>&1 | Tee-Object "$out\compile.log" | Select-String -Pattern 'vumeter\.ino:\d+:\d+: (warning|error)|error:|Le croquis|Les variables|Error during'
if ($LASTEXITCODE -ne 0) { "ECHEC compilation"; exit 1 }
Copy-Item "$out\vumeter.ino.bin" $bin -Force
"{0} : {1} octets" -f (Split-Path $bin -Leaf), (Get-Item $bin).Length
