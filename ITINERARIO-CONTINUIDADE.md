# NODO — Itinerário de continuidade do projeto

Última atualização: 2026-11-04

## Objetivo do produto
Reconstruir o Nodo 3 como aplicativo comercial desktop, minimalista, branco/preto/cinza, marca com a letra N, sem recursos de IA, sem telas de atualização/reversão e sem controle remoto do PC. Distribuição pretendida: Windows, macOS e Linux, por instaladores baixados de uma página web com seletor de sistema e versão.

## O que foi feito nesta etapa
1. Inspecionado o pacote `NODO-Reconstrucao-Comercial.zip` e sua estrutura CMake/Qt.
2. Tornado Qt 6 (mínimo 6.2, componente Widgets) obrigatório por padrão para compilar o app gráfico (`NODO_BUILD_GUI=ON`). A compilação não deve mais aparentar sucesso de desktop quando Qt está ausente.
3. Adicionadas regras de instalação CMake para o app gráfico e implantação das dependências Qt por `qt_generate_deploy_app_script`.
4. Configurado CPack para gerar ZIP no Windows, DMG no macOS e DEB/TGZ no Linux.
5. Atualizado `compilar.sh` para verificar Qt 6 e compilar o app gráfico, instalando a distribuição local em `dist/`.
6. Criado `.github/workflows/build-installers.yml` para builds automatizados em runners Windows, macOS e Ubuntu com Qt 6.8.3.
7. Atualizada `COMERCIALIZACAO.md` com requisitos, comandos e limitações de assinatura/publicação.

## Arquivos alterados ou criados
- `CMakeLists.txt`: Qt 6 obrigatório por padrão, regras de instalação, implantação e CPack.
- `compilar.sh`: instalação/verificação de pré-requisitos e build desktop.
- `.github/workflows/build-installers.yml`: pipeline multiplataforma.
- `COMERCIALIZACAO.md`: documentação de build e distribuição.
- `ITINERARIO-CONTINUIDADE.md`: este registro.

## Estado real — não confundir preparação com lançamento
- Os instaladores ainda NÃO foram compilados/testados nesta etapa.
- Este ambiente de trabalho não fornece uma instalação Qt 6 desktop validada para Windows/macOS/Linux.
- Builds automatizados precisam ser executados no GitHub Actions e os artefatos precisam ser testados em máquinas limpas.
- Windows ainda precisa de assinatura Authenticode para distribuição comercial confiável.
- macOS ainda precisa de Developer ID, assinatura e notarização Apple para distribuição fora da App Store sem alertas de desenvolvedor não identificado.
- Linux precisa ser testado nas distribuições-alvo; DEB/TGZ não equivale a AppImage universal.
- A página `downloads/` depende de URLs HTTPS reais em `downloads/releases.json`; manter vazio até haver binários publicados.
- Binários não incluem os fontes, mas C++ compilado ainda pode sofrer engenharia reversa. Não embutir segredos/chaves privadas.

## Próximos passos obrigatórios
1. Abrir o projeto no GitHub e executar o workflow `Build NODO desktop installers` manualmente.
2. Corrigir eventuais erros específicos de cada runner e repetir o workflow.
3. Baixar os artefatos Windows/macOS/Linux e testar instalação, abertura, persistência de dados, servidor e desinstalação em máquinas limpas.
4. Arquiteturas: o CMake e o workflow agora geram macOS universal (Intel + Apple Silicon); Windows x64 e Linux x86_64 já eram Intel/AMD. Falta testar o Nodo.app universal em um Mac Intel real (e em um Apple Silicon) e em macOS 12.
5. Adquirir/configurar certificados de assinatura e secrets do CI; assinar/notarizar os instaladores.
6. Publicar os arquivos em hospedagem HTTPS e preencher `downloads/releases.json` com URLs reais, hashes SHA-256 e notas da versão.
7. Gerar um novo ZIP de desenvolvimento com este itinerário atualizado.

## Verificações executadas nesta sessão
- `cmake -S . -B build-check -DNODO_BUILD_GUI=OFF` e `cmake --build build-check`: passaram; o alvo CLI compilou com sucesso.
- Configuração padrão `NODO_BUILD_GUI=ON`: parou corretamente no `find_package(Qt6 6.5 REQUIRED)` porque Qt 6 não está instalado neste ambiente de execução. Isso confirma a exigência explícita, mas não valida a compilação gráfica.
- Não foram gerados instaladores finais nem feita assinatura/notarização.

## Suporte a processadores Intel (alteração desta versão)
- Causa do problema: o `Nodo.app` do pacote anterior era arm64 (compilado em Mac Apple Silicon com o Qt do Homebrew, que é só arm64); o código-fonte em si é neutro quanto à arquitetura.
- `CMakeLists.txt`: `NODO_MAC_ARCH` (universal/intel/arm/native, padrão universal), alvo mínimo macOS 12 e conferência das arquiteturas do Qt.
- `compilar.sh`: escolhe/instala um Qt com as arquiteturas pedidas e refaz `build/` ao trocar de alvo.
- `instaladores/mac/montar-dmg.sh`: confere todas as bibliotecas do `.app` e põe a arquitetura no nome do DMG/PKG.
- Workflow: Mac universal (artefato `NODO-macOS-universal`) com verificação via `lipo`.
- Verificado em x86_64 (Linux): CLI compila e passa em `testes/rodar.py` (119 ok, 0 falhas); app Qt compila. NÃO verificado em Mac Intel real nem a parte Apple do CMake/scripts.

## Como outra IA deve retomar
Leia primeiro este arquivo, depois `LEIA-PRIMEIRO-COMERCIAL.txt`, `COMERCIALIZACAO.md`, `CMakeLists.txt` e `.github/workflows/build-installers.yml`. Não declare o produto pronto nem invente URLs de downloads. Priorize executar o workflow e corrigir os erros reais retornados pelos runners.


## Correção de continuidade — 2026-11-04 (pacote 3.0.1)
- O usuário executou `cpack` na pasta antiga `~/Downloads/nodo-3-mac`, que é o código anterior. Por isso recebeu `CPack Error: CPack generator not specified`.
- O log confirma que Qt 6.12.0 foi instalado via Homebrew. Também confirma que a versão antiga compilou `Nodo.app`, mas isso não valida a reconstrução comercial.
- Foi criado um ZIP novo com diretório raiz exclusivo `NODO-COMERCIAL-3.0.1/`, evitando misturar arquivos com `nodo-3-mac`.
- O CMake da reconstrução comercial já inclui CPack e define `DragNDrop` no macOS; para gerar o DMG explicitamente, executar `cd build && cpack -G DragNDrop`.
- Próximo passo: extrair este pacote em pasta nova, executar os comandos de `COMECE-AQUI-MAC.txt`, verificar `build/Nodo.app` e gerar o DMG. Depois testar o DMG em instalação limpa.
- Não declarar instalador final: ainda faltam assinatura Developer ID, notarização Apple e testes em máquina limpa.


## Continuidade — distribuição automática sem dependências locais (2026-11-04)
- `CMakeLists.txt` atualizado para a versão 3.0.1 e nome explícito do DMG macOS universal.
- Workflow atualizado para anexar os instaladores gerados a uma GitHub Release quando uma tag `v*` for enviada.
- `instaladores/mac/instalar-latest.sh` criado: busca a última release, baixa o DMG universal, instala o app, cria o comando CLI em `~/.local/bin/nodo` e abre o Nodo. Não instala Homebrew/CMake/Xcode e não desativa Gatekeeper.
- `montar-dmg.sh` não tenta mais chamar Homebrew como fallback para `macdeployqt`.
- Falta obrigatório: definir o endereço real do repositório GitHub, publicar o código, executar o workflow, testar os instaladores e configurar Developer ID/notarização para distribuição macOS sem alertas.
