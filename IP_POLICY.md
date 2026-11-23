# Política de Propriedade Intelectual — ACSTC Lab / Opit-Core

**Titular:** ACSTC Lab (`alexcstos@gmail.com`)  
**Autor:** Alex de Campos Santos Toledo Camilo  
**Repositório Oficial:** https://github.com/alexcruix/Opit-Core  

---

## 1. Modelo de Licenciamento (Source-Available / PolyForm Strict 1.0.0)

O **Opit-Core** adota a **[PolyForm Strict License 1.0.0](LICENSE)** (`https://polyformproject.org/licenses/strict/1.0.0`), uma licença restritiva de código-fonte disponível (*source-available*):

1. **Uso gratuito não comercial:** Permitido exclusivamente para fins não comerciais (uso pessoal, estudo, pesquisa, testes, entretenimento privado, hobbies e instituições sem fins lucrativos, educacionais, de pesquisa pública, saúde/segurança pública ou governamentais).
2. **Proibição de uso comercial, redistribuição e derivação por terceiros:** A licença **não autoriza** terceiros a:
   - usar o software para qualquer finalidade comercial (nem mesmo uso comercial interno em empresas sem licença comercial da ACSTC Lab);
   - distribuir ou redistribuir cópias do software (gratuitas ou pagas);
   - criar versões modificadas ou obras derivadas baseadas neste código.
3. **Exclusividade absoluta da ACSTC Lab:** Como única titular dos direitos autorais, **somente a ACSTC Lab** pode comercializar, distribuir oficialmente, lançar edições pagas/por assinatura ou conceder licenças comerciais separadas.

## 2. Política de Contribuições Externas

Para preservar a integridade da cadeia de direitos e a exclusividade comercial da ACSTC Lab, nenhuma contribuição externa (*Pull Request*) é incorporada sem o aceite expresso do Acordo de Licença de Contribuidor (CLA) descrito em [`CONTRIBUTING.md`](CONTRIBUTING.md).

## 3. Política de Marcas

As regras sobre os nomes **OPIT**, **Opit-Core**, **Opit-AIEngine**, **Opit-Graphics** / **Opit-Grafic**, **Opit-Sound**, **ACSTC Lab** e sobre os logotipos do projeto constam em [`TRADEMARKS.md`](TRADEMARKS.md). A licença de código não concede direitos sobre marcas (Seção *No Other Rights*).

## 4. Isenção de Garantia e Limitação de Responsabilidade

Complementando a seção **No Liability** da PolyForm Strict 1.0.0 e **na extensão máxima permitida pela legislação aplicável**:

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

Empresas ou interessados em licenciamento comercial, distribuição autorizada ou uso fora dos termos gratuitos da PolyForm Strict 1.0.0 devem entrar em contato pelo e-mail: **`alexcstos@gmail.com`**.
