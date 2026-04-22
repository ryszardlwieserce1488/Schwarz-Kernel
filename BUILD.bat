@echo off
setlocal enableextensions enabledelayedexpansion
set "SRC_DIR=%~dp0"
if "%SRC_DIR:~-1%"=="\" set "SRC_DIR=%SRC_DIR:~0,-1%"
set "CFG=%~1"
if "%CFG%"=="" set "CFG=Release"
set "OUT_DIR=%SRC_DIR%\x64\%CFG%"
if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"
set "CFLAGS=-target x86_64-unknown-elf -ffreestanding -fno-stack-protector -fno-exceptions -fno-rtti -fno-pic -fno-pie -mno-red-zone -ffunction-sections -fdata-sections -nostdlib -I"%SRC_DIR%""

echo [0/5] Generuję pliki czcionek...
powershell -ExecutionPolicy Bypass -File "%SRC_DIR%\generate_fonts.ps1" || exit /b 1

echo [1/5] Usuwams stare pliki ".o"...
del /q "%OUT_DIR%\*.o" 2>nul

echo [2/5] Kompiluję pliki źródłowe C/C++...
for %%F in ("%SRC_DIR%\*.cpp") do (
    echo   Kompiluję %%~nxF...
    clang %CFLAGS% -c "%%F" -o "%OUT_DIR%\%%~nF.o" || exit /b 1
)

echo [3/5] Kompiluję pliki źródłowe ASM...
for %%F in ("%SRC_DIR%\*.asm") do (
    echo    Kompiluję %%~nxF...
    nasm -f elf64 "%%F" -o "%OUT_DIR%\%%~nF.o" || exit /b 1
)

echo [4/5] Konsoliduję projekt...
ld.lld -m elf_x86_64 ^
       -T "%SRC_DIR%\linker.ld" ^
       -Map "%OUT_DIR%\kernel.map" ^
       -o "%OUT_DIR%\kernel.elf" ^
       "%OUT_DIR%\*.o" || exit /b 1

echo [5/5] Porządkuję plik binarny...
llvm-objcopy -O binary "%OUT_DIR%\kernel.elf" "%OUT_DIR%\kernel.bin" || exit /b 1

echo.
echo Kompilacja pomyślna: "%OUT_DIR%\kernel.bin"
exit /b 0