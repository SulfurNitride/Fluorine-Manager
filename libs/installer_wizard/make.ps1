# Install the lib:
pip install --target=.\src\lib --upgrade -r .\plugin-requirements.txt | Out-Null

# Convert ui files:
Get-ChildItem -Recurse -File -Include "*.ui" | ForEach-Object {
    pyuic5 $_ -o ([io.path]::ChangeExtension($_.FullName, "py"))
}

# Generate the .ts file:
pylupdate5 (Get-ChildItem src -Exclude lib  | Get-ChildItem -Recurse -File -Include "*.py") -ts installer_wizard_en.ts
