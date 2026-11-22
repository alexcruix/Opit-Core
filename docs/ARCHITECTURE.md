# Opit-Core - Arquitetura

## Visao Geral
Opit-Core e um orquestrador adaptativo de hardware, desempenho e consumo energetico para Windows 10/11 x64.

## Principios
- Observacao -> Decisao -> Execucao (separados)
- Deterministico com histerese e estabilizacao
- Somente ajustes permitidos/suportados
- Restauracao e recuperacao confiaveis
- Sem funcionalidades do Opit-Graphic ou Opit-AI Engine
- Baixo consumo, seguro e transparente

## Modularizacao
Ver estrutura em src/ conforme CMakeLists.
