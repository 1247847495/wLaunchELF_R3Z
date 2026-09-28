$OutputEncoding = [System.Text.Encoding]::UTF8
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$f = [System.IO.File]::ReadAllLines('d:\wLaunchELF_R3Z-master\src\game_titles_cn.h', [System.Text.Encoding]::UTF8)
"total: $($f.Count)"
$map = @{}
foreach ($l in $f) {
	$m = [regex]::Match($l, '^\{"([A-Z0-9-]+)", "(.+)"\}')
	if ($m.Success) { $map[$m.Groups[1].Value] = $m.Groups[2].Value }
}
"--- 验证关键条目 ---"
$codes = @('SLUS-20946','SCES-51061','SLUS-20001','SCES-50383','SCES-50358','SLUS-20911','SCES-51799','SCES-53312','SLPM-65241','SLUS-20130','SCES-54163','SLUS-21485','SLPM-65522','SCES-52725','SLES-53557','SCES-54439','SCAJ-20164','SLES-54163','SLES-54644','SCES-52118','SCES-53755','SCUS-97398','SLPM-66941','SCES-52384','SCES-50821','SCES-530.38','SLUS-20485','SLPM-65079','SLES-50591','SCES-51190')
foreach ($c in $codes) {
	if ($map.ContainsKey($c)) { "  $c -> $($map[$c])" } else { "  $c -> (未命中,回退原标题)" }
}
"--- CSV 原始标题对照 ---"
$csv = [System.IO.File]::ReadAllLines('d:\wLaunchELF_R3Z-master\gamename.csv', [System.Text.Encoding]::UTF8)
$prefixes = @('SLUS_209.46','SCES_510.61','SLUS_200.01','SCES_503.83','SCES_503.58','SLUS_209.11','SCES_517.99','SCES_533.12','SLPM_652.41','SLUS_201.30','SCES_541.63','SCES_527.25','SLES_535.57','SCES_544.39','SCAJ_201.64','SLES_541.63','SLES_546.44','SCES_521.18','SCES_537.55','SCUS_973.98','SLPM_669.41','SCES_523.84','SCES_508.21','SLUS_204.85','SLPM_650.79','SLES_505.91')
foreach ($p in $prefixes) {
	$hit = $csv | Where-Object { $_.StartsWith($p + ';') } | Select-Object -First 1
	if ($hit) { "  $hit" } else { "  $p; <无>" }
}
