param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('x86', 'x64', 'arm64')]
    [string]$Architecture
)

$ErrorActionPreference = 'Stop'

function Invoke-Checked {
    param([string]$Program, [string[]]$Arguments)
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Program failed with exit code $LASTEXITCODE"
    }
}

$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Set-Location $root
$conanArch = @{ x86 = 'x86'; x64 = 'x86_64'; arm64 = 'armv8' }[$Architecture]
$cmakeArch = @{ x86 = 'Win32'; x64 = 'x64'; arm64 = 'ARM64' }[$Architecture]
$build = Join-Path $root "build/windows/$Architecture"
$conanDir = Join-Path $build 'conan'
$nativeDir = Join-Path $build 'native'
$runtimeDir = Join-Path $build 'runtime'
$packageDir = Join-Path $root "out/OpenLF2-Windows-$Architecture"
New-Item -ItemType Directory -Force -Path $build, $packageDir | Out-Null

Invoke-Checked 'conan' @('profile', 'detect', '--force')
$cache = Join-Path $build 'conan-cache.tgz'
if (Test-Path $cache) {
    Invoke-Checked 'conan' @('cache', 'restore', $cache)
}
Invoke-Checked 'conan' @('export', 'conan/luajit')
Invoke-Checked 'conan' @(
    'install', '.', '--profile:host', 'default', '--profile:build', 'default',
    '--output-folder', $conanDir, '--build=missing',
    '--deployer=runtime_deploy', '--deployer-folder', $runtimeDir,
    '-s:h', "arch=$conanArch", '-s:h', 'build_type=Release',
    '-s:h', 'compiler.cppstd=23', '-s:h', 'compiler.runtime=dynamic',
    '-s:h', 'compiler.runtime_type=Release'
)
$nextCache = Join-Path $build 'conan-cache-next.tgz'
Invoke-Checked 'conan' @('cache', 'save', '*:*', '--no-source', "--file=$nextCache")
Move-Item -Force $nextCache $cache

$toolchain = Join-Path $conanDir 'conan_toolchain.cmake'
Invoke-Checked 'cmake' @(
    '-S', $root, '-B', $nativeDir, '-G', 'Visual Studio 17 2022', '-A', $cmakeArch,
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain", '-DOPENLF2_DEPENDENCY_PROVIDER=conan'
)
Invoke-Checked 'cmake' @('--build', $nativeDir, '--config', 'Release', '--parallel', '4')

$binaryDir = Join-Path $nativeDir 'Release'
$dlls = @(Get-ChildItem -Path $runtimeDir -Filter '*.dll' -File -Recurse)
if ($dlls.Count -eq 0) { throw 'Conan did not deploy any runtime DLLs.' }
foreach ($dll in $dlls) {
    Copy-Item -Force $dll.FullName $binaryDir
}

Copy-Item -Force (Join-Path $binaryDir 'openlf2.exe') $packageDir
foreach ($dll in $dlls) { Copy-Item -Force $dll.FullName $packageDir }
Copy-Item -Recurse -Force (Join-Path $root 'scripts') $packageDir
Copy-Item -Force (Join-Path $root 'README.md') $packageDir
Copy-Item -Force (Join-Path $root 'LICENSE') $packageDir
Invoke-Checked 'python' @((Join-Path $root 'dist/collect-licenses.py'),
    (Join-Path $packageDir 'licenses'), '--key', 'ffmpeg', '--key', 'ffmpeg-notes', '--key', 'sdl3',
    '--key', 'luajit', '--key', 'zlib', '--key', 'bzip2', '--key', 'openssl',
    '--key', 'libcurl')
$archive = Join-Path $root "out/OpenLF2-Windows-$Architecture.zip"
Compress-Archive -Force -Path (Join-Path $packageDir '*') -DestinationPath $archive
$hash = (Get-FileHash -Algorithm SHA256 $archive).Hash.ToLowerInvariant()
Set-Content -Path "$archive.sha256" -Value "$hash  $(Split-Path -Leaf $archive)"
Write-Host "Created $archive"
