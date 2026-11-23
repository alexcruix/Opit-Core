// SPDX-License-Identifier: LicenseRef-PolyForm-Strict-1.0.0
// Copyright (C) 2026 ACSTC Lab - Alex de Campos Santos Toledo Camilo (alexcstos@gmail.com)

#pragma once

#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif
#include "SecurityTypes.h"

namespace opitcore {
namespace security {

class ModuleVerifier {
public:
    ModuleVerifier() = default;
    ~ModuleVerifier() = default;

    /**
     * Configura o processo para carregar DLLs estritamente do diretório da aplicação e do System32.
     * Elimina o diretório de trabalho atual (CWD) do caminho de busca.
     */
    static bool EnforceSecureDllSearchOrder();

    /**
     * Verifica o hash de uma DLL antes de realizar o carregamento nativo.
     * Se válida, carrega via LoadLibraryExW com flags seguras.
     */
#ifdef _WIN32
    HMODULE VerifyAndLoad(const std::wstring& moduleFullPath,
                          const std::string& expectedSha256,
                          IntegrityStatus& outStatus);
#else
    // Em Linux o modulo e um .so e o handle e o ponteiro devolvido por dlopen(). A condicao de
    // carregamento e a mesma: hash provado primeiro, caminho absoluto, nada entra sem prova.
    void* VerifyAndLoad(const std::wstring& moduleFullPath,
                        const std::string& expectedSha256,
                        IntegrityStatus& outStatus);
#endif

    /**
     * Enumera e audita módulos já carregados no espaço de endereçamento do processo.
     */
    std::vector<std::string> ListLoadedModuleNames();
};

} // namespace security
} // namespace opitcore
