# Validação criptográfica — 03/11/2026, anterior ao CHF1

**Conclusão: a versão Q32 funciona nos envios legítimos testados, mas permanece reprovada nas propriedades criptográficas verificadas.** Nenhum arquivo da implementação criptográfica foi alterado nesta avaliação; foram adicionados testes e resultados.

SHA-256 de `hash_options/chaotic_hash.c`: `416eb049b8ec10eb243a7b2036b79fd5ada67792d7a625528e8330573feb9826`.

## Bateria existente

- Recompilação forçada dos testes e bibliotecas; 6 dos 10 executáveis passaram.
- `test_hashing` falhou: entrada `00..3f`, alteração no byte 16, bit 0, mantém o hash de 512 bits.
- `test_rejection512/768/1024` falharam: respectivamente, 1/6144, 1/8704 e 1/12544 alterações de um bit mantiveram o segredo original. Posições: byte 672/bit 3, byte 992/bit 3 e byte 1448/bit 4.
- Os três `test_kyber*` e os três `test_vectors*` passaram nesta execução.
- CTest do pacote ROS2, recompilado em pasta separada: cifra/autenticação e verificação C++ do KEM passaram; regressões de hash e rejeição falharam (2/4).
- O gerador `nistkat/PQCgenKAT_kem*` completou 100 casos por variante. É uma verificação de consistência interna; não compara com vetores oficiais nem certifica o algoritmo modificado.

## Nova amostragem de KEM

50.000 sessões por variante; chave e encapsulação novas por sessão. Em cada sessão: ciphertext válido, um bit, um byte XOR 1–255, dois bits distintos e ciphertext totalmente aleatório. Total: 150.000 sessões, 750.000 desencapsulações.

| Variante | Erro no segredo legítimo | Um bit: segredo mantido | Um byte: segredo mantido | Dois bits | Totalmente aleatório |
| --- | ---: | ---: | ---: | ---: | ---: |
| Kyber512 | 0/50000 | 11/50000 (0,022%) | 2/50000 (0,004%) | 0/50000 | 0/50000 |
| Kyber768 | 0/50000 | 14/50000 (0,028%) | 0/50000 | 0/50000 | 0/50000 |
| Kyber1024 | 0/50000 | 4/50000 (0,008%) | 0/50000 | 0/50000 | 0/50000 |

IC de 95% de Wilson para a coluna de um bit: 512 [0,01229%; 0,03939%], 768 [0,01668%; 0,04700%], 1024 [0,00311%; 0,02057%]. Intervalos individuais, sem correção por múltiplas comparações. Zero observado não demonstra probabilidade zero. As frequências descrevem os sorteios desta campanha; não medem chance de invasão, perda de drone ou ataque dirigido.

Em todos os casos classificados como falha, a saída foi o segredo original, diferente do valor de rejeição calculado pelo `rkprf`. A função de desencapsulação usa rejeição implícita; seu retorno zero, isoladamente, não significa aceitação.

Sementes: `audit-20261103-v1-512`, `audit-20261103-v1-768`, `audit-20261103-v1-1024`. Geração determinística de entradas de teste por SHAKE256 da biblioteca Python, independente do hash caótico; não usada como aleatoriedade de produção.

## Falha adicional: preenchimento ambíguo do hash

Exemplos reproduzidos em C e na referência Python independente, nas saídas de 256 e 512 bits:

| Entrada A, hexadecimal | Entrada B, hexadecimal | Resultado |
| --- | --- | --- |
| `41` | `41 00` | Mesmo hash |
| `41 42 43 44` | `41 42 43 44 01` | Mesmo hash |
| `61 62 63 64 65` | `61 62 63 64 65 00` | Mesmo hash |

O processamento completa palavras parciais com zeros, usa `cd_imag=1` quando falta a segunda palavra e não codifica o comprimento da mensagem. Entradas distintas podem produzir os mesmos pares de palavras e a mesma trajetória interna. A diferença desaparece antes das iterações; aumentar a precisão numérica não resolve essa ambiguidade por si só.

Por tamanho de hash: adicionar zero a entradas de comprimentos 1–64 gerou **48 colisões em 64 pares**. Adicionar `01` a comprimentos 4, 12, …, 60 gerou **8 colisões em 8 pares**. São testes dirigidos de estrutura, não uma estimativa global.

Em 3584 alterações individuais de bits em nove entradas de 32/64 bytes, houve também **15 colisões no hash de 256 bits** e **13 no de 512 bits**. Os comprimentos são iguais nesses casos: evidência adicional da perda de diferenças/reconvergência já conhecida. A fração média de bits da saída alterados ficou próxima de 49,8%, o que não elimina as colisões nem demonstra segurança.

O problema afeta a distinção entre dados binários pelo hash. Ele não demonstra que alguém consegue alterar uma mensagem e ainda produzir uma tag GCM válida.

## Pacote completo da demo: verificações adicionais

Nas três variantes, `test_protocol_extended.cpp` passou nos casos exercitados:

- 110 mensagens binárias por variante, incluindo vazio, bytes nulos, limites de bloco e 65536 bytes; mais 256 novas cifras/recuperações do mesmo texto, sem repetição do pacote observado.
- **29696 alterações de um bit**, distribuídas por todos os bits de um pacote por variante: cabeçalho, KEM, nonce, hash, texto cifrado e tag. Todas rejeitadas.
- Todas as truncagens de cada pacote, 128 extensões, 1000 pacotes aleatórios por variante, 64 chaves privadas não relacionadas e tamanhos inválidos rejeitados.
- Comparação AES-GCM com a API Python `cryptography`: 6 mensagens × 4 verificações × 3 variantes = **72 verificações sem falha**, incluindo alteração de AAD e tag. É uma conferência por outra API; ambas as camadas podem usar OpenSSL.
- AddressSanitizer, UndefinedBehaviorSanitizer e `float-cast-overflow`: suíte adicional passou nas três variantes, sem diagnósticos. Detecção de vazamentos desativada; isso não é prova de ausência de defeitos fora das entradas executadas.

### Limitações de protocolo reproduzidas localmente

Por variante, `demo::open()` aceitou 10/10 repetições de um pacote válido e 10/10 pacotes gerados por outro emissor usando apenas a chave pública. Essa camada não possui controle de replay nem autenticação de identidade. Não é uma quebra do AES ou um teste de invasão pela rede: não foram exercitadas permissões DDS/SROS2, tráfego ROS2 entre máquinas ou atuação de drone.

## Reprodução

Na raiz do repositório:

```bash
make -B -C ref -j3 test shared nistkat
python3 ref/test/run_checks.py /tmp/kyber-existing.json
python3 ref/test/audit_hash.py --library ref/lib/libpqcrystals_kyber768_ref.so --output /tmp/kyber-hash.json
python3 ref/test/reference_hash.py --library ref/lib/libpqcrystals_kyber768_ref.so --precision 32
python3 ref/test/estimate_errors.py --library ref/lib/libpqcrystals_kyber768_ref.so --variant 768 --precision 32 --extended --sessions 50000 --seed audit-20261103-v1-768 --output /tmp/kyber-sampling768.json
g++ -std=c++17 -O2 -DKYBER_K=3 -Iref -Ihash_options -Iros2/pqc_kyber/src ref/test/test_protocol_extended.cpp -Lref/lib -lpqcrystals_kyber768_ref -lcrypto -Wl,-rpath,"$PWD/ref/lib" -o /tmp/kyber-protocol768
/tmp/kyber-protocol768
python3 ref/test/check_aes_interop.py --executable /tmp/kyber-protocol768 --output /tmp/kyber-interop768.json
```

Os comandos de hash e regressão retornam código 1 devido às falhas encontradas. Para 512/1024, use `KYBER_K=2/4`, biblioteca e variante correspondentes. Os sanitizadores exigem recompilar tanto os arquivos C quanto o teste C++ com `-O1 -g -fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=all -fno-omit-frame-pointer`; executar com `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1` e `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`.

Dados completos, posições, exemplos, versões e hashes dos fontes/bibliotecas: [summary.json](summary.json). Os registros individuais foram consolidados em `summary.json`; duplicatas e logs de compilação foram removidos na limpeza. Esta auditoria precede a correção básica CHF1; seus resultados permanecem como histórico.
