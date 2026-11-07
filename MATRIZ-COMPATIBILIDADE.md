# NODO Comercial 3.0.5 — matriz de compatibilidade

Este pacote separa os runtimes por geração do sistema operacional para não usar uma versão do Qt que abandone sistemas antigos.

## macOS

- **Mínimo planejado:** macOS 12 Monterey.
- **Arquiteturas:** um único `.app` universal `x86_64 + arm64` (Mac Intel e Apple Silicon).
- **Qt:** 6.8.3 LTS, compilado em runner Intel macOS 26 com SDK Xcode 15+, `CMAKE_OSX_DEPLOYMENT_TARGET=12.0` e bundle validado apos `macdeployqt`.
- O empacotador executa `scripts/verificar_macos_minimo.py` sobre o bundle inteiro depois de `macdeployqt`. O pacote só passa se os binários Mach-O incluídos tiverem ambas as arquiteturas e não declararem um mínimo superior a 12.0.
- O Qt 6.12 não deve ser usado neste build: a documentação da linha 6.12 eleva o alvo macOS para 14.4; Qt 6.8 documenta macOS 12 ou superior.

## Windows

- **Windows 7 SP1, Windows 8.1 e Windows 10 antigo:** build legado x86 com Qt 5.15.2 e MinGW 8.1. O executável de 32 bits também pode ser executado em Windows x64 via WOW64. Use Windows 7 SP1 totalmente atualizado.
- **Windows 10 versão 1809 ou superior e Windows 11:** build moderno x64 com Qt 6.8.3 e MSVC 2022.
- São pacotes ZIP portáteis com `nodo-app.exe`, `nodo.exe`, Qt e plugins necessários. Eles não são ainda instaladores assinados `.exe`/`.msi`.
- O runtime Qt 5.15 é legado e não recebe o mesmo ciclo de atualizações do Qt 6. A compatibilidade com Windows 7/8.1 precisa ser confirmada em máquinas/VMs reais dessas versões; o runner Windows atual não é um Windows 7.

## Linux

- **Alvo publicado:** Linux desktop x86_64 moderno, baseado em Ubuntu 22.04 (glibc 2.35) ou distribuição compatível com essa base e bibliotecas X11/Wayland equivalentes. O AppImage reduz dependências de Qt, mas não carrega o glibc/kernel do sistema.
- **Pacotes:** AppImage, DEB e tar.gz.
- Qt 6.8.3; AppImage inclui Qt e plugins da interface. Não é correto prometer compatibilidade com toda distribuição Linux: distros rolling, arquiteturas ARM e ambientes sem bibliotecas gráficas precisam de validação própria.

## Testes previstos no CI

1. Compilar o CLI e executar `testes/platform_smoke.py` (cria configuração, inicia servidor HTTP, testa health e uma escrita autenticada).
2. Compilar a GUI e executar `testes/desktop_smoke.py` usando o plugin Qt `offscreen`, gerando capturas de todas as abas e encerrando automaticamente.
3. macOS: verificar slices `x86_64` e `arm64` e deployment target de todos os binários do `.app`.
4. Windows: verificar e empacotar `Qt5Widgets.dll` + `qwindows.dll` no legado e `Qt6Widgets.dll` + `qwindows.dll` no moderno; executar o CLI empacotado.
5. Linux: gerar AppImage, DEB e tar.gz e verificar se os arquivos finais existem.

**Limite de validação:** testes de CI comprovam compilação e smoke tests no runner atual. Eles não substituem testes de inicialização em um Mac real com macOS 12, uma VM Windows 7 SP1/8.1 ou uma coleção de distribuições Linux. O workflow deve permanecer vermelho caso um pacote não passe nos testes.

## Assinatura e Gatekeeper

O DMG é visual e permite arrastar `Nodo.app` para `Aplicativos`. Sem certificado Apple Developer ID e notarização, o macOS pode exibir avisos do Gatekeeper mesmo quando o binário é compatível com a versão do sistema. A assinatura/notarização comercial precisa das credenciais do proprietário.
