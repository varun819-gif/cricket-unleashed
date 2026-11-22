# Copies the engine-independent core (rules, physics, scorecards, recording)
# into the Unreal plugin. Run it after every `git pull`, before building in
# Unreal. The repository's core/ folder is the master copy: never edit the
# copies inside the plugin.
#   powershell -ExecutionPolicy Bypass -File tools\unreal\sync_core.ps1
$root = Resolve-Path (Join-Path $PSScriptRoot '..\..')
$mod  = Join-Path $root 'unreal\CricketUnleashed\Plugins\CricketCore\Source\CricketCore'
$hdr  = Join-Path $mod 'Public\cricket'
$src  = Join-Path $mod 'Private\Core'
Remove-Item -Recurse -Force $hdr, $src -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $hdr, $src | Out-Null
Copy-Item (Join-Path $root 'core\include\cricket\*.h') $hdr
Copy-Item (Join-Path $root 'core\src\*.cpp') $src
Write-Host ('Copied ' + (Get-ChildItem $hdr).Count + ' headers and ' + (Get-ChildItem $src).Count + ' sources into the CricketCore plugin.')
