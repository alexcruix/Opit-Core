// SPDX-License-Identifier: PolyForm-Shield-1.0.0
// Required Notice: Copyright (C) 2026 ACSTCLab (alexcstos@gmail.com)

// C++/CLI bridge between the native Opit-Core and LibreHardwareMonitorLib.
// Compiled with /clr (see lhm_bridge/CMakeLists.txt). Only this target uses .NET.
// The managed logic was moved from src/fans/LibreHardwareMonitorCLI.cpp; sensor
// enumeration and indexing are unchanged. Every exported function catches
// managed exceptions so none crosses the C ABI.

#include "OpitLhmBridge.h"

using namespace System;
using namespace System::Text;
using namespace System::Threading;
using namespace LibreHardwareMonitor::Hardware;

namespace opitcore_lhm_detail {

// temporario: capturar excecao gerenciada nao tratada antes do fail-fast do CLR — remover apos diagnostico
ref class LhmCrashWatch abstract sealed {
public:
    static bool Registered = false;
    static void Init() {
        if (Registered) return;
        Registered = true;
        AppDomain::CurrentDomain->UnhandledException += gcnew UnhandledExceptionEventHandler(OnUnhandled);
    }
    static void OnUnhandled(Object^, UnhandledExceptionEventArgs^ e) {
        try {
            Exception^ ex = dynamic_cast<Exception^>(e->ExceptionObject);
            String^ msg = (ex != nullptr)
                ? (ex->GetType()->FullName + ": " + ex->Message + "\n" + ex->StackTrace)
                : e->ExceptionObject->ToString();
            String^ path = Environment::GetEnvironmentVariable("LOCALAPPDATA") +
                           "\\OpitCore\\opit_crash.log";
            System::IO::File::AppendAllText(path,
                DateTime::Now.ToString("yyyy-MM-dd HH:mm:ss.fff") +
                " MANAGED-UNHANDLED isTerminating=" + e->IsTerminating + " thread=" +
                Thread::CurrentThread->ManagedThreadId + " " + msg + "\n",
                System::Text::Encoding::Unicode);
        } catch (...) {
        }
    }
};

// Managed globals are not allowed at namespace scope; keep them in a ref class.
ref class LhmState abstract sealed {
public:
    static Object^ Lock = gcnew Object();
    static Computer^ TheComputer = nullptr;
    static array<IHardware^>^ Hardware = nullptr;
    static Int64 LastRefreshTickMs = 0;
};

int32_t CountSensorsInHardware(IHardware^ hw, SensorType type) {
    if (hw == nullptr) return 0;
    int32_t count = 0;
    for each (ISensor^ s in hw->Sensors) {
        if (s->SensorType == type) ++count;
    }
    for each (IHardware^ sub in hw->SubHardware) {
        count += CountSensorsInHardware(sub, type);
    }
    return count;
}

int32_t CountSensors(SensorType type) {
    if (LhmState::Hardware == nullptr) return -1;
    int32_t count = 0;
    for each (IHardware^ hw in LhmState::Hardware) {
        count += CountSensorsInHardware(hw, type);
    }
    return count;
}

ISensor^ FindSensorInHardware(IHardware^ hw, SensorType type, int32_t targetIndex, int32_t% curIndex, IHardware^% outOwner) {
    if (hw == nullptr) return nullptr;
    for each (ISensor^ s in hw->Sensors) {
        if (s->SensorType == type) {
            if (curIndex == targetIndex) {
                outOwner = hw;
                return s;
            }
            ++curIndex;
        }
    }
    for each (IHardware^ sub in hw->SubHardware) {
        ISensor^ found = FindSensorInHardware(sub, type, targetIndex, curIndex, outOwner);
        if (found != nullptr) return found;
    }
    return nullptr;
}

// Returns the sensor at the given zero-based index among sensors of `type`.
ISensor^ FindSensor(SensorType type, int32_t index, IHardware^% outOwner) {
    outOwner = nullptr;
    if (LhmState::Hardware == nullptr || index < 0) return nullptr;
    int32_t cur = 0;
    for each (IHardware^ hw in LhmState::Hardware) {
        ISensor^ found = FindSensorInHardware(hw, type, index, cur, outOwner);
        if (found != nullptr) return found;
    }
    return nullptr;
}

ISensor^ FindControlForFan(int32_t fanIndex) {
    IHardware^ owner = nullptr;
    ISensor^ fanSensor = FindSensor(SensorType::Fan, fanIndex, owner);
    if (fanSensor == nullptr || owner == nullptr) return nullptr;

    if (fanSensor->Control != nullptr) {
        return fanSensor;
    }

    for each (ISensor^ s in owner->Sensors) {
        if (s->SensorType == SensorType::Control && s->Control != nullptr) {
            if (s->Index == fanSensor->Index || s->Name->Contains(fanSensor->Name) || fanSensor->Name->Contains(s->Name)) {
                return s;
            }
        }
    }

    for each (ISensor^ s in owner->Sensors) {
        if (s->SensorType == SensorType::Control && s->Control != nullptr) {
            return s;
        }
    }

    return nullptr;
}

void CopyManagedStringToUtf8(String^ str, char* buf, int32_t bufLen) {
    if (!buf || bufLen <= 0) return;
    buf[0] = '\0';
    if (String::IsNullOrEmpty(str)) return;
    array<Byte>^ bytes = Encoding::UTF8->GetBytes(str);
    int32_t copyLen = bytes->Length;
    if (copyLen >= bufLen) copyLen = bufLen - 1;
    for (int32_t i = 0; i < copyLen; ++i) {
        buf[i] = static_cast<char>(bytes[i]);
    }
    buf[copyLen] = '\0';
}

} // namespace opitcore_lhm_detail

using namespace opitcore_lhm_detail;

extern "C" {

int32_t __cdecl OpitLhm_GetAbiVersion(void) {
    return OPIT_LHM_ABI_VERSION;
}

int32_t __cdecl OpitLhm_IsLibraryLoadable(void) {
    try {
        array<System::Reflection::AssemblyName^>^ refs =
            System::Reflection::Assembly::GetExecutingAssembly()->GetReferencedAssemblies();
        for each (System::Reflection::AssemblyName^ rn in refs) {
            if (rn->Name != "LibreHardwareMonitorLib") continue;
            System::Reflection::Assembly^ loaded = System::Reflection::Assembly::Load(rn);
            return loaded != nullptr ? 1 : 0;
        }
        return 0;
    } catch (Exception^) {
        return 0;
    }
}

int32_t __cdecl OpitLhm_Init(void) {
    try {
        Monitor::Enter(LhmState::Lock);
        try {
            LhmCrashWatch::Init(); // temporario: registro do capturador gerenciado
            if (LhmState::TheComputer != nullptr) return 1;
            Computer^ computer = gcnew Computer();
            computer->IsCpuEnabled = true;
            computer->IsGpuEnabled = true;
            // Ativa Super I/O da placa-mãe (Nuvoton/ITE/Fintek) e controladores de AIO/fans (Corsair/NZXT)
            // para permitir a descoberta e controle de todas as ventoinhas do hardware
            computer->IsMotherboardEnabled = true;
            computer->IsControllerEnabled = true;
            computer->IsStorageEnabled = false;
            computer->Open();
            LhmState::Hardware = System::Linq::Enumerable::ToArray(computer->Hardware);
            LhmState::TheComputer = computer;
            LhmState::LastRefreshTickMs = DateTime::UtcNow.Ticks / 10000LL;
            return 1;
        } finally {
            Monitor::Exit(LhmState::Lock);
        }
    } catch (Exception^) {
        return 0;
    }
}

int32_t __cdecl OpitLhm_Refresh(void) {
    try {
        Monitor::Enter(LhmState::Lock);
        try {
            if (LhmState::Hardware == nullptr) return 0;
            Int64 nowTick = DateTime::UtcNow.Ticks / 10000LL;
            // Limita polling de registradores de baixo nivel (MSR/SMU/PCI) a no maximo 1 vez a cada 3 segundos
            if (nowTick - LhmState::LastRefreshTickMs < 3000) {
                return 1;
            }
            LhmState::LastRefreshTickMs = nowTick;
            for each (IHardware^ hw in LhmState::Hardware) {
                hw->Update();
                for each (IHardware^ sub in hw->SubHardware) {
                    sub->Update();
                }
            }
            return 1;
        } finally {
            Monitor::Exit(LhmState::Lock);
        }
    } catch (Exception^) {
        return 0;
    }
}

int32_t __cdecl OpitLhm_GetTemperatureCount(void) {
    try {
        Monitor::Enter(LhmState::Lock);
        try {
            return CountSensors(SensorType::Temperature);
        } finally {
            Monitor::Exit(LhmState::Lock);
        }
    } catch (Exception^) {
        return -1;
    }
}

int32_t __cdecl OpitLhm_GetFanCount(void) {
    try {
        Monitor::Enter(LhmState::Lock);
        try {
            return CountSensors(SensorType::Fan);
        } finally {
            Monitor::Exit(LhmState::Lock);
        }
    } catch (Exception^) {
        return -1;
    }
}

int32_t __cdecl OpitLhm_GetTemperature(int32_t index, double* outCelsius) {
    return OpitLhm_GetTemperatureInfo(index, outCelsius, nullptr, 0);
}

int32_t __cdecl OpitLhm_GetRpm(int32_t index, int32_t* outRpm) {
    return OpitLhm_GetFanInfo(index, outRpm, nullptr, 0);
}

int32_t __cdecl OpitLhm_GetTemperatureInfo(int32_t index, double* outCelsius, char* nameBuf, int32_t nameBufLen) {
    if (!outCelsius) return 0;
    try {
        Monitor::Enter(LhmState::Lock);
        try {
            IHardware^ owner = nullptr;
            ISensor^ s = FindSensor(SensorType::Temperature, index, owner);
            if (s == nullptr || !s->Value.HasValue) return 0;
            *outCelsius = static_cast<double>(s->Value.Value);
            if (nameBuf && nameBufLen > 0) {
                String^ label = (owner != nullptr ? (owner->Name + " - " + s->Name) : s->Name);
                CopyManagedStringToUtf8(label, nameBuf, nameBufLen);
            }
            return 1;
        } finally {
            Monitor::Exit(LhmState::Lock);
        }
    } catch (Exception^) {
        return 0;
    }
}

int32_t __cdecl OpitLhm_GetFanInfo(int32_t index, int32_t* outRpm, char* nameBuf, int32_t nameBufLen) {
    if (!outRpm) return 0;
    try {
        Monitor::Enter(LhmState::Lock);
        try {
            IHardware^ owner = nullptr;
            ISensor^ s = FindSensor(SensorType::Fan, index, owner);
            if (s == nullptr || !s->Value.HasValue) return 0;
            *outRpm = static_cast<int32_t>(s->Value.Value);
            if (nameBuf && nameBufLen > 0) {
                String^ label = (owner != nullptr ? (owner->Name + " - " + s->Name) : s->Name);
                CopyManagedStringToUtf8(label, nameBuf, nameBufLen);
            }
            return 1;
        } finally {
            Monitor::Exit(LhmState::Lock);
        }
    } catch (Exception^) {
        return 0;
    }
}

int32_t __cdecl OpitLhm_SetFanControl(int32_t fanIndex, double speedPercent) {
    try {
        Monitor::Enter(LhmState::Lock);
        try {
            ISensor^ ctlSensor = FindControlForFan(fanIndex);
            if (ctlSensor != nullptr && ctlSensor->Control != nullptr) {
                float val = static_cast<float>(speedPercent);
                if (val < 0.0f) val = 0.0f;
                if (val > 100.0f) val = 100.0f;
                ctlSensor->Control->SetSoftware(val);
                return 1;
            }
            return 0;
        } finally {
            Monitor::Exit(LhmState::Lock);
        }
    } catch (Exception^) {
        return 0;
    }
}

int32_t __cdecl OpitLhm_RestoreFanControl(int32_t fanIndex) {
    try {
        Monitor::Enter(LhmState::Lock);
        try {
            ISensor^ ctlSensor = FindControlForFan(fanIndex);
            if (ctlSensor != nullptr && ctlSensor->Control != nullptr) {
                ctlSensor->Control->SetDefault();
                return 1;
            }
            return 0;
        } finally {
            Monitor::Exit(LhmState::Lock);
        }
    } catch (Exception^) {
        return 0;
    }
}

void __cdecl OpitLhm_Shutdown(void) {
    try {
        Monitor::Enter(LhmState::Lock);
        try {
            if (LhmState::TheComputer != nullptr) {
                LhmState::TheComputer->Close();
                LhmState::TheComputer = nullptr;
            }
            LhmState::Hardware = nullptr;
        } finally {
            Monitor::Exit(LhmState::Lock);
        }
    } catch (Exception^) {
        // Never propagate managed exceptions across the C ABI.
    }
}

} // extern "C"
