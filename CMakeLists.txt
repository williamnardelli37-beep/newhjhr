cmake_minimum_required(VERSION 3.21)

# macOS: por padrao gera um app UNIVERSAL (Intel x86_64 + Apple Silicon arm64).
# Precisa ficar ANTES de project(). Opcoes: -DNODO_MAC_ARCH=universal|intel|arm|native
# (se voce passar -DCMAKE_OSX_ARCHITECTURES=... na linha de comando, ele vale mais que isto).
if(APPLE)
  set(NODO_MAC_ARCH "universal" CACHE STRING "Arquitetura do Mac: universal | intel | arm | native")
  set_property(CACHE NODO_MAC_ARCH PROPERTY STRINGS universal intel arm native)
  if(NOT CMAKE_OSX_ARCHITECTURES)
    if(NODO_MAC_ARCH STREQUAL "universal")
      set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "Arquiteturas do macOS" FORCE)
    elseif(NODO_MAC_ARCH STREQUAL "intel")
      set(CMAKE_OSX_ARCHITECTURES "x86_64" CACHE STRING "Arquiteturas do macOS" FORCE)
    elseif(NODO_MAC_ARCH STREQUAL "arm")
      set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Arquiteturas do macOS" FORCE)
    elseif(NOT NODO_MAC_ARCH STREQUAL "native")
      message(FATAL_ERROR "NODO_MAC_ARCH invalido: ${NODO_MAC_ARCH} (use universal, intel, arm ou native)")
    endif()
  endif()
  # Macs Intel costumam estar em macOS mais antigo: nao amarrar o app ao macOS da maquina que compilou.
  if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "macOS minimo" FORCE)
  endif()
endif()

project(Nodo VERSION 3.0.1 LANGUAGES CXX)
include(GNUInstallDirs)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Threads REQUIRED)

# Endurecimento do binario
if(NOT MSVC)
  add_compile_options(-Wall -fstack-protector-strong)
  if(NOT WIN32)
    add_compile_options(-D_FORTIFY_SOURCE=2)
  endif()
endif()

set(NODO_LIBS Threads::Threads)
if(WIN32)
  list(APPEND NODO_LIBS ws2_32 iphlpapi bcrypt psapi)
  if(MINGW)
    add_link_options(-static-libgcc -static-libstdc++ -static)
  endif()
endif()
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9)
  list(APPEND NODO_LIBS stdc++fs)
endif()

# Linha de comando: "nodo". Tambem e ela que liga e desliga cada projeto (um processo por site).
add_executable(nodo_cli nodo.cpp)
target_link_libraries(nodo_cli PRIVATE ${NODO_LIBS})
set_target_properties(nodo_cli PROPERTIES OUTPUT_NAME nodo)

# App comercial com janela (Qt 6 obrigatório por padrão).
# Nomes: no Mac o app e Nodo.app; no Windows/Linux o executavel e "nodo-app". Nunca "Nodo" ao lado de "nodo":
# Windows e Mac nao diferenciam maiusculas de minusculas e um arquivo sobrescreveria o outro.
option(NODO_BUILD_GUI "Compilar o aplicativo gráfico comercial" ON)
if(NODO_BUILD_GUI)
  find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets)
  # No Mac, o Qt precisa ter as mesmas arquiteturas do app. O Qt do Homebrew num Mac Apple Silicon e so arm64:
  # com ele nao sai binario Intel. Falhar aqui, com explicacao, e melhor que um erro de linker.
  if(APPLE AND CMAKE_OSX_ARCHITECTURES)
    get_target_property(_nodo_qtcore Qt6::Core LOCATION)
    execute_process(COMMAND lipo -archs "${_nodo_qtcore}" OUTPUT_VARIABLE _nodo_qt_archs
                    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE _nodo_lipo_rc)
    if(_nodo_lipo_rc EQUAL 0)
      string(REPLACE " " ";" _nodo_qt_archs "${_nodo_qt_archs}")
      foreach(_a IN LISTS CMAKE_OSX_ARCHITECTURES)
        if(NOT _a IN_LIST _nodo_qt_archs)
          message(FATAL_ERROR "O Qt encontrado (${Qt6_DIR}) so tem: ${_nodo_qt_archs}, mas o app pede: ${CMAKE_OSX_ARCHITECTURES}.\n"
                  "Use o Qt oficial (universal): rode ./compilar.sh, que instala e usa o certo, ou aponte -DCMAKE_PREFIX_PATH para ele.")
        endif()
      endforeach()
    endif()
    message(STATUS "Mac: arquiteturas do app = ${CMAKE_OSX_ARCHITECTURES}; Qt = ${_nodo_qt_archs}")
  endif()
  add_executable(nodo_app WIN32 MACOSX_BUNDLE nodo_gui.cpp)
  target_link_libraries(nodo_app PRIVATE Qt6::Widgets ${NODO_LIBS})
  if(APPLE)
    set_target_properties(nodo_app PROPERTIES OUTPUT_NAME Nodo
      MACOSX_BUNDLE_BUNDLE_NAME "Nodo"
      MACOSX_BUNDLE_GUI_IDENTIFIER "app.nodo.servidor"
      MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
      MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
      MACOSX_BUNDLE_COPYRIGHT "Loop Life in Server")
    if(EXISTS "${CMAKE_SOURCE_DIR}/icones/Nodo.icns")
      target_sources(nodo_app PRIVATE icones/Nodo.icns)
      set_source_files_properties(icones/Nodo.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
      set_target_properties(nodo_app PROPERTIES MACOSX_BUNDLE_ICON_FILE Nodo.icns)
    endif()
    # a linha de comando vai dentro do .app (como "nodo-cli": "nodo" e "Nodo" seriam o mesmo arquivo no Mac)
    add_custom_command(TARGET nodo_app POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:nodo_cli> $<TARGET_BUNDLE_CONTENT_DIR:nodo_app>/MacOS/nodo-cli
      )
    add_dependencies(nodo_app nodo_cli)
  else()
    set_target_properties(nodo_app PROPERTIES OUTPUT_NAME nodo-app)
    add_dependencies(nodo_app nodo_cli)
    if(WIN32 AND EXISTS "${CMAKE_SOURCE_DIR}/icones/nodo.rc")
      target_sources(nodo_app PRIVATE icones/nodo.rc)
    endif()
  endif()

  # Instala o aplicativo e prepara a redistribuicao das bibliotecas Qt.
  if(APPLE)
    install(TARGETS nodo_app BUNDLE DESTINATION .)
  else()
    install(TARGETS nodo_app RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(TARGETS nodo_cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  endif()
  if(COMMAND qt_generate_deploy_app_script)
    qt_generate_deploy_app_script(TARGET nodo_app OUTPUT_SCRIPT nodo_deploy_script NO_UNSUPPORTED_PLATFORM_ERROR)
    install(SCRIPT ${nodo_deploy_script})
  else()
    message(FATAL_ERROR "A instalacao comercial requer Qt 6.2+ com suporte a deploy.")
  endif()
else()
  message(STATUS "NODO_BUILD_GUI=OFF: gerando somente a ferramenta CLI (nao e pacote desktop comercial).")
endif()

# Pacotes redistribuiveis. O CI gera o formato nativo de cada sistema.
set(CPACK_PACKAGE_NAME "Nodo")
set(CPACK_PACKAGE_VENDOR "NODO")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "NODO - aplicativo desktop")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "NODO")
if(WIN32)
  set(CPACK_GENERATOR "ZIP")
elseif(APPLE)
  set(CPACK_GENERATOR "DragNDrop")
  set(CPACK_PACKAGE_FILE_NAME "Nodo-${PROJECT_VERSION}-mac-universal")
elseif(UNIX)
  set(CPACK_GENERATOR "DEB;TGZ")
  set(CPACK_DEBIAN_PACKAGE_MAINTAINER "NODO")
  set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
  set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
endif()
include(CPack)
