<#
.SYNOPSIS
  Builds PrintPulse on Windows using the Pebble SDK in a container.

.EXAMPLE
  ./build.ps1                       # build build/PrintPulse.pbw
  ./build.ps1 -Phone 192.168.1.23   # build, then install via the Pebble app's developer connection
  ./build.ps1 -Zip                  # build, then pack dist/PrintPulse-cloudpebble.zip
  ./build.ps1 -Login                # sign in to the Pebble account (once; kept in the SDK volume)
  ./build.ps1 -Publish -ReleaseNotes "Buzz when a print pauses"
  ./build.ps1 -Shell                # open a shell with the `pebble` tool
#>
param(
    [string]$Phone,
    [string]$ReleaseNotes = '',
    [switch]$Zip,
    [switch]$Publish,
    [switch]$Login,
    [switch]$Shell,
    [switch]$InteractivePublish
)
# Exit codes are checked explicitly: the SDK prints harmless warnings on stderr,
# which Windows PowerShell would otherwise turn into errors.
$engine = if (Get-Command podman -ErrorAction SilentlyContinue) { 'podman' }
          elseif (Get-Command docker -ErrorAction SilentlyContinue) { 'docker' }
          else { throw 'Install Podman Desktop or Docker Desktop first.' }

$root = $PSScriptRoot
$image = 'pebble-sdk'
$volume = 'pebble-sdk'   # keeps the downloaded SDK (and pebble login) between runs

& $engine image inspect $image *> $null
if ($LASTEXITCODE -ne 0) {
    Write-Host "Building the $image image (first run only)..."
    & $engine build -t $image (Join-Path $root 'build-env')
    if ($LASTEXITCODE -ne 0) { throw 'Image build failed.' }
}

$run = @('run', '--rm', '-v', "${root}:/PrintPulse", '-w', '/PrintPulse', '-v', "${volume}:/root/.pebble-sdk")
if ($env:PEBBLE_FIREBASE_ID_TOKEN) {
    $run += @('-e', 'PEBBLE_FIREBASE_ID_TOKEN')
}
$setup = 'pebble sdk list 2>/dev/null | grep -q active || pebble sdk install latest'

if ($Shell) {
    & $engine @run -it $image bash -c "$setup; exec bash"
    exit $LASTEXITCODE
}

if ($Login) {
    # Browser OAuth; we poll the broker for the token (no localhost callback port).
    Write-Host @"
Pebble login (session is kept in the '$volume' volume)

1. Open the URL that appears in your Windows browser and sign in.
2. The browser may then show a connection error on localhost — that is OK.
3. Leave this terminal running until it prints Login OK (do not Ctrl+C).

"@
    & $engine @run -it $image bash -c "$setup; python3 build-env/login.py"
    exit $LASTEXITCODE
}

$cmd = "$setup && pebble build"
if ($Zip) { $cmd += " && python3 build-env/make_zip.py" }
if ($Phone) { $cmd += " && pebble install --phone $Phone" }
if ($Publish) {
    $pub = 'python3 build-env/publish.py'
    if ($ReleaseNotes) {
        $escaped = $ReleaseNotes.Replace("'", "'\''")
        $pub += " --release-notes '$escaped'"
    }
    if ($InteractivePublish) { $pub += ' --interactive' }
    $cmd += " && $pub"
}

# Publish / phone install may need a TTY for auth prompts on some pebble-tool builds.
$runArgs = $run
if ($Publish -and $InteractivePublish) { $runArgs = $run + @('-it') }

& $engine @runArgs $image bash -c $cmd
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
Write-Host "Built $(Join-Path $root 'build\PrintPulse.pbw')"
if ($Zip) { Write-Host "Zip $(Join-Path $root 'dist\PrintPulse-cloudpebble.zip')" }
if ($Publish) { Write-Host 'Published to the Pebble appstore (see pebble publish output above).' }
