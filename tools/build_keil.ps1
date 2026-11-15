[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$KeilRoot,

    [ValidateSet('all', 'board2-source', 'board2-sink', 'reference-source', 'reference-sink')]
    [string]$Target = 'all',

    [string]$Python = 'python'
)

$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$root = Split-Path $PSScriptRoot -Parent
$bin = Join-Path (Resolve-Path -LiteralPath $KeilRoot) 'C51/BIN'
foreach ($tool in 'C51', 'A51', 'BL51', 'OH51') {
    if (!(Test-Path -LiteralPath (Join-Path $bin "$tool.exe") -PathType Leaf)) {
        throw "Missing $tool.exe in $bin. Use a Keil installation with the C51 toolset."
    }
}

function Invoke-KeilTool {
    param([string]$Tool, [string[]]$Arguments, [string]$Product)

    Add-Content -Encoding UTF8 build.log "$Tool $($Arguments -join ' ')"
    $output = & (Join-Path $bin "$Tool.exe") @Arguments
    $status = $LASTEXITCODE
    $output | Add-Content -Encoding UTF8 build.log
    $output | ForEach-Object { Write-Host $_ }
    if ($output -match 'FATAL ERROR L250') {
        throw 'BL51 L250: the Keil evaluation linker cannot produce this firmware. A full C51 license is required.'
    }
    # BL51 legitimately warns L16 for the public API entry points unused by main.
    if ($status -gt 1 -or !(Test-Path -LiteralPath $Product -PathType Leaf) -or
        ($Tool -eq 'BL51' -and $output -match 'WARNING L(?!16:)')) {
        throw "$Tool failed to produce an accepted $Product (exit $status). See build.log."
    }
}

function Build-Firmware {
    param([string]$Name)

    $board2 = $Name.StartsWith('board2-')
    $role = if ($Name.EndsWith('-source')) { 1 } else { 2 }
    $definitions = "DEFAULT_DP_ROLE=$role"
    $sources = @('src/main', 'src/wire', 'src/pd', 'src/dp', 'hal/ch543_phy')
    if ($board2) {
        $definitions += ',BOARD2=1,BOARD2_VBUS_VERIFIED=0'
        $sources += @('hal/ch543_board2', 'hal/ch9445', 'hal/soft_i2c')
    } else {
        $sources += 'hal/ch543_board'
    }

    $directory = Join-Path $root "build/keil/$Name"
    New-Item -ItemType Directory -Force $directory | Out-Null
    $lock = [IO.File]::Open((Join-Path $directory '.lock'), 'OpenOrCreate', 'ReadWrite', 'None')
    Push-Location -LiteralPath $directory
    try {
        Write-Host "`n=== $Name ==="
        Set-Content -Encoding UTF8 build.log "Target: $Name ($definitions)"
        $objects = @('ch543_startup.obj') + @($sources | ForEach-Object { "$(Split-Path $_ -Leaf).obj" })
        foreach ($file in ($objects + @("$Name.omf", "$Name.m51", "$Name.hex", "$Name.pending.hex"))) {
            if (Test-Path -LiteralPath $file) {
                Remove-Item -LiteralPath $file
            }
        }

        foreach ($source in $sources) {
            $module = Split-Path $source -Leaf
            Invoke-KeilTool C51 @(
                "../../../$source.c", 'INCDIR(..\..\..\include)', "DEFINE($definitions)",
                'SMALL', 'OPTIMIZE(8,SIZE)', "OBJECT($module.obj)", "PRINT($module.lst)"
            ) "$module.obj"
        }
        Invoke-KeilTool A51 @(
            '../../../keil/ch543_startup.a51', 'OBJECT(ch543_startup.obj)', 'PRINT(ch543_startup.lst)'
        ) 'ch543_startup.obj'
        Invoke-KeilTool BL51 @(
            ($objects -join ','), 'TO', "$Name.omf", 'RAMSIZE(256)', 'CODE(0x0000-0x37FF)',
            'XDATA(0x0000-0x00FF,?XD?CH543_PHY(0x0000))', "PRINT($Name.m51)"
        ) "$Name.omf"
        Invoke-KeilTool OH51 @("$Name.omf", "HEXFILE($Name.pending.hex)") "$Name.pending.hex"

        $verifyArgs = @("$root/tools/check_keil_image.py", "$Name.pending.hex", "$Name.m51")
        if ($board2) { $verifyArgs += '--board2' }
        $verification = & $Python @verifyArgs
        $status = $LASTEXITCODE
        $verification | Add-Content -Encoding UTF8 build.log
        $verification | ForEach-Object { Write-Host $_ }
        if ($status -ne 0) { throw 'Firmware image validation failed.' }
        Move-Item -LiteralPath "$Name.pending.hex" -Destination "$Name.hex"
        Write-Host "Verified image: $directory\$Name.hex"
    } finally {
        if (Test-Path -LiteralPath "$Name.pending.hex") {
            Remove-Item -LiteralPath "$Name.pending.hex" -ErrorAction SilentlyContinue
        }
        Pop-Location
        $lock.Dispose()
    }
}

$targets = if ($Target -eq 'all') {
    'board2-source', 'board2-sink', 'reference-source', 'reference-sink'
} else {
    $Target
}
$failed = @()
foreach ($name in $targets) {
    try {
        Build-Firmware $name
    } catch {
        $failed += $name
        Write-Warning "${name}: $($_.Exception.Message)"
    }
}
if ($failed.Count) {
    Write-Warning "Keil build failed for $($failed -join ', '). See build/keil/<target>/build.log."
    exit 1
}
