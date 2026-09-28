foreach ($p in @('d:\wLaunchELF_R3Z-master\check_table.ps1', 'd:\wLaunchELF_R3Z-master\make_game_titles_cn.ps1')) {
	$c = [System.IO.File]::ReadAllText($p, [System.Text.Encoding]::UTF8)
	[System.IO.File]::WriteAllText($p, $c, (New-Object System.Text.UTF8Encoding($true)))
	"bom ok: $p"
}
