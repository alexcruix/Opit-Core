# Opit-Core — Guia de Compilação (Build)

## Requisitos
- **Sistema Operacional:** Windows 10 ou Windows 11 (x64 ou x86)
- **Compilador:** Visual Studio 2022 (MSVC) ou Build Tools for Visual Studio 2022 com suporte a C++17
- **CMake:** Versão 3.16 ou superior
- **Opcional (.NET / Sensores Super I/O):** Suporte a C++/CLI no Visual Studio e `LibreHardwareMonitorLib.dll` na raiz do projeto caso deseje compilar a ponte `opitcore_lhm_bridge.dll`. Sem ela, o Opit-Core compila normalmente usando provedores nativos (ACPI/WMI, NVIDIA NVAPI, AMD ADL, Intel).

## Compilação (64 bits — Recomendado)
```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release -j 8 -- /nodeReuse:false
```

## Compilação (32 bits / x86)
```powershell
cmake -S . -B build-x86 -G "Visual Studio 17 2022" -A Win32
cmake --build build-x86 --config Release -j 8 -- /nodeReuse:false
```

## Execução da Suíte de Testes (23 Suítes CTest)
```powershell
ctest --test-dir build -C Release --output-on-failure
```

## Executável Gerado
- **x64:** `build\Release\Opit-Core.exe`
- **x86:** `build-x86\Release\Opit-Core.exe`
