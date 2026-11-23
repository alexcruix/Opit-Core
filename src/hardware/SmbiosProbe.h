// SPDX-License-Identifier: PolyForm-Shield-1.0.0
// Required Notice: Copyright (C) 2026 ACSTCLab (alexcstos@gmail.com)

#pragma once
#include "utils/Types.h"
#include <vector>

namespace opitcore {

struct SmbiosSnapshot {
    bool ok = false;
    BiosInfo bios;
    MotherboardDetails board;
    std::vector<MemoryStickInfo> sticks;
};

// Le a tabela SMBIOS do firmware (GetSystemFirmwareTable 'RSMB') e extrai:
// BIOS (Type 0), sistema (Type 1), placa-mae (Type 2), chassi (Type 3) e
// pentes de memoria (Type 17). Numeros de serie NUNCA sao lidos — sao
// identificadores de maquina, e o programa nao os exibe nem os transmite.
class SmbiosProbe {
public:
    static SmbiosSnapshot Probe();
};

} // namespace opitcore
