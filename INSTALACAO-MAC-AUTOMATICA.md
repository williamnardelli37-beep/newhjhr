# Instalação automática do NODO no macOS

## Objetivo
O computador do usuário final não precisa instalar Homebrew, CMake, Qt, Xcode ou ferramentas de compilação. Os binários são compilados no GitHub Actions e distribuídos como um DMG macOS universal (Intel x86_64 + Apple Silicon arm64).

## Publicação
1. Envie o repositório para o GitHub.
2. Confirme que o workflow `Build NODO desktop installers` está ativo.
3. Atualize `project(VERSION ...)` no `CMakeLists.txt` para a versão que será publicada.
4. Crie e envie uma tag, por exemplo: `git tag v3.0.1 && git push origin v3.0.1`.
5. O workflow compila Windows, macOS e Linux; o job `publish-release` anexa os instaladores à GitHub Release.

## Instalação no Mac
O script `instaladores/mac/instalar-latest.sh` baixa o DMG universal da última GitHub Release, instala `Nodo.app` em `/Applications`, cria o comando CLI em `~/.local/bin/nodo` e abre o app. Ele não instala nem remove ferramentas de segurança do macOS.

Defina o repositório real e execute:

```sh
NODO_GITHUB_REPO="organizacao/repositorio" bash instaladores/mac/instalar-latest.sh
```

O script usa utilitários que já acompanham o macOS: `curl`, `ruby`, `hdiutil` e `ditto`. A cópia para `/Applications` pode solicitar senha de administrador.

## Instalação sem clonar o projeto
Depois de publicar o repositório e a release, o comando remoto pode ser:

```sh
curl -fsSL "https://raw.githubusercontent.com/organizacao/repositorio/main/instaladores/mac/instalar-latest.sh" | NODO_GITHUB_REPO="organizacao/repositorio" bash
```

Substitua `organizacao/repositorio` pelo endereço real. Não use esse comando antes de publicar o script no repositório correto.

## Segurança e limitações
- O script não remove `com.apple.quarantine`, não desativa Gatekeeper e não simula notarização.
- Para distribuição pública sem alertas de desenvolvedor não identificado, assine com Developer ID e faça notarização Apple antes de publicar.
- O DMG é universal; o macOS escolhe automaticamente a arquitetura compatível.
- O job de release só publica quando uma tag `v*` é enviada. `workflow_dispatch` continua útil para validar os builds como artefatos temporários.
