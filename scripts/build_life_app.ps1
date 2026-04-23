# =============================================================
# build_life_app.ps1 — build the production LifeGL viewer.
# Phase B6.4: out-of-tree build into ./build, clean root.
# =============================================================
$ErrorActionPreference = "Stop"
Set-Location (Split-Path $PSScriptRoot -Parent)

$ROOT  = (Get-Location).Path
$BIN   = Join-Path $ROOT "bin"
$BUILD = Join-Path $ROOT "build"
$OBJ   = Join-Path $BUILD "obj\life_app"

$INC  = @("-I$ROOT\src", "-I$ROOT\third_party\glad\include")
$SRC  = @(
    "$ROOT\src\main.cpp",
    "$ROOT\src\life_app.cpp",
    "$ROOT\src\camera.cpp",
    "$ROOT\src\gl_context.cpp",
    "$ROOT\src\gl_window.cpp",
    "$ROOT\src\gl_compute_engine.cpp",
    "$ROOT\src\grid_renderer.cpp",
    "$ROOT\third_party\glad\src\gl.c"
)
$OUT  = Join-Path $BIN   "life_app.exe"
$PDB  = Join-Path $BUILD "life_app.pdb"
$LIBS = @("opengl32.lib","gdi32.lib","user32.lib")

# --- Prepare output dirs -------------------------------------
foreach ($d in @($BIN, $BUILD, $OBJ)) {
    if (-not (Test-Path $d)) { New-Item -ItemType Directory -Force $d | Out-Null }
}

# --- Compiler flags ------------------------------------------
# Note: no /D WIN32_LEAN_AND_MEAN or /D NOMINMAX — each .cpp defines
#       them internally, passing on cmdline triggers C4005.
# /Fo<dir>\  : place .obj files into <dir>\ (trailing backslash is mandatory)
# /Fd<file>  : place compiler .pdb into <file>
$FLAGS = @(
    "/std:c++20","/EHsc","/W4","/permissive-","/O2","/DNDEBUG","/nologo",
    "/MP",                          # parallel compilation across TUs
    "/Fo$OBJ\",                     # .obj -> build/obj/life_app/
    "/Fd$BUILD\life_app_cl.pdb"     # compiler PDB -> build/
)

$LINK_FLAGS = @(
    "/PDB:$PDB",                    # linker PDB -> build/life_app.pdb
    "/INCREMENTAL:NO"
)

# --- Build ---------------------------------------------------
Write-Host "==> Building life_app.exe (release, out-of-tree)" -ForegroundColor Cyan
Write-Host "    OBJ  -> $OBJ"   -ForegroundColor DarkGray
Write-Host "    EXE  -> $OUT"   -ForegroundColor DarkGray

& cl.exe $FLAGS $INC $SRC /Fe:$OUT /link $LINK_FLAGS $LIBS
if ($LASTEXITCODE -ne 0) { throw "Build failed: $LASTEXITCODE" }

Write-Host "==> Build OK: $OUT" -ForegroundColor Green