# Como Contribuir com o Opit-Core

Obrigada pelo interesse! Antes de abrir um *Pull Request*, leia as duas seções abaixo.

## 1. Regras de engenharia

1. **Números e medições, não adjetivos.** PRs que dizem "melhora a velocidade do PC" sem medição não são aceitos. Documente o estado medido antes e depois, com a mesma métrica.
2. **Prova de reversão obrigatória.** Toda rotina que altere uma configuração do Windows deve trazer a lógica de reversão exata e um teste correspondente em `tests/`.
3. **Ausência declarada.** Se um sensor ou métrica não estiver disponível em determinado hardware, o código deve informar *não medido / não suportado*. Nunca invente nem estime números.

## 2. Acordo de Licença de Contribuidor (CLA)

O Opit-Core é distribuído sob a **PolyForm Shield License 1.0.0** (*source-available*), e a **ACSTCLab** detém o direito exclusivo de comercialização e licenciamento comercial do projeto. Por isso, **toda contribuição externa precisa do aceite expresso deste acordo**.

Ao enviar uma contribuição e declarar o aceite, você afirma que:

1. A contribuição é obra sua, ou você tem o direito de enviá-la, e ela não viola direitos de terceiros.
2. Você mantém os seus direitos autorais sobre a contribuição.
3. Você concede à **ACSTCLab** uma licença **perpétua, mundial, não exclusiva, gratuita e irrevogável** para usar, reproduzir, modificar, distribuir, sublicenciar e **relicenciar a contribuição sob quaisquer termos**, inclusive sob a PolyForm Shield 1.0.0 ou sob licenças comerciais/proprietárias exclusivas da ACSTCLab.
4. Você concede também uma licença de patentes perpétua, mundial e livre de royalties sobre quaisquer reivindicações de patente necessariamente infringidas pela sua contribuição.

**Como aceitar:** no seu Pull Request, escreva o comentário abaixo, exatamente assim:

> Li o CLA em CONTRIBUTING.md e concordo com os seus termos para esta e para as minhas futuras contribuições ao Opit-Core.

Pull Requests sem esse aceite não serão incorporados.
