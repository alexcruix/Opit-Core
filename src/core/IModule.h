// SPDX-License-Identifier: PolyForm-Shield-1.0.0
// Required Notice: Copyright (C) 2026 ACSTCLab (alexcstos@gmail.com)

#pragma once
namespace opitcore {
class IModule {
public:
    virtual ~IModule() = default;
    virtual bool Initialize() = 0;
    virtual void Start() = 0;
    virtual void Stop() = 0;
    virtual void Shutdown() = 0;
    virtual const char* Name() const = 0;
};
}
