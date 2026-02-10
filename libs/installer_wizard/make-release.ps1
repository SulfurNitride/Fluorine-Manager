# Build everything first:
.\make.ps1

# Package:
$target = ".\installer_wizard\"

Remove-Item -Recurse -Force -ErrorAction Ignore $target
New-Item -Path $target -Type Directory | Out-Null
Copy-Item -Recurse -Path .\src\* -Exclude "*.ui" -Destination $target
Get-ChildItem -Recurse $target -Include "__pycache__" | Remove-Item -Recurse -Force
Copy-Item .\installer_wizard_en.ts, .\README.md, .\LICENSE $target

# Find the version:
$ctx = Get-Content .\src\installer.py | Select-String -Pattern "def version\(self\):" -Context 0, 1
$parts = $ctx.Context[0].PostContext.Split("(")[1].Trim(")").Split(",").Trim()
$version = Join-String -Separator "." -InputObject $parts[0..2]

if ($parts[3] -match "ALPHA") {
    $version += "a"
}
if ($parts[3] -match "BETA") {
    $version += "b"
}

# Create the zip:
$archive = "installer_wizard-$version.zip"
Remove-Item -Force -ErrorAction Ignore $archive
Compress-Archive -Path $target -DestinationPath $archive
Write-Output "Created archive $archive."
