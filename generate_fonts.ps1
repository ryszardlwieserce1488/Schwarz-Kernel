$ttfFiles = Get-ChildItem -Path . -Filter *.ttf | Sort-Object Name
$asmContent = "section .rodata`n`n"
$hContent = "#pragma once`n#include `"types.h`"`n`nstruct EmbeddedFont {`n    const char* name;`n    unsigned char* data;`n    uint64_t size;`n};`n`n#ifdef __cplusplus`nextern `"C`" {`n#endif`n`n"
$cppContent = "#include `"fonts.h`"`n`n#ifdef __cplusplus`nextern `"C`" {`n#endif`n`n"

foreach ($file in $ttfFiles) {
    $baseName = ($file.Name -replace '[^a-zA-Z0-9]', '_')
    
    $asmContent += "global _binary_$($baseName)_start`n"
    $asmContent += "global _binary_$($baseName)_size`n"
    $asmContent += "_binary_$($baseName)_start:`n"
    $asmContent += "    incbin `"$($file.Name)`"`n"
    $asmContent += "_binary_$($baseName)_end:`n"
    $asmContent += "_binary_$($baseName)_size:`n"
    $asmContent += "    dq _binary_$($baseName)_end - _binary_$($baseName)_start`n`n"
    
    $hContent += "extern unsigned char _binary_$($baseName)_start[];`n"
    $hContent += "extern uint64_t _binary_$($baseName)_size;`n"
}

$hContent += "`nextern const EmbeddedFont G_EMBEDDED_FONTS[];`n"
$hContent += "extern const int G_EMBEDDED_FONTS_COUNT;`n"
$hContent += "`n#ifdef __cplusplus`n}`n#endif`n"

$cppContent += "const EmbeddedFont G_EMBEDDED_FONTS[] = {`n"
foreach ($file in $ttfFiles) {
    $baseName = ($file.Name -replace '[^a-zA-Z0-9]', '_')
    $cppContent += "    { `"$($file.BaseName)`", _binary_$($baseName)_start, _binary_$($baseName)_size },`n"
}
$cppContent += "};`n`nconst int G_EMBEDDED_FONTS_COUNT = $($ttfFiles.Count);`n`n#ifdef __cplusplus`n}`n#endif`n"

$asmContent | Out-File -FilePath "fonts_data.asm" -Encoding ascii -NoNewline
$hContent   | Out-File -FilePath "fonts.h"        -Encoding ascii -NoNewline
$cppContent | Out-File -FilePath "fonts.cpp"       -Encoding ascii -NoNewline