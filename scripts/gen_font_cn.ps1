# Generate src/font_cn.c - 16x16 bitmap font for Chinese UI text
# Renders each unique CJK char from Lang/CHN.LNG using SimSun 16px (GDI embedded bitmap)
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms

$lngPath = 'd:\wLaunchELF_R3Z-master\Lang\CHN.LNG'
$outPath = 'd:\wLaunchELF_R3Z-master\src\font_cn.c'

# 1. Extract unique CJK chars
$lng = Get-Content $lngPath -Raw -Encoding UTF8
$set = [System.Collections.Generic.HashSet[char]]::new()
foreach ($ch in $lng.ToCharArray()) { if ([int]$ch -ge 0x2E80) { $set.Add($ch) | Out-Null } }
$chars = @($set | Sort-Object)
Write-Host "Glyphs to render: $($chars.Count)"

$font = [System.Drawing.Font]::new('SimSun', 16, [System.Drawing.FontStyle]::Regular, [System.Drawing.GraphicsUnit]::Pixel)
$flags = [System.Windows.Forms.TextFormatFlags]::HorizontalCenter -bor [System.Windows.Forms.TextFormatFlags]::VerticalCenter -bor [System.Windows.Forms.TextFormatFlags]::NoPrefix

$bmp = [System.Drawing.Bitmap]::new(32, 32)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::SingleBitPerPixelGridFit

$entries = New-Object System.Collections.Generic.List[string]
$preview = New-Object System.Collections.Generic.List[string]
$emptyCount = 0

foreach ($ch in $chars) {
    $g.Clear([System.Drawing.Color]::White)
    $rect = [System.Drawing.Rectangle]::new(0, 0, 32, 32)
    [System.Windows.Forms.TextRenderer]::DrawText($g, [string]$ch, $font, $rect, [System.Drawing.Color]::Black, $flags)

    # find ink bbox
    $minx = 32; $miny = 32; $maxx = -1; $maxy = -1
    for ($y = 0; $y -lt 32; $y++) {
        for ($x = 0; $x -lt 32; $x++) {
            if ($bmp.GetPixel($x, $y).R -lt 128) {
                if ($x -lt $minx) { $minx = $x }; if ($x -gt $maxx) { $maxx = $x }
                if ($y -lt $miny) { $miny = $y }; if ($y -gt $maxy) { $maxy = $y }
            }
        }
    }

    # build 16x16 grid, ink centered
    $grid = New-Object 'bool[,]' 16,16
    if ($maxx -ge 0) {
        $w = $maxx - $minx + 1; $h = $maxy - $miny + 1
        $dx = [math]::Floor((16 - $w) / 2.0)
        $dy = [math]::Floor((16 - $h) / 2.0)
        for ($ty = 0; $ty -lt 16; $ty++) {
            for ($tx = 0; $tx -lt 16; $tx++) {
                $srcx = $tx - $dx + $minx
                $srcy = $ty - $dy + $miny
                if ($srcx -ge 0 -and $srcx -lt 32 -and $srcy -ge 0 -and $srcy -lt 32) {
                    if ($bmp.GetPixel($srcx, $srcy).R -lt 128) { $grid[$ty, $tx] = $true }
                }
            }
        }
    } else { $emptyCount++ }

    # bytes: 16 rows x 2 bytes, MSB first
    $bytes = New-Object System.Text.StringBuilder
    for ($y = 0; $y -lt 16; $y++) {
        $b0 = 0; $b1 = 0
        for ($x = 0; $x -lt 8; $x++) { if ($grid[$y, $x]) { $b0 = $b0 -bor (0x80 -shr $x) } }
        for ($x = 8; $x -lt 16; $x++) { if ($grid[$y, $x]) { $b1 = $b1 -bor (0x80 -shr ($x - 8)) } }
        [void]$bytes.Append(('0x{0:X2},0x{1:X2},' -f $b0, $b1))
    }
    $code = [int]$ch
    $entries.Add(('    {{0x{0:X4}, {{{1}}}}},' -f $code, $bytes.ToString().TrimEnd(',')))

    # ascii preview for first few chars
    if ($preview.Count -lt 4) {
        $lines = @()
        for ($y = 0; $y -lt 16; $y++) {
            $line = ''
            for ($x = 0; $x -lt 16; $x++) { $line += $(if ($grid[$y, $x]) { '##' } else { '..' }) }
            $lines += $line
        }
        $preview.Add("U+{0:X4} {1}:" -f $code, $ch)
        $preview.AddRange($lines)
    }
}

$g.Dispose(); $bmp.Dispose(); $font.Dispose()

$header = @"
//--------------------------------------------------------------
//File name:   font_cn.c
//Description: Built-in 16x16 bitmap glyphs for Chinese (UTF-8) UI
//             strings. Generated from Lang/CHN.LNG (SimSun 16px).
//             Sorted by unicode; lookup via binary search.
//--------------------------------------------------------------
#include "launchelf.h"

typedef struct FontCnGlyph
{
    u16 code;
    u8 bitmap[32];
} FontCnGlyph;

const FontCnGlyph font_cn[] = {
"@
$footer = @"

const int font_cn_count = sizeof(font_cn) / sizeof(font_cn[0]);

const u8 *font_cn_lookup(unsigned int code)
{
    int lo = 0, hi = font_cn_count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        if (font_cn[mid].code == (u16)code)
            return font_cn[mid].bitmap;
        if (font_cn[mid].code < (u16)code)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return NULL;
}
//--------------------------------------------------------------
//End of file:  font_cn.c
//--------------------------------------------------------------
"@

[System.IO.File]::WriteAllText($outPath, ($header + "`r`n" + ($entries -join "`r`n") + $footer + "`r`n"), (New-Object System.Text.UTF8Encoding($false)))
Write-Host "Written: $outPath"
Write-Host "Glyphs: $($entries.Count), empty: $emptyCount"
Write-Host ($preview -join "`n")
