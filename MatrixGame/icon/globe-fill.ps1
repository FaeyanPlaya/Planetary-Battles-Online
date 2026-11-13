# Makes globe-fill.png from icon.png: white where the globe is (inside its outline), clear elsewhere. icon.svg lays it
# under the picture, so the globe is white inside on any background. Run again after a new icon.png.
# The gap between the sight and the globe is flooded from a point in it (through the clear pixels); what the flood does
# not reach inside the sight's ring is the globe.
Add-Type -AssemblyName System.Drawing
$src = [System.Drawing.Bitmap]::FromFile("$PSScriptRoot\icon.png")
$w = $src.Width; $h = $src.Height
$seedX = [int]($w / 2); $seedY = [int]($h * 48 / 256)   # between the ring and the globe, above the globe
$inner = $w * 90 / 256                                  # the ring's inside: the globe is within it
$cx = ($w - 1) / 2; $cy = ($h - 1) / 2
$alpha = New-Object 'byte[,]' $w, $h
for ($y = 0; $y -lt $h; $y++) { for ($x = 0; $x -lt $w; $x++) { $alpha[$x, $y] = $src.GetPixel($x, $y).A } }
$src.Dispose()
if ($alpha[$seedX, $seedY] -ge 128) { throw "the seed ($seedX, $seedY) is not clear: move it into the gap" }
$outside = New-Object 'bool[,]' $w, $h
$queue = New-Object 'System.Collections.Generic.Queue[int]'
$queue.Enqueue($seedY * $w + $seedX); $outside[$seedX, $seedY] = $true
while ($queue.Count -gt 0) {
    $i = $queue.Dequeue(); $x = $i % $w; $y = [int][Math]::Floor($i / $w)
    foreach ($d in @(@(1, 0), @(-1, 0), @(0, 1), @(0, -1))) {
        $nx = $x + $d[0]; $ny = $y + $d[1]
        if ($nx -lt 0 -or $ny -lt 0 -or $nx -ge $w -or $ny -ge $h -or $outside[$nx, $ny]) { continue }
        if ($alpha[$nx, $ny] -ge 128) { continue }
        $outside[$nx, $ny] = $true; $queue.Enqueue($ny * $w + $nx)
    }
}
$fill = New-Object System.Drawing.Bitmap $w, $h, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$count = 0
for ($y = 0; $y -lt $h; $y++) {
    for ($x = 0; $x -lt $w; $x++) {
        $r = [Math]::Sqrt(($x - $cx) * ($x - $cx) + ($y - $cy) * ($y - $cy))
        if ($r -lt $inner -and -not $outside[$x, $y]) { $fill.SetPixel($x, $y, [System.Drawing.Color]::White); $count++ }
    }
}
$fill.Save("$PSScriptRoot\globe-fill.png", [System.Drawing.Imaging.ImageFormat]::Png)
"globe-fill.png: $count white pixels"
