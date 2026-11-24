# Criptografia: critérios e resultados

[README](../README.md) · [Simulação ROS2](SIMULACAO.md)

Os PDFs orientam a criptografia; ROS2 e a comunicação sistema/drone são requisitos do projeto. Desde 20/10/2026, o núcleo usa **Q32 experimental por padrão**; a opção de compilação `CHAOTIC_FRAC_BITS=24` mantém Q24 para comparação. A compatibilidade C++ e o buffer residual do XOF estão nos cabeçalhos/adaptadores, fora do núcleo matemático.

## Atualização do núcleo GitHub — 17/11/2026

Base: [marcospgr/pqc-kyber, commit fe63e12a04f6f70b4e01e9b52ae15b32b151ceda](https://github.com/marcospgr/pqc-kyber/commit/fe63e12a04f6f70b4e01e9b52ae15b32b151ceda), publicado em 03/11/2026. O upstream acrescentou `chaotic_hash_32.c`, mas seu Makefile ainda seleciona `chaotic_hash.c` Q24. Incorporamos a lógica Q32.32 nova ao núcleo ativo local, mantendo a integração ROS2 e o CHF1.

O estado recebe xorshift 13/17/5 após cada passo de absorção, iteração e extração. A extração agora usa os bits 8–23, exatamente como no arquivo Q32.32 do upstream. As operações de mistura usam UINT32 para reproduzir o resultado sem deslocamentos assinados. Q24 permanece apenas como comparação histórica, sem esse misturador.

O código local não é uma cópia binariamente compatível do upstream bruto: o formato CHF1 continua diferenciando comprimento e finalidade. O upstream ainda produz hashes iguais para `41` e `4100`; o teste comparativo registra essa observação. A codificação CHF1 foi fornecida como entrada ao núcleo bruto para verificar a equivalência do processamento: **924 absorções e 2772 extrações coincidiram em estado e saída**.

| Verificação | Resultado |
| --- | --- |
| Bateria principal C | 10/10 aprovados, sem retirar casos |
| Rejeição determinística 512/768/1024 | 0/6144, 0/8704, 0/12544 falhas |
| Colisões de mesmo comprimento, hash256 / hash512 | 0/3584 em cada tamanho |
| Extensão por zero / antigo sentinela | 0/64 e 0/8 em cada tamanho |
| Referência Python independente | 136 comparações Q32 e 136 Q24 aprovadas |
| Pacote completo da demo | 29696 alterações de um bit rejeitadas nas três variantes |
| AES-GCM por outra API | 72 verificações aprovadas |
| CTest do pacote ROS2 | 5/5 aprovados |
| Dois processos ROS2 reais | duas entregas (incluindo acentos) e tag adulterada rejeitada |
| ASan, UBSan e float-cast-overflow | sem diagnósticos; detecção de vazamentos desativada |
| Cabeçalhos C++17 | 17 cabeçalhos nas três variantes e duas precisões |
| Gerador NIST KAT interno | 100 casos por variante; não é comparação com vetores oficiais |

Nova execução dos mesmos sorteios anteriores, 50000 sessões por variante, com novas chaves e encapsulações por sessão: **zero erros de recuperação e zero falhas observadas em um bit, um byte XOR, dois bits distintos e ciphertext totalmente aleatório**. São 150000 sessões e 750000 desencapsulações. Os zeros não demonstram probabilidade zero, resistência a ataques dirigidos ou certificação criptográfica. Identidade de participantes e proteção contra replay continuam pendentes na aplicação.

A demo passa a usar **pacote versão 3**, rejeitando versões 1 e 2. Recompile todos os participantes e gere novas chaves. [Resultados consolidados e hashes dos fontes](../ref/test/results/upstream_20261117.json).

Para comparar com um checkout do upstream, além de `make -C ref check`:

```bash
python3 ref/test/compare_upstream_hash.py --library ref/lib/libpqcrystals_kyber768_ref.so --upstream-source /caminho/do/checkout/hash_options/chaotic_hash_32.c --output /tmp/kyber-upstream-comparison.json
```

O teste compila o arquivo fornecido em pasta temporária, sem alterá-lo, e usa entradas CHF1 montadas independentemente do leitor C local.

**As seções seguintes documentam a versão anterior, sem xorshift; não descrevem o estado atual.**

## Atualização básica CHF1 — 03/11/2026

Cada absorção agora usa `"CHF" || versão 1 || domínio32le || dados || zeros até múltiplo de 8 || comprimento64le`. O comprimento fecha o bloco para distinguir zeros finais e o antigo sentinela de palavra ausente. O leitor monta palavras virtuais sem alocação proporcional à entrada. Há domínios distintos para hash256, hash512, matriz/XOF, PRF, chave de rejeição e ciphertext de rejeição. Chamadas de extração XOF não absorvem novos cabeçalhos.

É uma alteração experimental da codificação em relação aos PDFs. Foram mantidos Q32, equações do mapa, truncamentos, parâmetros e extração; **nenhum xorshift foi adicionado**. Não se afirma que a construção é criptograficamente segura. Hashes, chaves e ciphertexts antigos são incompatíveis: recompile ambos os participantes, gere novas chaves e use a demo versão 2.

- Preenchimento: zero colisões nos 64 pares de extensão por zero e 8 pares por `01`, em cada tamanho de hash. Comprimentos vazios e domínios passaram na regressão C.
- Reconvergência: 21/3584 colisões de mesmo comprimento no hash256 e 13/3584 no hash512. O teste continua reprovando, sem excluir os casos.
- Bateria C: 9/10 aprovados; os três testes determinísticos de rejeição passaram para as sementes fixas. Isso não substitui a amostragem abaixo.
- Referência Python independente: 136 comparações por precisão (Q24 e Q32) passaram. Os vetores esperados foram atualizados a partir dessa referência.
- Pacote completo: 29696 alterações de um bit rejeitadas nas três variantes; 72 verificações AES-GCM com a API Python passaram. ASan/UBSan/float-cast-overflow sem diagnósticos nas entradas executadas, com detecção de vazamentos desativada.
- CTest ROS2: 4/5 aprovados; falha remanescente é a regressão do hash. Dois processos ROS2 reais também completaram duas entregas (incluindo acentos) e rejeitaram a tag adulterada.

Mesmas sementes e sorteios da auditoria anterior, 50000 sessões por variante:

| Variante | Legítimo incorreto | 1 bit: segredo original | 1 bit: diferente da rejeição | 1 byte: segredo original |
| --- | ---: | ---: | ---: | ---: |
| Kyber512 | 0 | 8 | 7 | 2 |
| Kyber768 | 0 | 10 | 9 | 0 |
| Kyber1024 | 0 | 6 | 5 | 0 |

No cenário de byte, houve 1 saída diferente da rejeição em Kyber512. Algumas saídas coincidiram simultaneamente com o segredo original e o valor de rejeição, razão pela qual as colunas diferem; ambas as medidas estão no JSON. Dois bits distintos e ciphertext totalmente aleatório tiveram zero casos de manutenção do segredo/diferença da rejeição nesta amostra. Zeros observados não provam probabilidade zero e essas contagens não medem chance de invasão.

[Resultados consolidados CHF1](../ref/test/results/framing_20261103.json). [Auditoria anterior à alteração](../ref/test/results/audit_20261103/RELATORIO.md). Os testes de replay e de emissor sem identidade continuam sendo limitações de protocolo, separadas desta correção.

**As seções seguintes registram a versão anterior ao CHF1. Suas contagens, checksums e estados de aprovação são históricos.**

## Alteração Q32 e referência dos PDFs

Referências: [REF 13 Draft_Version.pdf](<../../REF 13 Draft_Version.pdf>), páginas 9–13; [SBCCI2026_Angelo_final.pdf](<../../SBCCI2026_Angelo_final.pdf>), páginas 2–3. A alteração para 32 bits fracionários foi autorizada como experimento; modifica o critério Q10.24 do SBCCI.

| Elemento | Q24 original | Q32 atual |
| --- | --- | --- |
| Escala / folding | `2^24` / máscara de 24 bits | `2^32` / máscara de 32 bits |
| Normalização UINT32 | `d >> 8` | `d`, sem perda direta de bits |
| Extração dos 16 bits superiores | Deslocamento 8 | Deslocamento 16 |
| Inicialização de `c` | Conversão para `int32_t` | Conversão para `int64_t`, evitando estouro |

Blocos de oito bytes, padding, mapa, ajuste de alpha, `Hsize + 200` iterações e truncamentos intermediários foram mantidos. Q32 resolve o descarte direto, mas não torna o mapa injetivo nem elimina todas as colisões. Os PDFs têm ambiguidades sobre `c`/`cdata` e extração; não foram repetidas as baterias estatísticas ou a síntese HLS.

## Amostragem Q32 ampliada — 100.000 sessões por variante

Foram usadas cinco sementes distintas, com 20.000 sessões por semente e variante: **300.000 sessões e 1.500.000 avaliações de desencapsulação**. Cada sessão usa novas moedas de geração/encapsulação e verifica um ciphertext válido e quatro cenários de alteração. O núcleo Q32 não foi modificado.

| Variante | Legítimo incorreto | 1 bit | 1 byte XOR | 2 bits distintos | Ciphertext aleatório |
| --- | ---: | ---: | ---: | ---: | ---: |
| Kyber512 | 0 | 24 | 0 | 0 | 0 |
| Kyber768 | 0 | 12 | 0 | 1 | 0 |
| Kyber1024 | 0 | 12 | 0 | 0 | 0 |

Cada célula é uma contagem em **100.000 casos do respectivo cenário**. Para alterações, falha significa saída diferente do segredo de rejeição; em todos os casos observados, ela também foi igual ao segredo original. Bit e byte são sorteados uniformemente; a máscara de byte é XOR 1..255; os dois bits têm posições distintas; o ciphertext aleatório tem o comprimento correto.

| Variante | Falha com 1 bit | IC de 95% Wilson | Falhas por semente (20.000 casos cada) |
| --- | ---: | ---: | --- |
| Kyber512 | 0,02400% | 0,01613%–0,03571% | 5, 6, 6, 3, 4 |
| Kyber768 | 0,01200% | 0,00686%–0,02098% | 3, 4, 2, 2, 1 |
| Kyber1024 | 0,01200% | 0,00686%–0,02098% | 3, 1, 3, 1, 4 |

Os zeros têm limite superior Wilson de 95% de aproximadamente **0,00384% por cenário e variante**, não probabilidade zero. Os intervalos são individuais, sem ajuste para múltiplas comparações. A amostra maior detectou falhas raras ausentes nos lotes anteriores de 10.000 sessões. As frequências valem para esses sorteios e não estimam ataques dirigidos.

[Resultados completos e exemplos reproduzíveis](../ref/test/results/q32_extended_20261020.json). O arquivo inclui as cinco sementes, contagens por lote e por região do ciphertext, intervalos e hashes das bibliotecas. O gerador SHAKE256 serve apenas a entradas de teste. Não se avaliaram ROS2, perdas de rede ou o pacote completo AES-GCM.

Reprodução na raiz do repositório, após compilar as bibliotecas Q32 na pasta indicada:

```bash
python3 ref/test/sample_campaign.py --library-dir /tmp/kyber-q32-expanded-libs --output /tmp/q32-expanded-campaign.json
```

## Primeira avaliação Q32 — 10.000 sessões por variante

| Variante | Alterações determinísticas | Falhas Q24 → Q32 | Bit aleatório: Q24 → Q32 | Byte aleatório: Q24 → Q32 |
| --- | ---: | ---: | ---: | ---: |
| Kyber512 | 6.144 | 65 → 1 | 118 → 1 | 7 → 0 |
| Kyber768 | 8.704 | 65 → 1 | 70 → 0 | 1 → 0 |
| Kyber1024 | 12.544 | 65 → 1 | 40 → 0 | 4 → 0 |

As duas últimas colunas têm **10.000 novas sessões por variante e precisão**, com as mesmas moedas e posições em Q24/Q32 para comparação. Não houve erro de recuperação legítima em nenhum cenário. Em Q32, 1/10.000 = 0,01% em Kyber512 para bit aleatório; os zeros observados não demonstram ausência de falha. Máscara de byte sorteada uniformemente entre 1 e 255.

Os testes determinísticos continuam encontrando uma falha em todas as variantes. Em Kyber768, o caso altera o bit 64 da mensagem interna: após o segundo bloco, os estados reais são `50996013` e `50996014`; após o terceiro bloco, ambos valem `1993567333`, com partes imaginárias iguais. A normalização preservou a diferença, mas houve reconvergência.

Validações adicionais: 136 comparações por precisão com uma referência Python independente passaram; vetores Q32 e extrações XOF passaram; 17 cabeçalhos compilaram em C++ nas três variantes e duas precisões; ASan/UBSan e verificação de conversões flutuantes passaram nas entradas exercitadas (LeakSanitizer desativado). Compilação C completa e ROS2 passaram; mensagem ROS2 Q32 foi recuperada com hashes iguais, e o teste de cifra/autenticação passou.

**A bateria permanece reprovada:** `test_rejection*` acusa 1 falha por variante. `test_hashing` também reprova uma nova verificação de sensibilidade: alterar o bit 0 do byte 16 da entrada `00..3f` mantém o hash de 512 bits. Esse é um caso adicional de colisão, distinto da contagem dos ciphertexts. O CTest ROS2 reproduz essas duas categorias de falha.

Registros e método reproduzível: [resultados JSON](../ref/test/results/q24_q32_20261020.json), [referência independente](../ref/test/reference_hash.py) e [amostragem](../ref/test/estimate_errors.py). A amostragem deriva moedas de teste por SHAKE256 com semente fixa, independentemente do hash caótico; não é um gerador de chaves de produção. As taxas não medem a chance de ataque deliberado.

## Histórico Q24: as 65 falhas

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

## Histórico Q24: amostragem de 06/10/2026

Em 2026-10-06 foram testadas **10.000 sessões Kyber768**, com novas chaves e encapsulações por sessão. Posições foram sorteadas uniformemente; no cenário de byte, a máscara XOR foi sorteada entre 1 e 255. A seleção usou amostragem com rejeição para evitar viés de módulo.

| Cenário | Falhas / casos | Frequência | IC de 95% — Wilson |
| --- | ---: | ---: | ---: |
| Ciphertext válido: segredo incorreto | 0/10.000 | 0% observado | 0%–0,0384% |
| Um bit alterado: segredo original mantido | 71/10.000 | 0,71% | 0,5633%–0,8945% |
| Um byte alterado: segredo original mantido | 2/10.000 | 0,02% | 0,0055%–0,0729% |

São 10.000 observações por cenário, compartilhando as mesmas sessões; as taxas não são combinadas. Em ambos os cenários alterados, as contagens de segredo original mantido e saída diferente da rejeição coincidiram. [Método de Wilson — NIST](https://itl.nist.gov/div898/handbook/prc/section2/prc241.htm).

Essas estimativas valem para alterações aleatórias definidas acima, não para ataques deliberados, outras variantes ou perdas de rede. Zero falhas legítimas observadas não prova probabilidade zero; apenas dois casos no cenário de byte tornam essa estimativa exploratória. As falhas também ocorreram fora das sementes originais.

Os registros ficaram em `/tmp/kyber-global-estimate.c`, `.json` e `-replay.bin`; podem desaparecer com a limpeza do ambiente. O replay usa registros de 105 bytes: moedas de chave (64), moedas de encapsulação (32), índice de bit e posição de byte (UINT32 little-endian cada), máscara (1).

## Próximas correções candidatas

Primeiro, comparar o adaptador com a implementação realmente utilizada no estudo SBCCI. Depois, avaliar codificação que preserve todos os bits da entrada ou maior precisão nos produtos intermediários. Ambas exigem validação e podem mudar resultados; preservar os bits não garante eliminar a reconvergência. A mudança Q32 já foi aplicada; alterar os truncamentos é uma próxima hipótese, ainda não implementada.

<a id="validacao"></a>

## Histórico de validação Q24

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

Na versão Q32 atual, a bateria retorna falha para rejeição e sensibilidade do hash; nenhum caso foi excluído. `kem_check` verifica apenas um caso de adulteração. A inspeção inicial sem ROS2 é histórica: a instalação foi concluída antes da validação da simulação.

O pacote ROS2 também autentica o ciphertext Kyber como dado associado do AES-GCM. Essa camada adicional não corrige o KEM. `tamper:=true` testa a tag GCM, não as 65 falhas. As colisões permanecem uma limitação criptográfica e não devem ser ignoradas ao avaliar segurança.

## Reproduzir a avaliação atual

Na raiz, execute `make -C ref all` e `make -C ref check`. Para conferir as equações e repetir a amostragem de Kyber768:

```bash
python3 ref/test/reference_hash.py --library ref/lib/libpqcrystals_kyber768_ref.so --precision 32
python3 ref/test/estimate_errors.py --library ref/lib/libpqcrystals_kyber768_ref.so --variant 768 --precision 32 --sessions 10000 --output /tmp/q32-768.json
```

A macro `CHAOTIC_FRAC_BITS` deve ser a mesma em todas as fontes. Q24 e Q32 são incompatíveis em resultados: recompile e gere novas chaves em ambos os lados. Os slides existentes representam a avaliação histórica Q24 e não foram atualizados nesta alteração.
