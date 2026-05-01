$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ttfFiles = Get-ChildItem -Path $scriptDir -Filter *.ttf | Sort-Object Name
$asmContent = "section .rodata`n`n"
$hContent = "#pragma once`n#include `"types.h`"`n`nstruct EmbeddedFont {`n    const char* name;`n    unsigned char* data;`n    unsigned char* end;`n};`n`nstatic inline uint64_t embedded_font_size(const EmbeddedFont* font) {`n    return (uint64_t)(font->end - font->data);`n}`n`n#ifdef __cplusplus`nextern `"C`" {`n#endif`n`n"
$cppContent = "#include `"fonts.h`"`n`n#ifdef __cplusplus`nextern `"C`" {`n#endif`n`n"

foreach ($file in $ttfFiles) {
    $baseName = ($file.Name -replace '[^a-zA-Z0-9]', '_')
    
    $asmContent += "global _binary_$($baseName)_start`n"
    $asmContent += "global _binary_$($baseName)_end`n"
    $asmContent += "_binary_$($baseName)_start:`n"
    $asmContent += "    incbin `"$($file.Name)`"`n"
    $asmContent += "_binary_$($baseName)_end:`n`n"
    
    $hContent += "extern unsigned char _binary_$($baseName)_start[];`n"
    $hContent += "extern unsigned char _binary_$($baseName)_end[];`n"
}

$hContent += "`nextern const EmbeddedFont G_EMBEDDED_FONTS[];`n"
$hContent += "extern const int G_EMBEDDED_FONTS_COUNT;`n"
$hContent += "`n#ifdef __cplusplus`n}`n#endif`n"

$cppContent += "const EmbeddedFont G_EMBEDDED_FONTS[] = {`n"
foreach ($file in $ttfFiles) {
    $baseName = ($file.Name -replace '[^a-zA-Z0-9]', '_')
    $cppContent += "    { `"$($file.BaseName)`", _binary_$($baseName)_start, _binary_$($baseName)_end },`n"
}
$cppContent += "};`n`nconst int G_EMBEDDED_FONTS_COUNT = $($ttfFiles.Count);`n`n#ifdef __cplusplus`n}`n#endif`n"

$asmContent | Out-File -FilePath (Join-Path $scriptDir "fonts_data.asm") -Encoding ascii -NoNewline
$hContent   | Out-File -FilePath (Join-Path $scriptDir "fonts.h")        -Encoding ascii -NoNewline
$cppContent | Out-File -FilePath (Join-Path $scriptDir "fonts.cpp")       -Encoding ascii -NoNewline
