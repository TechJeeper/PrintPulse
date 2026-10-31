<#
.SYNOPSIS
  Builds PrintPulse on Windows using the Pebble SDK in a container.

.EXAMPLE
  ./build.ps1                       # build build/PrintPulse.pbw
  ./build.ps1 -Phone 192.168.1.23   # build, then install via the Pebble app's developer connection
  ./build.ps1 -Zip                  # build, then pack dist/PrintPulse-cloudpebble.zip for CloudPebble
  ./build.ps1 -Shell                # open a shell with the `pebble` tool
#>
param(
    [string]$Phone,
    [switch]$Zip,
    [switch]$Shell
)
# Exit codes are checked explicitly: the SDK prints harmless warnings on stderr,
# which Windows PowerShell would otherwise turn into errors.
$engine = if (Get-Command podman -ErrorAction SilentlyContinue) { 'podman' }
          elseif (Get-Command docker -ErrorAction SilentlyContinue) { 'docker' }
          else { throw 'Install Podman Desktop or Docker Desktop first.' }

$root = $PSScriptRoot
$image = 'pebble-sdk'
$volume = 'pebble-sdk'   # keeps the downloaded SDK between runs

& $engine image inspect $image *> $null
if ($LASTEXITCODE -ne 0) {
    Write-Host "Building the $image image (first run only)..."
    & $engine build -t $image (Join-Path $root 'build-env')
    if ($LASTEXITCODE -ne 0) { throw 'Image build failed.' }
}

$run = @('run', '--rm', '-v', "${root}:/PrintPulse", '-w', '/PrintPulse', '-v', "${volume}:/root/.pebble-sdk")
$setup = 'pebble sdk list 2>/dev/null | grep -q active || pebble sdk install latest'

if ($Shell) {
    & $engine @run -it $image bash -c "$setup; exec bash"
    exit $LASTEXITCODE
}

$cmd = "$setup && pebble build"
if ($Zip) { $cmd += " && python3 build-env/make_zip.py" }
if ($Phone) { $cmd += " && pebble install --phone $Phone" }
& $engine @run $image bash -c $cmd
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
Write-Host "Built $(Join-Path $root 'build\PrintPulse.pbw')"
