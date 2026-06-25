$proj = $PSScriptRoot
$buildDir = Join-Path $proj "build"
$env:Path = "C:\Program Files\CMake\bin;C:\raylib\w64devkit\bin;C:\Windows\System32;C:\Windows"

if (-not (Test-Path "$buildDir\CMakeCache.txt")) {
    # Defender suele bloquear momentaneamente el .exe de prueba del compilador
    # en la primera corrida; reintentar resuelve el falso bloqueo.
    $configured = $false
    for ($i = 1; $i -le 3; $i++) {
        cmake -S $proj -B $buildDir -G "MinGW Makefiles" -DRAYLIB_DIR=C:/raylib/raylib
        if ($LASTEXITCODE -eq 0) { $configured = $true; break }
        Write-Host "Configure intento $i fallo (probablemente Defender), reintentando..."
    }
    if (-not $configured) {
        Write-Error "No se pudo configurar CMake tras 3 intentos."
        exit 1
    }
}

cmake --build $buildDir -- -j4
if ($LASTEXITCODE -ne 0) { exit 1 }

Write-Host "`nBuild OK -> $buildDir\gritador_universe.exe"
