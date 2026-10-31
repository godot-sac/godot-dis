# Links godot-milsymbol's addon into this project for the demo (symbols on the tactical map).
# Usage (from the repo root): .\tools\link_milsymbol.ps1 [-Source ..\godot-milsymbol\addons\milsymbol]
# Creates addons\milsymbol as a directory junction (no admin rights needed); it is git-ignored.
# Without it the demo still runs and draws simple markers instead of military symbols.
param(
	[string]$Source = (Join-Path $PSScriptRoot "..\..\godot-milsymbol\addons\milsymbol")
)

$target = Join-Path $PSScriptRoot "..\addons\milsymbol"
$Source = (Resolve-Path $Source -ErrorAction Stop).Path
if (Test-Path $target) {
	Write-Host "addons\milsymbol already exists: $((Get-Item $target).Target)"
	exit 0
}
New-Item -ItemType Junction -Path $target -Target $Source | Out-Null
Write-Host "linked addons\milsymbol -> $Source"
Write-Host "Run once so Godot registers its classes: <godot> --headless --path . --import"
