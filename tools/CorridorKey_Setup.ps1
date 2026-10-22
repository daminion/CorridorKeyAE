# CorridorKey AE - setup menu
# Installs CorridorKey 2.0 (the Python keyer) and points the AE plug-in at it.
# Run via CorridorKey_Setup.bat (double-click).

$ErrorActionPreference = 'Continue'
$RepoUrl      = 'https://github.com/nikopueringer/CorridorKey2.0.git'
$TorchIndex   = 'https://download.pytorch.org/whl/cu128'
$SettingsDir  = Join-Path $env:APPDATA 'CorridorKeyAE'
$SettingsFile = Join-Path $SettingsDir 'settings.ini'

# ---------------------------------------------------------------- helpers ---

function Get-Setting([string]$key) {
    if (-not (Test-Path $SettingsFile)) { return '' }
    foreach ($line in Get-Content $SettingsFile) {
        if ($line -match "^\s*$key\s*=(.*)$") { return $Matches[1].Trim().Trim('"') }
    }
    return ''
}

function Set-Setting([string]$key, [string]$value) {
    if (-not (Test-Path $SettingsDir)) { New-Item -ItemType Directory -Path $SettingsDir | Out-Null }
    $lines = @()
    if (Test-Path $SettingsFile) { $lines = @(Get-Content $SettingsFile) }
    $found = $false
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match "^\s*$key\s*=") { $lines[$i] = "$key=$value"; $found = $true }
    }
    if (-not $found) {
        if (-not ($lines -match '^\s*\[paths\]')) { $lines += '[paths]' }
        $lines += "$key=$value"
    }
    Set-Content -Path $SettingsFile -Value $lines -Encoding ASCII
}

function Get-InstallDir {
    $d = Get-Setting 'corridorkey_dir'
    if ($d) { return $d }
    return (Join-Path $env:USERPROFILE 'CorridorKey2.0')
}

function Get-VenvPython([string]$dir) { Join-Path $dir 'venv\Scripts\python.exe' }

function Find-BasePython {
    # Prefer the py launcher with a version PyTorch 2.9 supports.
    if (Get-Command py -ErrorAction SilentlyContinue) {
        foreach ($v in '3.12', '3.13', '3.11', '3.10') {
            & py "-$v" -c "import sys" 2>$null
            if ($LASTEXITCODE -eq 0) { return @('py', "-$v") }
        }
    }
    if (Get-Command python -ErrorAction SilentlyContinue) {
        $ver = & python -c "import sys;print('%d.%d'%sys.version_info[:2])" 2>$null
        if ($ver -in '3.10', '3.11', '3.12', '3.13') { return @('python') }
    }
    return $null
}

function Pause-Menu { Write-Host ''; Read-Host 'Press Enter to return to the menu' | Out-Null }

function Write-Check([string]$label, [bool]$ok, [string]$detail) {
    $mark = if ($ok) { '[ OK ]' } else { '[MISS]' }
    $color = if ($ok) { 'Green' } else { 'Yellow' }
    Write-Host ("  {0} {1,-22} {2}" -f $mark, $label, $detail) -ForegroundColor $color
}

# ---------------------------------------------------------------- actions ---

function Show-Status {
    $dir = Get-InstallDir
    $vpy = Get-VenvPython $dir
    Write-Host ''
    Write-Host 'Prerequisites' -ForegroundColor Cyan
    $git = Get-Command git -ErrorAction SilentlyContinue
    Write-Check 'git' ([bool]$git) $(if ($git) { $git.Source } else { 'install Git for Windows' })
    $bp = Find-BasePython
    Write-Check 'Python 3.10-3.13' ([bool]$bp) $(if ($bp) { $bp -join ' ' } else { 'install Python 3.12 from python.org' })
    $ff = Get-Command ffmpeg -ErrorAction SilentlyContinue
    Write-Check 'ffmpeg on PATH' ([bool]$ff) $(if ($ff) { $ff.Source } else { 'winget install Gyan.FFmpeg' })
    $smi = Get-Command nvidia-smi -ErrorAction SilentlyContinue
    $gpu = ''
    if ($smi) { $gpu = (& nvidia-smi --query-gpu=name,memory.total --format=csv,noheader 2>$null | Select-Object -First 1) }
    Write-Check 'NVIDIA GPU' ([bool]$gpu) $gpu

    Write-Host ''
    Write-Host 'CorridorKey install' -ForegroundColor Cyan
    Write-Host "  Folder: $dir"
    Write-Check 'repo cloned' (Test-Path (Join-Path $dir 'infer.py')) ''
    Write-Check 'venv' (Test-Path $vpy) ''
    Write-Check 'model weights' (Test-Path (Join-Path $dir 'weights\corridorkey_v2.safetensors')) ''
    Write-Check 'foundation models' (Test-Path (Join-Path $dir 'vfms\moge\model.pt')) ''

    Write-Host ''
    Write-Host 'AE plug-in settings' -ForegroundColor Cyan
    Write-Host "  File:            $SettingsFile"
    $ckd = Get-Setting 'corridorkey_dir'; $cr = Get-Setting 'cache_root'
    Write-Host "  corridorkey_dir: $(if ($ckd) { $ckd } else { '(not set)' })"
    Write-Host "  cache_root:      $(if ($cr) { $cr } else { '(default)' })"
}

function Do-Clone {
    $default = Get-InstallDir
    $dir = Read-Host "Install folder [$default]"
    if (-not $dir) { $dir = $default }
    $dir = $dir.Trim().Trim('"').TrimEnd('\')

    if (Test-Path (Join-Path $dir '.git')) {
        Write-Host "Already cloned. Updating..." -ForegroundColor Cyan
        & git -C $dir pull
    } elseif ((Test-Path $dir) -and (Get-ChildItem $dir -Force | Select-Object -First 1)) {
        Write-Host "$dir exists and isn't empty. Pick another folder." -ForegroundColor Red
        return
    } else {
        & git clone $RepoUrl $dir
    }
    if ($LASTEXITCODE -eq 0) {
        Set-Setting 'corridorkey_dir' $dir
        Write-Host "Done. The AE plug-in now points at $dir" -ForegroundColor Green
    }
}

function Do-Venv {
    $dir = Get-InstallDir
    if (-not (Test-Path (Join-Path $dir 'infer.py'))) { Write-Host 'Clone the repo first (option 2).' -ForegroundColor Red; return }
    $bp = Find-BasePython
    if (-not $bp) { Write-Host 'No Python 3.10-3.13 found. Install Python 3.12 from python.org first.' -ForegroundColor Red; return }

    $vpy = Get-VenvPython $dir
    if (-not (Test-Path $vpy)) {
        Write-Host "Creating venv with $($bp -join ' ')..." -ForegroundColor Cyan
        $exe = $bp[0]; $args0 = @($bp | Select-Object -Skip 1)
        & $exe @args0 -m venv (Join-Path $dir 'venv')
        if ($LASTEXITCODE -ne 0) { Write-Host 'venv creation failed.' -ForegroundColor Red; return }
    }
    Write-Host 'Upgrading pip...' -ForegroundColor Cyan
    & $vpy -m pip install --upgrade pip
    Write-Host 'Installing PyTorch 2.9.1 (CUDA 12.8) - about 3 GB...' -ForegroundColor Cyan
    & $vpy -m pip install torch==2.9.1 torchvision==0.24.1 --index-url $TorchIndex
    if ($LASTEXITCODE -ne 0) { Write-Host 'PyTorch install failed.' -ForegroundColor Red; return }
    Write-Host 'Installing CorridorKey requirements...' -ForegroundColor Cyan
    & $vpy -m pip install -r (Join-Path $dir 'requirements.txt')
    if ($LASTEXITCODE -ne 0) { Write-Host 'Requirements install failed.' -ForegroundColor Red; return }
    & $vpy -c "import torch;print('CUDA available:', torch.cuda.is_available(), '|', torch.cuda.get_device_name(0) if torch.cuda.is_available() else '')"
}

function Do-Weights {
    $dir = Get-InstallDir
    $vpy = Get-VenvPython $dir
    if (-not (Test-Path $vpy)) { Write-Host 'Set up Python first (option 3).' -ForegroundColor Red; return }
    Write-Host 'Downloading weights (about 5 GB). Re-running skips files already present.' -ForegroundColor Cyan
    Push-Location $dir
    try { & $vpy scripts\download_weights.py } finally { Pop-Location }
}

function Do-Test {
    $dir = Get-InstallDir
    $vpy = Get-VenvPython $dir
    if (-not (Test-Path $vpy)) { Write-Host 'Set up Python first (option 3).' -ForegroundColor Red; return }
    $clip = Read-Host 'Drag a short green-screen clip into this window, then press Enter'
    $clip = $clip.Trim().Trim('"')
    if (-not (Test-Path $clip)) { Write-Host 'File not found.' -ForegroundColor Red; return }
    $out = Join-Path ([IO.Path]::GetDirectoryName($clip)) ('CK_test_' + [IO.Path]::GetFileNameWithoutExtension($clip))
    Write-Host "Keying the first 48 frames to $out ..." -ForegroundColor Cyan
    Push-Location $dir
    try { & $vpy infer.py --input $clip --output_dir $out --num_frames 48 --outputs alpha cutout } finally { Pop-Location }
    if ($LASTEXITCODE -eq 0) { Write-Host "Done. Check $out" -ForegroundColor Green; Start-Process explorer.exe $out }
}

function Do-Cache {
    $cur = Get-Setting 'cache_root'
    Write-Host "Current default cache folder: $(if ($cur) { $cur } else { '(built-in: %LOCALAPPDATA%\CorridorKeyAE\Cache)' })"
    $new = Read-Host 'New default cache folder (blank = built-in default)'
    $new = $new.Trim().Trim('"').TrimEnd('\')
    Set-Setting 'cache_root' $new
    Write-Host 'Saved. Effects with their own Cache Folder override keep using theirs.' -ForegroundColor Green
}

function Get-CacheRoot {
    $cr = Get-Setting 'cache_root'
    if ($cr) { return $cr }
    return (Join-Path $env:LOCALAPPDATA 'CorridorKeyAE\Cache')
}

function Get-KeyInfo([IO.DirectoryInfo]$d) {
    $srcFile = Join-Path $d.FullName 'source.txt'
    $source = ''
    if (Test-Path $srcFile) { $source = (Get-Content $srcFile -TotalCount 1).Trim() }
    $fgDir = Get-ChildItem $d.FullName -Directory -Filter '*_fg_exr' -ErrorAction SilentlyContinue | Select-Object -First 1
    $frames = 0
    if ($fgDir) { $frames = @(Get-ChildItem $fgDir.FullName -Filter '*.exr' -File -ErrorAction SilentlyContinue).Count }
    $bytes = (Get-ChildItem $d.FullName -Recurse -File -ErrorAction SilentlyContinue | Measure-Object Length -Sum).Sum
    if (-not $bytes) { $bytes = 0 }

    $state = 'Interrupted'
    if (Test-Path (Join-Path $d.FullName 'done.flag')) { $state = 'Done' }
    elseif (Test-Path (Join-Path $d.FullName 'failed.flag')) { $state = 'Failed' }
    else {
        $pidFile = Join-Path $d.FullName 'pid.txt'
        if (Test-Path $pidFile) {
            $procId = 0
            [int]::TryParse((Get-Content $pidFile -Raw).Trim(), [ref]$procId) | Out-Null
            if ($procId -and (Get-Process -Id $procId -ErrorAction SilentlyContinue)) { $state = 'Running' }
        }
    }

    [pscustomobject]@{
        Name      = $d.Name
        Path      = $d.FullName
        Source    = $source
        SourceOK  = [bool]($source -and (Test-Path $source))
        Frames    = $frames
        Bytes     = [double]$bytes
        Modified  = $d.LastWriteTime
        State     = $state
    }
}

function Parse-Selection([string]$text, [int]$max) {
    $picked = New-Object System.Collections.Generic.List[int]
    foreach ($part in ($text -split '[,\s]+' | Where-Object { $_ })) {
        if ($part -match '^(\d+)-(\d+)$') {
            for ($i = [int]$Matches[1]; $i -le [int]$Matches[2]; $i++) { if ($i -ge 1 -and $i -le $max) { $picked.Add($i) } }
        } elseif ($part -match '^\d+$') {
            $i = [int]$part; if ($i -ge 1 -and $i -le $max) { $picked.Add($i) }
        }
    }
    return $picked | Sort-Object -Unique
}

function Do-ManageCache {
    $root = Get-CacheRoot
    $other = '' + (Read-Host "Cache folder to manage [$root]")
    if ($other.Trim()) { $root = $other.Trim().Trim('"').TrimEnd('\') }
    if (-not (Test-Path $root)) { Write-Host "No cache folder at $root" -ForegroundColor Yellow; return }

    while ($true) {
        Write-Host ''
        Write-Host "Scanning $root ..." -ForegroundColor Cyan
        $keys = @(Get-ChildItem $root -Directory | ForEach-Object { Get-KeyInfo $_ } |
                  Where-Object { $_.Source -or $_.Frames -gt 0 } | Sort-Object Modified)
        if ($keys.Count -eq 0) { Write-Host '  No cached keys.'; return }

        $total = 0
        Write-Host ''
        Write-Host ('  {0,3}  {1,-34} {2,6} {3,9}  {4,-10} {5,-11} {6}' -f '#', 'Key', 'Frames', 'Size', 'Date', 'State', 'Source')
        for ($i = 0; $i -lt $keys.Count; $i++) {
            $k = $keys[$i]; $total += $k.Bytes
            $src = if (-not $k.Source) { '?' } elseif ($k.SourceOK) { Split-Path $k.Source -Leaf } else { 'MISSING: ' + (Split-Path $k.Source -Leaf) }
            $color = if ($k.State -eq 'Running') { 'Cyan' } elseif (-not $k.SourceOK) { 'Yellow' } else { 'Gray' }
            Write-Host ('  {0,3}  {1,-34} {2,6} {3,7:N2} GB  {4:yyyy-MM-dd} {5,-11} {6}' -f ($i + 1), $k.Name, $k.Frames, ($k.Bytes / 1GB), $k.Modified, $k.State, $src) -ForegroundColor $color
        }
        Write-Host ('  Total: {0:N2} GB in {1} keys' -f ($total / 1GB), $keys.Count)
        Write-Host ''
        Write-Host '  Numbers (e.g. 1,3-5) = delete those    M = delete keys whose source is missing'
        Write-Host '  O = delete keys older than N days      F = open folder    Enter = back'
        $c = ("" + (Read-Host 'Choose')).Trim().ToUpper()
        if (-not $c) { return }
        if ($c -eq 'F') { Start-Process explorer.exe $root; continue }

        $doomed = @()
        if ($c -eq 'M') { $doomed = @($keys | Where-Object { $_.Source -and -not $_.SourceOK }) }
        elseif ($c -eq 'O') {
            $days = 0
            if (-not [int]::TryParse(('' + (Read-Host 'Older than how many days')).Trim(), [ref]$days)) { continue }
            $cutoff = (Get-Date).AddDays(-$days)
            $doomed = @($keys | Where-Object { $_.Modified -lt $cutoff })
        }
        else { $doomed = @(Parse-Selection $c $keys.Count | ForEach-Object { $keys[$_ - 1] }) }

        $running = @($doomed | Where-Object { $_.State -eq 'Running' })
        if ($running.Count) { Write-Host "  Skipping $($running.Count) key(s) still running." -ForegroundColor Yellow }
        $doomed = @($doomed | Where-Object { $_.State -ne 'Running' })
        if ($doomed.Count -eq 0) { Write-Host '  Nothing to delete.'; continue }

        $gb = ($doomed | Measure-Object Bytes -Sum).Sum / 1GB
        $doomed | ForEach-Object { Write-Host "    $($_.Name)" }
        $ok = Read-Host ('Delete these {0} key(s), {1:N2} GB? Projects using them will need Key Clip again. (y/N)' -f $doomed.Count, $gb)
        if (('' + $ok).Trim().ToLower() -ne 'y') { continue }   # anything but an explicit y cancels
        foreach ($k in $doomed) {
            try { Remove-Item -LiteralPath $k.Path -Recurse -Force -ErrorAction Stop; Write-Host "  deleted $($k.Name)" -ForegroundColor Green }
            catch { Write-Host "  couldn't delete $($k.Name): $($_.Exception.Message)" -ForegroundColor Red }
        }
    }
}

# ------------------------------------------------------------------- menu ---

while ($true) {
    Clear-Host
    Write-Host '=== CorridorKey AE setup ===' -ForegroundColor Cyan
    Write-Host ''
    Write-Host '  1  Check status'
    Write-Host '  2  Clone / update CorridorKey 2.0'
    Write-Host '  3  Set up Python (venv + PyTorch + requirements)'
    Write-Host '  4  Download model weights (~5 GB)'
    Write-Host '  5  Test-key a clip outside AE'
    Write-Host '  6  Set default cache folder'
    Write-Host '  7  Manage cached keys'
    Write-Host '  Q  Quit'
    Write-Host ''
    $c = (Read-Host 'Choose').Trim().ToUpper()
    try {
        switch ($c) {
            '1' { Show-Status; Pause-Menu }
            '2' { Do-Clone;    Pause-Menu }
            '3' { Do-Venv;     Pause-Menu }
            '4' { Do-Weights;  Pause-Menu }
            '5' { Do-Test;     Pause-Menu }
            '6' { Do-Cache;    Pause-Menu }
            '7' { Do-ManageCache; Pause-Menu }
            'Q' { return }
        }
    } catch {
        Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
        Pause-Menu
    }
}
