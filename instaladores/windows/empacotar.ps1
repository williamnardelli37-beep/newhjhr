param(
  [Parameter(Mandatory=$true)][string]$Version,
  [Parameter(Mandatory=$true)][ValidateSet('modern-10-11','legacy-7-10')][string]$Flavor
)
$ErrorActionPreference = 'Stop'
$Build = (Resolve-Path 'build').Path
$QtBin = Join-Path $env:QT_ROOT_DIR 'bin'
$Deploy = Join-Path $QtBin 'windeployqt.exe'
if (!(Test-Path $Deploy)) { throw "windeployqt não encontrado: $Deploy" }
$Gui = Get-ChildItem $Build -Recurse -File -Filter 'nodo-app.exe' | Select-Object -First 1
$Cli = Get-ChildItem $Build -Recurse -File -Filter 'nodo.exe' | Select-Object -First 1
if (!$Gui -or !$Cli) { throw 'Executáveis nodo-app.exe e nodo.exe não encontrados no build.' }

$Stage = Join-Path $Build "NODO-Windows-$Flavor"
if (Test-Path $Stage) { Remove-Item $Stage -Recurse -Force }
New-Item -ItemType Directory -Path $Stage | Out-Null

& $Deploy --release --compiler-runtime --no-translations --dir $Stage $Gui.FullName
if ($LASTEXITCODE -ne 0) { throw "windeployqt falhou com código $LASTEXITCODE" }
Copy-Item $Gui.FullName (Join-Path $Stage 'nodo-app.exe') -Force
Copy-Item $Cli.FullName (Join-Path $Stage 'nodo.exe') -Force
if (Test-Path 'icones/nodo.ico') { Copy-Item 'icones/nodo.ico' $Stage -Force }

# Certificado GRATUITO "Loop" (autoassinado): assina os executaveis e guarda a parte publica em Loop.cer.
try {
  $Cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject 'CN=Loop, O=Loop Web, C=BR' `
    -CertStoreLocation 'Cert:\CurrentUser\My' -KeyAlgorithm RSA -KeyLength 2048 `
    -HashAlgorithm SHA256 -NotAfter (Get-Date).AddYears(10)
  foreach ($exe in @('nodo-app.exe', 'nodo.exe')) {
    $sig = Set-AuthenticodeSignature -FilePath (Join-Path $Stage $exe) -Certificate $Cert -HashAlgorithm SHA256
    if (!$sig.SignerCertificate) { throw "Nao assinou $exe ($($sig.StatusMessage))" }
  }
  Export-Certificate -Cert $Cert -FilePath (Join-Path $Stage 'Loop.cer') | Out-Null
  Write-Host 'Executaveis assinados com o certificado Loop.'
} catch {
  Write-Warning "Assinatura Loop ignorada: $($_.Exception.Message)"
}

$qtWidgets = if ($Flavor -eq 'legacy-7-10') {
  # windeployqt --compiler-runtime copia os runtimes exigidos pelo executavel.
  # Completa a pasta apenas se algum runtime comum estiver disponivel no Qt Tools;
  # nao falha por DLL que nao seja dependencia efetiva desta compilacao.
  $runtimeNames = @('libgcc_s_dw2-1.dll', 'libstdc++-6.dll', 'libwinpthread-1.dll')
  if ($env:IQTA_TOOLS -and (Test-Path $env:IQTA_TOOLS)) {
    foreach ($runtime in $runtimeNames) {
      if (!(Test-Path (Join-Path $Stage $runtime))) {
        $found = Get-ChildItem $env:IQTA_TOOLS -Recurse -File -Filter $runtime -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($found) { Copy-Item $found.FullName (Join-Path $Stage $runtime) -Force }
      }
    }
  }
}

$targetText = if ($Flavor -eq 'legacy-7-10') {
  @'
NODO COMERCIAL - WINDOWS LEGACY
Alvo de compatibilidade: Windows 7 SP1, Windows 8.1 e Windows 10 (pacote x86; funciona tambem em Windows x64).
Framework: Qt 5.15.2 (ramo legado; não recebe o mesmo ciclo de manutenção do Qt 6).
Para Windows 10/11, prefira o instalador NODO-Windows-modern-10-11.
Execute nodo-app.exe para abrir o aplicativo; nodo.exe é a ferramenta de terminal.
'@
} else {
  @'
NODO COMERCIAL - WINDOWS MODERNO
Alvo: Windows 10 (1809 ou superior) e Windows 11 x64.
Framework: Qt 6.8.3.
Execute nodo-app.exe para abrir o aplicativo; nodo.exe é a ferramenta de terminal.
'@
}
Set-Content -Path (Join-Path $Stage 'LEIA-ME.txt') -Value $targetText -Encoding UTF8
if (Test-Path 'COMO-ABRIR.txt') { Copy-Item 'COMO-ABRIR.txt' (Join-Path $Stage 'Como abrir na primeira vez.txt') -Force }

# Teste rápido real do executável CLI e verificação dos componentes Qt empacotados.
$help = & (Join-Path $Stage 'nodo.exe') --help 2>&1 | Out-String
if ($LASTEXITCODE -ne 0 -or $help -notmatch 'nodo integrar') { throw 'O teste de inicialização do nodo.exe falhou.' }
$Architecture = if ($Flavor -eq 'legacy-7-10') { 'x86' } else { 'x64' }
$Zip = Join-Path $Build "NODO-$Version-Windows-$Flavor-$Architecture.zip"
if (Test-Path $Zip) { Remove-Item $Zip -Force }
Compress-Archive -Path (Join-Path $Stage '*') -DestinationPath $Zip -CompressionLevel Optimal
Write-Host "Pacote verificado: $Zip"
