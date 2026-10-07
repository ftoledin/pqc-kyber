# Criptografia: critérios e resultados

[README](../README.md) · [Simulação ROS2](SIMULACAO.md)

Os PDFs orientam a criptografia; ROS2 e a comunicação sistema/drone são requisitos do projeto. O núcleo `hash_options/chaotic_hash.c` permanece igual ao original. A compatibilidade C++ e o buffer residual do XOF estão nos cabeçalhos/adaptadores, fora do núcleo matemático.

## Critérios e limites dos PDFs

Referências: [REF 13 Draft_Version.pdf](<../../REF 13 Draft_Version.pdf>), páginas 9–13, equações (10)–(13); [SBCCI2026_Angelo_final.pdf](<../../SBCCI2026_Angelo_final.pdf>), páginas 2–3.

| Critério | Implementação preservada |
| --- | --- |
| Entrada | Pares UINT32: blocos de oito bytes e padding original |
| Mapa e parâmetros | Fórmulas originais e ajuste de alpha por `Hsize/10000` |
| Iterações | `Hsize + 200` |
| Representação | Q10.24; folding com `0x00FFFFFFLL` |
| Extração | Dois valores de 16 bits por passo, conforme o código original |

A restauração não comprova equivalência integral aos artigos: o primeiro PDF apresenta ambiguidade entre `c` e `cdata`, e a extração em ponto flutuante difere da implementação fixa. O SBCCI não especifica toda a codificação e os arredondamentos. Não foram repetidas as baterias NIST SP 800-22, Dieharder, ENT nem a síntese HLS.

## As 65 falhas

O teste gera **um ciphertext por variante com sementes fixas**, altera um bit por caso e compara o segredo retornado com o original e com o segredo de rejeição. Foi executado diretamente em C, sem ROS2.

| Variante | Alterações | Falhas | Fração nessa amostra |
| --- | ---: | ---: | ---: |
| Kyber512 | 6.144 | 65 | 1,06% |
| Kyber768 | 8.704 | 65 | 0,75% |
| Kyber1024 | 12.544 | 65 | 0,52% |

Nos casos que falharam, o ciphertext alterado manteve o segredo original. As causas identificadas são:

- **64 casos:** a normalização Q24 descarta oito bits de cada palavra UINT32. Na mensagem interna de 32 bytes, isso corresponde a oito bytes × oito bits.
- **1 caso:** a diferença entra no estado, mas desaparece nas multiplicações truncadas. Em Kyber768, os estados reais `215678` e `215679` reconvergem no passo seguinte.

A normalização implementada corresponde à equação (10) truncada:

```text
floor(((d + 1) * 2^24) / (2^32 + 1)) = d >> 8
```

O hash de 64 bytes, o segredo e a aleatoriedade ficaram iguais; a recifragem reproduziu o ciphertext alterado e o comparador aceitou. A falha é a perda de informação no hashing, não um defeito no comparador. Mais iterações não recuperam bits perdidos.

**65 não é um limite universal.** O teste altera o ciphertext de uma encapsulação fixa; não testa textos ROS2. A mensagem interna do KEM vem da aleatoriedade da encapsulação, separadamente do texto cifrado por AES-GCM.

## Estimativa em novas sessões

Em 2026-10-06 foram testadas **10.000 sessões Kyber768**, com novas chaves e encapsulações por sessão. Posições foram sorteadas uniformemente; no cenário de byte, a máscara XOR foi sorteada entre 1 e 255. A seleção usou amostragem com rejeição para evitar viés de módulo.

| Cenário | Falhas / casos | Frequência | IC de 95% — Wilson |
| --- | ---: | ---: | ---: |
| Ciphertext válido: segredo incorreto | 0/10.000 | 0% observado | 0%–0,0384% |
| Um bit alterado: segredo original mantido | 71/10.000 | 0,71% | 0,5633%–0,8945% |
| Um byte alterado: segredo original mantido | 2/10.000 | 0,02% | 0,0055%–0,0729% |

São 10.000 observações por cenário, compartilhando as mesmas sessões; as taxas não são combinadas. Em ambos os cenários alterados, as contagens de segredo original mantido e saída diferente da rejeição coincidiram. [Método de Wilson — NIST](https://itl.nist.gov/div898/handbook/prc/section2/prc241.htm).

Essas estimativas valem para alterações aleatórias definidas acima, não para ataques deliberados, outras variantes ou perdas de rede. Zero falhas legítimas observadas não prova probabilidade zero; apenas dois casos no cenário de byte tornam essa estimativa exploratória. As falhas também ocorreram fora das sementes originais.

Os registros ficaram em `/tmp/kyber-global-estimate.c`, `.json` e `-replay.bin`; podem desaparecer com a limpeza do ambiente. O replay usa registros de 105 bytes: moedas de chave (64), moedas de encapsulação (32), índice de bit e posição de byte (UINT32 little-endian cada), máscara (1).

## Correções candidatas, não aplicadas

Primeiro, comparar o adaptador com a implementação realmente utilizada no estudo SBCCI. Depois, avaliar codificação que preserve todos os bits da entrada ou maior precisão nos produtos intermediários. Ambas exigem validação e podem mudar resultados; preservar os bits não garante eliminar a reconvergência. Aumentar os bits fracionários altera o critério Q10.24.

<a id="validacao"></a>

## Validação

| Verificação | Resultado registrado |
| --- | --- |
| Núcleo original, 134 comparações de hash/PRF/XOF, 17 cabeçalhos C++ nas três variantes | Passou |
| Vetores de hash, continuidade XOF e ASan/UBSan no adaptador | Passou |
| `test_kyber*` e `test_vectors*` nas três variantes | Passaram na revisão final; testes aleatórios de rejeição também falharam em execução anterior |
| Compilação ROS2, `cpp_kem_chaotic_check`, `chaotic_hash_regression`, `demo_encryption_authentication` | Passaram em Jazzy/Kyber768 |
| `run_demo.py` | Duas entregas, incluindo acentos, e tag adulterada rejeitada |
| `test_rejection*` / `ciphertext_rejection_regression` | Falharam conforme as contagens acima |

Referência original: commit `7506be86983094141ea2712caa0cca0265d64e10`. SHA-256 do núcleo usado na estimativa: `dad7ad4c58f52df6eb8f2304b888a17ffe791828ca4b4b57738b12449a45c841`.

Na raiz do repositório, execute `make -C ref check`. Para os testes ROS2, na pasta `ros2`, com os setups carregados:

```bash
colcon test --packages-select pqc_kyber
colcon test-result --verbose
```

A bateria continua retornando falha para rejeição; nenhum caso foi excluído. `kem_check` verifica apenas um caso de adulteração. A inspeção inicial sem ROS2 é histórica: a instalação foi concluída antes da validação da simulação.

O pacote ROS2 também autentica o ciphertext Kyber como dado associado do AES-GCM. Essa camada adicional não corrige o KEM. `tamper:=true` testa a tag GCM, não as 65 falhas. As colisões permanecem uma limitação criptográfica e não devem ser ignoradas ao avaliar segurança.
