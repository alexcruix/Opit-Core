// SPDX-License-Identifier: PolyForm-Shield-1.0.0
// Required Notice: Copyright (C) 2026 ACSTCLab (alexcstos@gmail.com)

#pragma once
#include <string>
#include <vector>

namespace opitcore {

struct HardwareSpecItem {
    const char* category;
    const char* family;
    const char* model;
    const char* coresThreads;
    const char* clockBase;
    const char* clockBoost;
    const char* tdp;
    const char* socketOrBus;
    const char* details;
};

class HardwareKnowledgeBase {
public:
    static const std::vector<HardwareSpecItem>& GetDocumentedSpecs();
    // Incrementar a cada expansao da base: existindo kb_meta.seed_version maior no banco,
    // SeedHardwareKnowledgeBase insere apenas os modelos ausentes (sem duplicar os antigos).
    static int GetSeedVersion() { return 2; }
};

} // namespace opitcore
