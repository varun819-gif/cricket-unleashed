# Cricket Unleashed - "Can this PC run Unreal Engine 5?" check.
# Free, read-only: it only READS hardware information and prints a verdict.
# It installs nothing, changes nothing and sends nothing anywhere.
#
# How to run (Windows 10/11):
#   1. Double-click check_pc.bat in this folder,  OR
#   2. Start -> PowerShell, then:
#        powershell -ExecutionPolicy Bypass -File check_pc.ps1
# Then send the printed lines (or the file pc_check_result.txt next to this script).

$ErrorActionPreference = 'SilentlyContinue'
$lines = New-Object System.Collections.Generic.List[string]
function Say($t) { $lines.Add($t); Write-Host $t }

# ---- read the hardware ----
$os   = Get-CimInstance Win32_OperatingSystem
$cpu  = Get-CimInstance Win32_Processor | Select-Object -First 1
$ramGB = [math]::Round((Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory / 1GB, 1)
$gpus = @(Get-CimInstance Win32_VideoController)

# Graphics memory: Win32_VideoController stops at 4 GB, so read the real
# size from the display driver's registry entry when it is there.
$vramByName = @{}
Get-ChildItem 'HKLM:\SYSTEM\CurrentControlSet\Control\Class\{4d36e968-e325-11ce-bfc1-08002be10318}' |
  ForEach-Object {
    $p = Get-ItemProperty $_.PSPath
    $q = $p.'HardwareInformation.qwMemorySize'
    if ($q -and $p.DriverDesc) { $vramByName[$p.DriverDesc] = [math]::Round([double]$q / 1GB, 1) }
  }

Say '=== Cricket Unleashed: PC check for Unreal Engine 5 ==='
Say ('Windows     : ' + $os.Caption + ' (' + $os.OSArchitecture + ')')
Say ('Processor   : ' + $cpu.Name.Trim() + ' - ' + $cpu.NumberOfCores + ' cores / ' + $cpu.NumberOfLogicalProcessors + ' threads')
Say ('Memory (RAM): ' + $ramGB + ' GB')

$bestVram = 0; $dedicated = $false
foreach ($g in $gpus) {
  $v = $vramByName[$g.Name]
  if (-not $v) { $v = [math]::Round([double]$g.AdapterRAM / 1GB, 1) }
  $integrated = $g.Name -match 'Intel\(R\) (UHD|HD|Iris)|Radeon\(TM\) Graphics|Radeon Graphics|Microsoft Basic'
  $kind = if ($integrated) { 'integrated (shares RAM)' } else { 'dedicated' }
  Say ('Graphics    : ' + $g.Name + ' - ' + $v + ' GB, ' + $kind)
  if (-not $integrated) { $dedicated = $true; if ($v -gt $bestVram) { $bestVram = $v } }
}

$disks = Get-CimInstance Win32_LogicalDisk -Filter 'DriveType=3'
foreach ($d in $disks) { Say ('Disk ' + $d.DeviceID + '     : ' + [math]::Round($d.FreeSpace / 1GB) + ' GB free of ' + [math]::Round($d.Size / 1GB) + ' GB') }
$maxFree = ($disks | Measure-Object -Property FreeSpace -Maximum).Maximum / 1GB
$ssd = @(Get-PhysicalDisk | Where-Object MediaType -eq 'SSD').Count -gt 0
Say ('SSD present : ' + $ssd)

# ---- verdict (Epic's published guidance for UE5 development, simplified) ----
#   Recommended: 64-bit Windows 10/11, quad-core 2.5 GHz+, 32 GB RAM,
#                DirectX 12 graphics card with 8 GB, SSD with ~100 GB+ free.
$problems = New-Object System.Collections.Generic.List[string]
$warnings = New-Object System.Collections.Generic.List[string]
if ($os.OSArchitecture -notmatch '64') { $problems.Add('Windows is not 64-bit') }
if ($cpu.NumberOfCores -lt 4) { $warnings.Add('fewer than 4 processor cores: building C++ and shaders will be very slow') }
if ($ramGB -lt 15) { $problems.Add('less than 16 GB RAM: the Unreal editor will be very slow or crash') }
elseif ($ramGB -lt 31) { $warnings.Add('16 GB RAM: works for a small prototype, 32 GB is recommended') }
if (-not $dedicated) { $problems.Add('no dedicated graphics card: integrated graphics cannot run the UE5 editor usefully') }
elseif ($bestVram -lt 6) { $warnings.Add('graphics card has less than 6 GB: use low settings / no Lumen') }
if ($maxFree -lt 100) { $warnings.Add('less than 100 GB free on every disk: Unreal + the project need 100 GB or more') }
if (-not $ssd) { $warnings.Add('no SSD found: loading and building will be slow') }

Say ''
if ($problems.Count -gt 0) {
  Say 'VERDICT: NOT PRACTICAL for Unreal Engine 5 3D development on this PC.'
  foreach ($p in $problems) { Say ('  - ' + $p) }
  foreach ($w in $warnings) { Say ('  - (also) ' + $w) }
  Say 'This PC is still fine for everything else in the project: the game server,'
  Say 'the phone controller, single player in the browser, and the tests.'
} elseif ($warnings.Count -gt 0) {
  Say 'VERDICT: POSSIBLE for a small, simple 3D prototype, with limits.'
  foreach ($w in $warnings) { Say ('  - ' + $w) }
} else {
  Say 'VERDICT: GOOD - this PC meets the recommended level for UE5 development.'
}
$out = Join-Path $PSScriptRoot 'pc_check_result.txt'
$lines | Out-File -FilePath $out -Encoding utf8
Write-Host ''
Write-Host ('Saved to ' + $out)
