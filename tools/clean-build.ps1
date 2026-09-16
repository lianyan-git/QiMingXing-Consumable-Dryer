# clean-build.ps1 - tidy build target dir.
#  default (post-build): keep .bin outputs + minimal EIDE caches; delete all big byproducts (axf/map/hex/s19/htm/logs, subdirs except .obj)
#  -Keep (pre-build):     also preserve .sct/.axf needed by current build; wipe the rest of stale leftovers
param([Parameter(Mandatory = $true)][string]$Dir, [switch]$Keep)

if (-not (Test-Path $Dir)) { exit 0 }

$keepFiles = @('*.bin','objs.db','objs.db.json','ref.json','compile_commands.json','builder.params','statistic.json','.lock')
$keepDirs  = @('.obj')

Get-ChildItem -LiteralPath $Dir -File | Where-Object {
    $n = $_.Name
    $base = [System.IO.Path]::GetFileNameWithoutExtension($n)
    if ($n -eq 'Project.bin') { return $true }                      # duplicate of dryer_*.bin
    if ($Keep -and ($n -like '*.sct' -or $n -like '*.axf')) { return $false }
    -not ($keepFiles | Where-Object { $n -like $_ })
} | ForEach-Object {
    Remove-Item -LiteralPath $_.FullName -Force
    Write-Output ("cleaned: " + $_.Name)
}

Get-ChildItem -LiteralPath $Dir -Directory | Where-Object { $keepDirs -notcontains $_.Name } | ForEach-Object {
    Remove-Item -LiteralPath $_.FullName -Recurse -Force
    Write-Output ("cleaned dir: " + $_.Name)
}

exit 0
