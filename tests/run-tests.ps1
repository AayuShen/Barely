# Barely - created by AayuShen. Licensed under the MIT License.
#
# Automated tests for the built binaries. Safe to run anywhere: nothing here loads Barely
# into Explorer or changes the taskbar. Run after build.cmd:
#   powershell -ExecutionPolicy Bypass -File tests\run-tests.ps1
param([string]$BuildDir = (Join-Path $PSScriptRoot "..\build"))

$ErrorActionPreference = "Stop"
$BuildDir = (Resolve-Path $BuildDir).Path
$exe = Join-Path $BuildDir "barely.exe"
$dll = Join-Path $BuildDir "barely_tap.dll"
$version = ([regex]::Match((Get-Content (Join-Path $PSScriptRoot "..\src\version.h") -Raw),
    'BARELY_VERSION_STR "([^"]+)"')).Groups[1].Value
$elevated = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)

$script:failed = 0
$script:passed = 0
function Check([string]$name, [bool]$ok, [string]$detail = "") {
    if ($ok) { $script:passed++; Write-Host "  PASS  $name" -ForegroundColor Green }
    else { $script:failed++; Write-Host "  FAIL  $name $detail" -ForegroundColor Red }
}

function Run([string]$path, [string[]]$argv) {
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $path
    $psi.Arguments = ($argv | ForEach-Object { if ($_ -match '\s') { "`"$_`"" } else { $_ } }) -join " "
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $p = [Diagnostics.Process]::Start($psi)
    $out = $p.StandardOutput.ReadToEnd() + $p.StandardError.ReadToEnd()
    if (-not $p.WaitForExit(30000)) { $p.Kill(); return @{ Code = -1; Out = "timeout" } }
    @{ Code = $p.ExitCode; Out = $out }
}

function DllCharacteristics([string]$path) {
    $b = [IO.File]::ReadAllBytes($path)
    $pe = [BitConverter]::ToInt32($b, 0x3C)
    [BitConverter]::ToUInt16($b, $pe + 24 + 70)  # optional header + 70
}

Write-Host "Barely $version tests ($BuildDir)$(if ($elevated) { ' [elevated]' })"

Write-Host "`nCommand line"
$r = Run $exe @("--version")
Check "--version prints version and author" ($r.Code -eq 0 -and $r.Out -match [regex]::Escape($version) -and $r.Out -match "AayuShen") $r.Out
$r = Run $exe @("--help")
Check "--help exits 0 and lists options" ($r.Code -eq 0 -and $r.Out -match "--maximized") $r.Out
foreach ($bad in @(@("101"), @("-5"), @("abc"), @("1000"), @("--opacity"), @("--opacity", "50%"),
                   @("--tint", "#12345G"), @("--tint", "#1234"), @("--border", "maybe"),
                   @("--maximized", "on"), @("--autostart", "yes"), @("--status", "--restore"),
                   @("--status", "50"), @("--bogus"), @("50", "60"))) {
    $r = Run $exe $bad
    Check "rejects: $($bad -join ' ')" ($r.Code -eq 2) "exit $($r.Code)"
}

Write-Host "`nSecurity"
$tmp = Join-Path ([IO.Path]::GetTempPath()) ("barely-test-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory $tmp | Out-Null
try {
    Copy-Item $exe $tmp
    $before = (Get-ItemProperty HKCU:\Software\Barely -ErrorAction SilentlyContinue | Out-String)

    # Tampered DLL: one byte flipped.
    $bytes = [IO.File]::ReadAllBytes($dll)
    $bytes[$bytes.Length - 1] = $bytes[$bytes.Length - 1] -bxor 1
    [IO.File]::WriteAllBytes((Join-Path $tmp "barely_tap.dll"), $bytes)
    $r = Run (Join-Path $tmp "barely.exe") @("0")
    if ($elevated) {
        Check "refuses to run elevated" ($r.Code -eq 5) "exit $($r.Code)"
    } else {
        Check "refuses a tampered barely_tap.dll" ($r.Code -eq 4 -and $r.Out -match "modified") "exit $($r.Code): $($r.Out)"
        Remove-Item (Join-Path $tmp "barely_tap.dll")
        $r = Run (Join-Path $tmp "barely.exe") @("0")
        Check "refuses a missing barely_tap.dll" ($r.Code -eq 4) "exit $($r.Code)"
        $after = (Get-ItemProperty HKCU:\Software\Barely -ErrorAction SilentlyContinue | Out-String)
        Check "integrity failure changes no settings" ($before -eq $after)
    }
} finally {
    Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue
}

foreach ($bin in @("barely.exe", "barelyw.exe", "barely_tap.dll")) {
    $c = DllCharacteristics (Join-Path $BuildDir $bin)
    Check "$bin has ASLR + high-entropy ASLR" (($c -band 0x60) -eq 0x60)
    Check "$bin has DEP (NX)" (($c -band 0x100) -ne 0)
    Check "$bin has Control Flow Guard" (($c -band 0x4000) -ne 0)
}

Write-Host "`nMetadata"
foreach ($bin in @("barely.exe", "barelyw.exe", "barely_tap.dll")) {
    $vi = (Get-Item (Join-Path $BuildDir $bin)).VersionInfo
    Check "$bin version $version" ($vi.ProductVersion -eq $version) $vi.ProductVersion
    Check "$bin credits AayuShen (MIT)" ($vi.CompanyName -eq "AayuShen" -and $vi.LegalCopyright -match "AayuShen.*MIT") "$($vi.CompanyName) / $($vi.LegalCopyright)"
}

if (-not $elevated) {
    Write-Host "`nRead-only commands"
    $r = Run $exe @("--status")
    Check "--status reports build and settings" ($r.Code -eq 0 -and $r.Out -match "Windows build" -and $r.Out -match "Settings") $r.Out
}

Write-Host "`n$script:passed passed, $script:failed failed"
exit $(if ($script:failed) { 1 } else { 0 })
