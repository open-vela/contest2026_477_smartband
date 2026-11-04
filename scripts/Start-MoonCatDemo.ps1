param([switch]$Panel)
$ErrorActionPreference = 'Stop'
try {
    $scriptPath = Join-Path $PSScriptRoot 'mooncat_demo.py'
    $candidates = @(
        $env:MOONCAT_PYTHON,
        (Join-Path $env:USERPROFILE '.cache/codex-runtimes/codex-primary-runtime/dependencies/python/pythonw.exe'),
        (Get-Command pythonw.exe -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source -First 1)
    )
    $pythonPath = $candidates | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Leaf) } | Select-Object -First 1
    if (-not $pythonPath) { throw 'Python with tkinter is required. Set MOONCAT_PYTHON to pythonw.exe.' }
    $arguments = '"' + $scriptPath + '"'
    if (-not $Panel) { $arguments += ' --autostart' }
    Start-Process -FilePath $pythonPath -ArgumentList $arguments -WorkingDirectory $PSScriptRoot -WindowStyle Hidden
} catch {
    Add-Type -AssemblyName System.Windows.Forms
    [System.Windows.Forms.MessageBox]::Show($_.Exception.Message, 'MoonCat Demo') | Out-Null
    exit 1
}
