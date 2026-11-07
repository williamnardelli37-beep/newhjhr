NODO COMERCIAL 3.0.5 — BUILD MULTIPLATAFORMA

O QUE ESTA PREPARADO
- macOS 12 ou superior: app universal Intel + Apple Silicon, Qt 6.8.3, deployment target 12.0, DMG com janela visual de arrastar para Aplicativos e PKG opcional.
- Windows 7 SP1 / 8.1 / Windows 10 antigo: pacote legado x86 com Qt 5.15.2 + MinGW 8.1.
- Windows 10 1809 ou superior / Windows 11: pacote moderno x64 com Qt 6.8.3 + MSVC 2022.
- Linux desktop moderno x86_64: AppImage, DEB e tar.gz baseados em Ubuntu 22.04 (glibc 2.35).

PUBLICAR
1. Extraia esta pasta em Downloads.
2. Abra o Terminal e execute:
   bash ~/Downloads/NODO-COMERCIAL-3.0.5/PUBLICAR-GITHUB-3.0.5.sh
3. Confira o resumo mostrado pelo script.
4. Digite PUBLICAR somente se quiser enviar o commit e a tag v3.0.5.

O script nao faz force-push, nao substitui tags e pede confirmacao antes de enviar.
A tag aciona o GitHub Actions; a Release so aparece quando todos os jobs passam.

TESTES EXECUTADOS NESTA PREPARACAO
- Compilacao C++ do CLI: aprovada.
- Smoke test de configuracao, servidor HTTP, health e escrita autenticada: aprovado.
- Testes de seguranca: 28 aprovados, 0 falhas.
- Testes de multiplos projetos/portas/isolamento: 28 aprovados, 0 falhas.
- Sintaxe dos scripts shell, Python e YAML do GitHub Actions: aprovada.

LIMITES DOS TESTES
Este ambiente nao possui macOS, Windows nem Qt desktop instalado. Portanto, nao foi possivel gerar/testar aqui o .app, o DMG, o PKG, os ZIPs Windows ou o AppImage. O workflow executa os testes nativos nos runners do GitHub depois do envio. A validacao do deployment target nao substitui teste real num Mac com macOS 12 nem em VMs Windows 7/8.1.

LINUX
"Linux moderno" e delimitado na matriz: x86_64 e glibc 2.35 ou superior. Nao prometa suporte universal a todas as distribuicoes, ARM Linux ou sistemas sem ambiente grafico.

VEJA TAMBEM
- MATRIZ-COMPATIBILIDADE.md
- ATUALIZAR-GITHUB-3.0.5.txt
- ITINERARIO-CONTINUIDADE.md
