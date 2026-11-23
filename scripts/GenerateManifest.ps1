param(
    [string]$TargetDir = "$PSScriptRoot\..\build\Release",
    [string]$PrivateKeyPath = "$PSScriptRoot\..\..\ACSTCLab\keys\opit-response-private.pem"
)

# Este script roda no Windows PowerShell 5.1 (ja vem no Windows). Por isso:
#   - nao usa RSA.ImportFromPem (so existe no PowerShell 7 / .NET Core);
#   - nao usa acentos: o PS 5.1 le .ps1 sem BOM pela ACP e quebraria as mensagens.
# A chave PKCS#8 e decodificada a mao em DER e injetada via RSAParameters.

$ErrorActionPreference = "Stop"

if (-not (Test-Path $TargetDir)) {
    Write-Error "Diretorio alvo nao encontrado: $TargetDir"
}

if (-not (Test-Path $PrivateKeyPath)) {
    Write-Output "AVISO: Chave privada oficial nao encontrada ($PrivateKeyPath). Pulando assinatura de manifesto (build local/comunitario)."
    exit 0
}

function Read-OpitTlv {
    param([byte[]]$Der, [int]$Start)
    $p = $Start
    $tag = [int]$Der[$p]
    $p++
    $len = [int]$Der[$p]
    $p++
    if ($len -gt 0x7F) {
        $nBytes = $len -band 0x7F
        $len = 0
        for ($i = 0; $i -lt $nBytes; $i++) { $len = ($len -shl 8) -bor [int]$Der[$p]; $p++ }
    }
    $value = New-Object byte[] $len
    [Array]::Copy($Der, $p, $value, 0, $len)
    return [pscustomobject]@{ Tag = $tag; Value = $value; Next = $p + $len }
}

function Convert-OpitStrip {
    param([byte[]]$Raw)
    $start = 0
    while ($start -lt ($Raw.Length - 1) -and $Raw[$start] -eq 0) { $start++ }
    $content = New-Object byte[] ($Raw.Length - $start)
    [Array]::Copy($Raw, $start, $content, 0, $content.Length)
    return ,$content
}

function Convert-OpitInteger {
    param([byte[]]$Raw, [int]$Size)
    $content = Convert-OpitStrip -Raw $Raw
    if ($content.Length -gt $Size) {
        throw "Inteiro RSA de $($content.Length) bytes excede o esperado $Size"
    }
    $out = New-Object byte[] $Size
    [Array]::Copy($content, 0, $out, $Size - $content.Length, $content.Length)
    return ,$out
}

function Read-OpitIntegerFields {
    param([byte[]]$Der)
    $seq = Read-OpitTlv -Der $Der -Start 0
    if ($seq.Tag -ne 0x30) { throw "esperava SEQUENCE" }
    $fields = @()
    $cursor = 0
    while ($cursor -lt $seq.Value.Length) {
        $f = Read-OpitTlv -Der $seq.Value -Start $cursor
        $fields += $f
        $cursor = $f.Next
    }
    return ,$fields
}

function Import-OpitRsaPrivateKey {
    param([string]$PemPath)

    $pem = Get-Content $PemPath -Raw
    if ($pem -notmatch '-----BEGIN PRIVATE KEY-----') {
        throw "A chave precisa estar em PKCS#8 (-----BEGIN PRIVATE KEY-----): $PemPath"
    }
    $b64 = $pem -replace '-----BEGIN PRIVATE KEY-----', '' -replace '-----END PRIVATE KEY-----', ''
    $b64 = $b64 -replace '\s', ''
    $der = [Convert]::FromBase64String($b64)

    $pkcs8 = Read-OpitTlv -Der $der -Start 0
    if ($pkcs8.Tag -ne 0x30) { throw "PKCS#8: esperava SEQUENCE no topo" }

    $version = Read-OpitTlv -Der $pkcs8.Value -Start 0
    $algId   = Read-OpitTlv -Der $pkcs8.Value -Start $version.Next
    $keyTlv  = Read-OpitTlv -Der $pkcs8.Value -Start $algId.Next
    if ($keyTlv.Tag -ne 0x04) { throw "PKCS#8: esperava OCTET STRING para a chave RSA" }

    $fields = Read-OpitIntegerFields -Der $keyTlv.Value
    if ($fields.Count -lt 9) { throw "RSAPrivateKey: esperava 9 inteiros, vieram $($fields.Count)" }

    $modulus = Convert-OpitStrip -Raw $fields[1].Value
    $modSize = $modulus.Length
    $half = [int]($modSize / 2)

    $params = New-Object System.Security.Cryptography.RSAParameters
    $params.Modulus  = $modulus
    $params.Exponent = Convert-OpitStrip -Raw $fields[2].Value
    $params.D        = Convert-OpitInteger -Raw $fields[3].Value -Size $modSize
    $params.P        = Convert-OpitInteger -Raw $fields[4].Value -Size $half
    $params.Q        = Convert-OpitInteger -Raw $fields[5].Value -Size $half
    $params.DP       = Convert-OpitInteger -Raw $fields[6].Value -Size $half
    $params.DQ       = Convert-OpitInteger -Raw $fields[7].Value -Size $half
    $params.InverseQ = Convert-OpitInteger -Raw $fields[8].Value -Size $half

    $rsa = [System.Security.Cryptography.RSA]::Create()
    $rsa.ImportParameters($params)
    return $rsa
}

function Get-OpitPublicKeyRsa {
    param([string]$PemPath)

    $pem = Get-Content $PemPath -Raw
    $b64 = $pem -replace '-----BEGIN PUBLIC KEY-----', '' -replace '-----END PUBLIC KEY-----', ''
    $b64 = $b64 -replace '\s', ''
    $der = [Convert]::FromBase64String($b64)

    $spki = Read-OpitTlv -Der $der -Start 0
    $algId = Read-OpitTlv -Der $spki.Value -Start 0
    $bitString = Read-OpitTlv -Der $spki.Value -Start $algId.Next
    if ($bitString.Tag -ne 0x03) { throw "SPKI: esperava BIT STRING" }

    # BIT STRING comeca com o numero de bits unused (0x00)
    $inner = New-Object byte[] ($bitString.Value.Length - 1)
    [Array]::Copy($bitString.Value, 1, $inner, 0, $inner.Length)

    $fields = Read-OpitIntegerFields -Der $inner
    $params = New-Object System.Security.Cryptography.RSAParameters
    $params.Modulus = Convert-OpitStrip -Raw $fields[0].Value
    $params.Exponent = Convert-OpitStrip -Raw $fields[1].Value

    $rsa = [System.Security.Cryptography.RSA]::Create()
    $rsa.ImportParameters($params)
    return $rsa
}

$filesToCatalog = @(
    @{ Path = "Opit-Core.exe"; Component = "core_executable"; Critical = $true },
    @{ Path = "opitcore_lhm_bridge.dll"; Component = "sensor_bridge"; Critical = $false },
    @{ Path = "LibreHardwareMonitorLib.dll"; Component = "sensor_library"; Critical = $false },
    @{ Path = "LibreHardwareMonitor.exe"; Component = "sensor_host"; Critical = $false },
    @{ Path = "HidSharp.dll"; Component = "hid_library"; Critical = $false },
    @{ Path = "DiskInfoToolkit.dll"; Component = "disk_toolkit"; Critical = $false },
    @{ Path = "RAMSPDToolkit-NDD.dll"; Component = "ram_toolkit"; Critical = $false },
    @{ Path = "BlackSharp.Core.dll"; Component = "blacksharp"; Critical = $false },
    @{ Path = "System.Memory.dll"; Component = "dotnet_memory"; Critical = $false },
    @{ Path = "System.Buffers.dll"; Component = "dotnet_buffers"; Critical = $false },
    @{ Path = "System.Numerics.Vectors.dll"; Component = "dotnet_vectors"; Critical = $false },
    @{ Path = "System.Runtime.CompilerServices.Unsafe.dll"; Component = "dotnet_unsafe"; Critical = $false }
)

$records = @()
$ausentes = @()

foreach ($entry in $filesToCatalog) {
    $fullPath = Join-Path $TargetDir $entry.Path
    if (Test-Path $fullPath) {
        $sha = (Get-FileHash -Algorithm SHA256 -Path $fullPath).Hash.ToLowerInvariant()
        $records += [ordered]@{
            path      = $entry.Path
            component = $entry.Component
            critical  = $entry.Critical
            sha256    = $sha
        }
    } else {
        $ausentes += $entry.Path
    }
}

if ($records.Count -eq 0) {
    Write-Error "Nenhum arquivo catalogado encontrado em $TargetDir - nada para assinar."
}

$manifestObj = [ordered]@{
    product    = "Opit-Core"
    version    = "8.2.0"
    minVersion = "8.0.0"
    algorithm  = "SHA-256"
    issuedAt   = (Get-Date).ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:ssZ")
    files      = $records
}

$jsonPayload = $manifestObj | ConvertTo-Json -Depth 5 -Compress

$manifestPath = Join-Path $TargetDir "opit_manifest.json"
$sigPath = Join-Path $TargetDir "opit_manifest.sig"

# UTF-8 sem BOM: o verificador assina e le os mesmos bytes, um BOM quebraria a assinatura.
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($manifestPath, $jsonPayload, $utf8NoBom)

$rsa = Import-OpitRsaPrivateKey -PemPath $PrivateKeyPath
$dataBytes = [System.Text.Encoding]::UTF8.GetBytes($jsonPayload)
$sigBytes = $rsa.SignData($dataBytes, [System.Security.Cryptography.HashAlgorithmName]::SHA256, [System.Security.Cryptography.RSASignaturePadding]::Pkcs1)
$sigB64 = [System.Convert]::ToBase64String($sigBytes)
[System.IO.File]::WriteAllText($sigPath, $sigB64, $utf8NoBom)

# Autoconferencia com a chave publica oficial (a mesma embutida em ManifestVerifier.cpp).
$pubPemPath = Join-Path (Split-Path $PrivateKeyPath -Parent) "opit-response-public.pem"
if (Test-Path $pubPemPath) {
    $verifyRsa = Get-OpitPublicKeyRsa -PemPath $pubPemPath
    if (-not $verifyRsa.VerifyData($dataBytes, $sigBytes, [System.Security.Cryptography.HashAlgorithmName]::SHA256, [System.Security.Cryptography.RSASignaturePadding]::Pkcs1)) {
        Write-Error "Autoconferencia da assinatura falhou - chave privada nao corresponde a chave publica oficial."
    }
    Write-Output "Autoconferencia RSA-SHA256 com a chave publica oficial: OK"
}

if ($ausentes.Count -gt 0) {
    Write-Output ("AVISO: " + $ausentes.Count + " arquivo(s) catalogado(s) ausente(s) em " + $TargetDir + " : " + ($ausentes -join ', '))
}

Write-Output "Manifesto gerado com sucesso em: $manifestPath"
Write-Output "Assinatura digital gravada em: $sigPath ($($records.Count) arquivos catalogados)"
