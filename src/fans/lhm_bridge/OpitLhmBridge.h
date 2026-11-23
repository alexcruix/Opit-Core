// SPDX-License-Identifier: PolyForm-Shield-1.0.0
// Required Notice: Copyright (C) 2026 ACSTCLab (alexcstos@gmail.com)

#pragma once

// Native C ABI exposed by opitcore_lhm_bridge.dll (C++/CLI wrapper around
// LibreHardwareMonitorLib). No managed types cross this boundary.
// The native core loads the DLL at runtime (LoadLibraryW + GetProcAddress),
// so Opit-Core never depends on the CLR at startup.
//
// Conventions:
//   - All functions are extern "C", __cdecl and never let exceptions escape.
//   - int32_t return: 1 = success, 0 = failure / unavailable.

#include <stdint.h>

#define OPIT_LHM_ABI_VERSION 1
#define OPIT_LHM_BRIDGE_DLL_NAME L"opitcore_lhm_bridge.dll"

#ifdef OPIT_LHM_BRIDGE_BUILD
#define OPIT_LHM_API __declspec(dllexport)
#else
#define OPIT_LHM_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Returns OPIT_LHM_ABI_VERSION of the compiled bridge.
OPIT_LHM_API int32_t __cdecl OpitLhm_GetAbiVersion(void);

// 1 se o LibreHardwareMonitorLib pode ser carregado neste processo, 0 caso contrario
// (per ex. assembly Amd64 num processo de 32 bits, ou .NET ausente).
// Chamar ANTES de qualquer outra funcao abaixo.
OPIT_LHM_API int32_t __cdecl OpitLhm_IsLibraryLoadable(void);

// Opens a LibreHardwareMonitor Computer (CPU, GPU, motherboard, storage).
OPIT_LHM_API int32_t __cdecl OpitLhm_Init(void);

// Updates all hardware and sub-hardware sensors.
OPIT_LHM_API int32_t __cdecl OpitLhm_Refresh(void);

// Number of temperature / fan sensors; -1 if not initialized.
OPIT_LHM_API int32_t __cdecl OpitLhm_GetTemperatureCount(void);
OPIT_LHM_API int32_t __cdecl OpitLhm_GetFanCount(void);

// Reads a temperature (Celsius) / fan speed (RPM) by zero-based index.
// Returns 0 if the index is invalid or the sensor has no value.
OPIT_LHM_API int32_t __cdecl OpitLhm_GetTemperature(int32_t index, double* outCelsius);
OPIT_LHM_API int32_t __cdecl OpitLhm_GetRpm(int32_t index, int32_t* outRpm);

// Reads detailed temperature / fan sensor info by zero-based index.
// nameBuf receives "HardwareName - SensorName" (UTF-8, null-terminated).
OPIT_LHM_API int32_t __cdecl OpitLhm_GetTemperatureInfo(int32_t index, double* outCelsius, char* nameBuf, int32_t nameBufLen);
OPIT_LHM_API int32_t __cdecl OpitLhm_GetFanInfo(int32_t index, int32_t* outRpm, char* nameBuf, int32_t nameBufLen);

// Controls fan speed / duty cycle (0.0 to 100.0%) or restores default firmware control.
OPIT_LHM_API int32_t __cdecl OpitLhm_SetFanControl(int32_t fanIndex, double speedPercent);
OPIT_LHM_API int32_t __cdecl OpitLhm_RestoreFanControl(int32_t fanIndex);

// Closes the Computer and releases resources.
OPIT_LHM_API void __cdecl OpitLhm_Shutdown(void);

// Function pointer types for runtime loading.
typedef int32_t(__cdecl* PFN_OpitLhm_GetAbiVersion)(void);
typedef int32_t(__cdecl* PFN_OpitLhm_IsLibraryLoadable)(void);
typedef int32_t(__cdecl* PFN_OpitLhm_Init)(void);
typedef int32_t(__cdecl* PFN_OpitLhm_Refresh)(void);
typedef int32_t(__cdecl* PFN_OpitLhm_GetCount)(void);
typedef int32_t(__cdecl* PFN_OpitLhm_GetTemperature)(int32_t, double*);
typedef int32_t(__cdecl* PFN_OpitLhm_GetRpm)(int32_t, int32_t*);
typedef int32_t(__cdecl* PFN_OpitLhm_GetTemperatureInfo)(int32_t, double*, char*, int32_t);
typedef int32_t(__cdecl* PFN_OpitLhm_GetFanInfo)(int32_t, int32_t*, char*, int32_t);
typedef int32_t(__cdecl* PFN_OpitLhm_SetFanControl)(int32_t, double);
typedef int32_t(__cdecl* PFN_OpitLhm_RestoreFanControl)(int32_t);
typedef void(__cdecl* PFN_OpitLhm_Shutdown)(void);

#ifdef __cplusplus
}
#endif
