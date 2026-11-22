#pragma once

#include <string>
#include <vector>
#include <windows.h>
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
    HMODULE VerifyAndLoad(const std::wstring& moduleFullPath,
                          const std::string& expectedSha256,
                          IntegrityStatus& outStatus);

    /**
     * Enumera e audita módulos já carregados no espaço de endereçamento do processo.
     */
    std::vector<std::string> ListLoadedModuleNames();
};

} // namespace security
} // namespace opitcore
