@echo off
setlocal enableextensions enabledelayedexpansion

set "SRC_DIR=%~dp0"
if "%SRC_DIR:~-1%"=="\" set "SRC_DIR=%SRC_DIR:~0,-1%"

set "CFG=%~1"
if "%CFG%"=="" set "CFG=Release"

set "OUT_DIR=%SRC_DIR%\x64\%CFG%"
if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"

:: 1. Definiujemy wsp?lne flagi w jednej zmiennej
set "CFLAGS=-target x86_64-unknown-elf -ffreestanding -fno-stack-protector -fno-exceptions -fno-rtti -fno-pic -fno-pie -mno-red-zone -ffunction-sections -fdata-sections -nostdlib -I"%SRC_DIR%""

echo [0/4] Deleting old ".o" files...
del /q "%OUT_DIR%\*.o" 2>nul

echo [1/4] Compiling C++ files...
:: 2. P?tla przechodz?ca po li?cie plik?w
:: P?tla przechodzi przez ka?dy plik .cpp w SRC_DIR
for %%F in ("%SRC_DIR%\*.cpp") do (
    echo   Compiling %%~nxF...
    clang %CFLAGS% -c "%%F" -o "%OUT_DIR%\%%~nF.o" || exit /b 1
)

echo [2/4] Assembling NASM...
nasm -f elf64 "%SRC_DIR%\io.asm" -o "%OUT_DIR%\io.o" || exit /b 1

echo [3/4] Linking image...
:: Tutaj u?ywamy maski *.o, aby automatycznie zlinkowa? wszystko, co si? skompilowa?o
ld.lld -m elf_x86_64 ^
       -T "%SRC_DIR%\linker.ld" ^
       -Map "%OUT_DIR%\kernel.map" ^
       -o "%OUT_DIR%\kernel.elf" ^
       "%OUT_DIR%\*.o" || exit /b 1

echo [4/4] Converting ELF to flat binary...
llvm-objcopy -O binary "%OUT_DIR%\kernel.elf" "%OUT_DIR%\kernel.bin" || exit /b 1

echo.
echo Build OK: "%OUT_DIR%\kernel.bin"
exit /b 0