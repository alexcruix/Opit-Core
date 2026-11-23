# Política de Propriedade Intelectual — ACSTC Lab / Opit-Core

**Titular:** ACSTC Lab (`alexcstos@gmail.com`)  
**Autor:** Alex de Campos Santos Toledo Camilo  
**Repositório Oficial:** https://github.com/alexcruix/Opit-Core  

---

## 1. Modelo de Licenciamento (Source-Available / Exclusividade Comercial)

O **Opit-Core** adota a **[PolyForm Shield License 1.0.0](LICENSE)**, uma licença de código-fonte disponível (*source-available*) com cláusula de não concorrência (*Noncompete*):

1. **Uso gratuito para usuários e empresas:** Qualquer pessoa física ou jurídica pode usar, auditar e modificar o Opit-Core gratuitamente para uso próprio ou interno.
2. **Proibição de exploração concorrente por terceiros:** É proibido a terceiros usar este código (ou derivados) para vender, cobrar assinatura ou distribuir qualquer produto ou serviço concorrente com o Opit-Core ou com a linha de negócios da ACSTC Lab declarada em [`NOTICE`](NOTICE) — independentemente da linguagem de programação, interface, arquitetura ou sistema operacional (Seção *Competition* da licença).
3. **Exclusividade de comercialização:** Como única titular dos direitos autorais, **somente a ACSTC Lab** pode comercializar o Opit-Core, lançar versões pagas/por assinatura ou conceder licenças comerciais separadas.

## 2. Política de Contribuições Externas

Para preservar a integridade da cadeia de direitos e a exclusividade comercial da ACSTC Lab, nenhuma contribuição externa (*Pull Request*) é incorporada sem o aceite expresso do Acordo de Licença de Contribuidor (CLA) descrito em [`CONTRIBUTING.md`](CONTRIBUTING.md).

## 3. Política de Marcas

As regras sobre os nomes **OPIT**, **Opit-Core**, **Opit-AIEngine**, **Opit-Graphics** / **Opit-Grafic**, **Opit-Sound**, **ACSTC Lab** e sobre os logotipos do projeto constam em [`TRADEMARKS.md`](TRADEMARKS.md). A licença de código não concede direitos sobre marcas.

## 4. Isenção de Garantia e Limitação de Responsabilidade

Complementando a seção **No Liability** da PolyForm Shield 1.0.0 e **na extensão máxima permitida pela legislação aplicável**:

1. O software é fornecido gratuitamente, **"no estado em que se encontra" (*AS IS*)**, sem garantias de qualquer tipo. O usuário assume integralmente o risco do uso no próprio computador.
2. Nem o autor nem a **ACSTC Lab** respondem por danos decorrentes do uso ou da impossibilidade de uso do programa, incluindo:
   - superaquecimento, queima, desgaste ou dano físico, elétrico ou mecânico de componentes (CPU, GPU, placa-mãe, memória RAM, SSD/HD, fonte, bateria, ventoinhas ou water cooler);
   - problemas decorrentes de curvas de ventoinha ou perfis configurados pelo próprio usuário, falhas mecânicas de refrigeração, leituras imprecisas de sensores, defeitos preexistentes do equipamento ou uso combinado com ferramentas de overclock/undervolt de terceiros;
   - telas azuis (BSOD), travamentos, reinicializações, conflitos de drivers, falhas de serviços, corrupção do sistema operacional, perda de arquivos/dados ou lucros cessantes.
3. O usuário é o único responsável por manter cópias de segurança (*backups*) de seus dados.
4. No direito brasileiro, em contrato gratuito, quem concede o benefício responde apenas por dolo (Código Civil, art. 392), respeitadas as normas de ordem pública quando aplicáveis.

## 5. Autenticidade e Verificação de Releases Oficiais

Cada lançamento oficial da ACSTC Lab é acompanhado de:
- manifesto criptográfico assinado (`opit_manifest.json` + `opit_manifest.sig`) verificado em tempo de execução pela chave pública RSA-2048 oficial embutida em `src/security/ManifestVerifier.cpp`;
- arquivo `SHA256SUMS.txt` contendo os hashes criptográficos de verificação dos pacotes publicados;
- registro público no histórico de versões em [`VERSION_HISTORY.md`](VERSION_HISTORY.md).

## 6. Contato para Licenciamento Comercial

Empresas interessadas em licenciamento comercial, distribuição autorizada ou uso fora dos termos gratuitos da PolyForm Shield 1.0.0 devem entrar em contato pelo e-mail: **`alexcstos@gmail.com`**.
