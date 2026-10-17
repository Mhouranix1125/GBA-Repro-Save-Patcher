param(
    [Parameter(Mandatory=$true)][string]$InputBin,
    [Parameter(Mandatory=$true)][string]$OutputCpp
)

$ErrorActionPreference = "Stop"

$bytes = [System.IO.File]::ReadAllBytes($InputBin)
$sb = New-Object System.Text.StringBuilder

[void]$sb.AppendLine('extern "C" {')
[void]$sb.Append('unsigned char payload_bin[] = {')

for ($i = 0; $i -lt $bytes.Length; $i++) {
    if ($i -gt 0) {
        [void]$sb.Append(',')
    }
    [void]$sb.Append(('0x{0:X2}' -f $bytes[$i]))
}

[void]$sb.AppendLine('};')
[void]$sb.AppendLine('unsigned int payload_bin_len = sizeof(payload_bin);')
[void]$sb.AppendLine('}')

$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($OutputCpp, $sb.ToString(), $utf8NoBom)

$firstLine = [System.IO.File]::ReadLines($OutputCpp) | Select-Object -First 1
if ($firstLine -ne 'extern "C" {') {
    throw "Generated payload C++ file is malformed: $firstLine"
}
