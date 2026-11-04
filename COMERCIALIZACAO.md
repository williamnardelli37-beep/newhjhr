# NODO Desktop — preparação comercial

## O que mudou nesta reconstrução

- Interface Qt simplificada para uma identidade branca, preta e cinza.
- Marca visual reduzida à letra **N** e ao nome NODO.
- Navegação removeu as telas e gerador de prompts para IA (desktop, painel web e CLI), atualizações internas, reversão/desinstalação de atualizações e controle do PC.
- Corrigido `std::string::isEmpty()` para `std::string::empty()` no fluxo de compilação.
- Criada a página `downloads/index.html`, com seletores de sistema operacional e versão. Ela só habilita downloads que estejam publicados em `downloads/releases.json`.

## Importante: o que ainda não está publicado

Esta pasta contém código-fonte e a página de downloads, não instaladores comerciais assinados. Os instaladores precisam ser compilados em ambientes próprios para Windows, macOS e Linux, testados e assinados:

- Windows: pacote `.exe` ou `.msi`, assinado com certificado de publicação.
- macOS: `.dmg` ou `.pkg`, assinado com Developer ID e notarizado pela Apple.
- Linux: `.AppImage` ou `.deb`, testado nas distribuições-alvo.

Não é correto anunciar suporte/compatibilidade final antes de testar os binários em máquinas limpas de cada plataforma.

## Publicar uma versão

1. Compile e teste o aplicativo para cada sistema e arquitetura suportados.
2. Hospede os instaladores em HTTPS (site/CDN ou releases privadas/públicas conforme a licença).
3. Edite `downloads/releases.json`, adicionando a versão e URLs HTTPS reais. Exemplo de estrutura:

```json
{
  "product": "NODO",
  "releases": [{
    "version": "3.1.0",
    "stable": true,
    "downloads": {
      "windows": {"url": "https://downloads.example/nodo-3.1.0-windows-x64.exe", "filename": "nodo-3.1.0-windows-x64.exe", "arch": "x64"},
      "macos": {"url": "https://downloads.example/nodo-3.1.0-macos-universal.dmg", "filename": "nodo-3.1.0-macos-universal.dmg", "arch": "Intel + Apple Silicon"},
      "linux": {"url": "https://downloads.example/nodo-3.1.0-linux-x86_64.AppImage", "filename": "nodo-3.1.0-linux-x86_64.AppImage", "arch": "x86_64"}
    }
  }]
}
```

4. Publique `downloads/` no domínio oficial. A página não cria nem hospeda binários automaticamente.

## Proteção do código e licença

A distribuição de binários evita entregar os arquivos `.cpp`/`.hpp` no instalador, mas C++ compilado pode ser analisado por engenharia reversa. Não existe proteção absoluta contra isso. Remova símbolos de depuração dos pacotes de produção, mantenha segredos no servidor e revise a licença de todas as dependências antes de vender o produto.

## Compilação local

Requisitos: CMake, compilador C++17 e Qt 6 Widgets.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

No macOS, o bundle é gerado como `build/Nodo.app` quando Qt 6 está instalado e localizado pelo CMake.

## Qt 6 obrigatório e geração dos instaladores (revisão de build)

A compilação desktop agora exige Qt 6.5 ou superior com o módulo Widgets. O padrão é `NODO_BUILD_GUI=ON`; não há fallback silencioso para CLI. A opção `-DNODO_BUILD_GUI=OFF` existe apenas para testes/uso de servidor e não gera o produto desktop.

### Compilar no macOS (Intel e Apple Silicon)

```sh
brew install cmake
./compilar.sh              # universal (padrão): Intel x86_64 + Apple Silicon arm64
./compilar.sh intel        # somente Intel
./compilar.sh arm          # somente Apple Silicon
```

O app sai **universal** por padrão (`-DNODO_MAC_ARCH=universal`, alvo mínimo macOS 12). Para rodar em Intel o Qt também precisa ter `x86_64`: o Qt do Homebrew num Mac Apple Silicon é só `arm64` e não serve. O script usa, nesta ordem, `$NODO_QT`, `~/Qt/6.*/macos` (instalador oficial) ou o Homebrew, e instala o Qt oficial universal em `~/Qt` se nenhum servir. O CMake também confere isso e falha com mensagem clara. Trocar de arquitetura apaga `build/` automaticamente.

Para gerar DMG e PKG (confere que todas as bibliotecas do `.app` têm as mesmas arquiteturas):

```sh
VERSAO=3.0.1 sh instaladores/mac/montar-dmg.sh   # gera Nodo-3.0.1-mac-universal.dmg/.pkg
```

Verificar: `lipo -archs build/Nodo.app/Contents/MacOS/Nodo` (universal: `x86_64 arm64`).

### Instalar Qt 6 no Ubuntu/Debian

```sh
sudo apt update
sudo apt install cmake g++ qt6-base-dev qt6-tools-dev pkg-config
./compilar.sh
cd build && cpack
```

### Windows

Use Visual Studio 2022 (C++ Desktop), CMake e Qt 6.8.3 para MSVC 2022 64-bit. O fluxo automatizado em `.github/workflows/build-installers.yml` instala Qt 6 e gera um ZIP com o executável e dependências implantadas.

### Builds automatizados

O workflow `.github/workflows/build-installers.yml` constrói em runners Windows, macOS e Ubuntu, usa Qt 6.8.3 e publica artefatos de teste. Para executá-lo manualmente: GitHub → Actions → Build NODO desktop installers → Run workflow. Para disparar por tag, crie uma tag `vX.Y.Z` depois de atualizar `project(VERSION ...)`.

**Estado de validação desta revisão:** os scripts e o pipeline foram adicionados ao projeto, mas os instaladores finais ainda precisam ser produzidos e testados nos três sistemas operacionais. A assinatura do Windows, a assinatura/notarização Apple e a publicação dos arquivos HTTPS exigem certificados/contas do proprietário e não são simuladas por este repositório. Não preencher `downloads/releases.json` com URLs fictícias.


## Instalação final no macOS sem Homebrew ou Xcode no computador do usuário

O usuário final não deve compilar o projeto. O workflow do GitHub Actions compila o app universal e, ao enviar uma tag `v*`, publica o DMG na GitHub Release. O script `instaladores/mac/instalar-latest.sh` baixa o DMG da última release, instala em `/Applications`, cria `~/.local/bin/nodo` e abre o aplicativo. Consulte `INSTALACAO-MAC-AUTOMATICA.md`.

A compilação local continua sendo um fluxo de desenvolvimento e requer CMake, Qt e ferramentas de compilação. Isso não é requisito para quem instala o aplicativo já compilado.
