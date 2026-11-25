# Central e drones com Zenoh

[README](../README.md) · [Instalação e ambiente](SIMULACAO.md#instalação-e-ambiente)

A central envia comandos por interfaces ROS2. Os gateways serializam e protegem os dados com Kyber768, hashing caótico e AES-256-GCM, transmitindo pelo router Zenoh. O drone valida o pacote e devolve uma resposta cifrada. Cada ligação possui chaves, sessão e contadores próprios.

## Executar

Após a instalação, carregue o [ambiente em cada terminal](SIMULACAO.md#instalação-e-ambiente). Inicie a central primeiro:

```bash
ros2 run pqc_zenoh_demo fleet central --show-crypto
```

No segundo terminal:

```bash
ros2 run pqc_zenoh_demo fleet drone B --show-crypto
```

B e C são cadastrados por padrão. Para iniciar C, use o mesmo comando do drone substituindo B por C em outro terminal. Aguarde a indicação de conexão antes de enviar comandos pela central.

| Comando na central | Função |
| --- | --- |
| `goto B 10 20 5` | Define a posição absoluta em metros; atualização imediata após validação |
| `pos B` | Consulta a posição por pedido e resposta cifrados |
| `tamper B 7 8 9` | Envia um goto com um bit da tag de autenticação adulterado |
| `status B` / `list` | Exibe cache / lista os drones e sua conectividade |
| `add D` | Cadastra D; inicie também o processo `fleet drone D` |
| `cancel B` / `help` / `quit` | Cancela um goto ativo / lista comandos / encerra a central |

Coordenadas no referencial local `map`: x/y de −100 a 100 e z de 0 a 50 metros. Comandos e IDs aceitam maiúsculas e minúsculas. `--simulate-motion` no drone permite movimento gradual e cancelamento durante o trajeto.

`--show-crypto` exibe sentido, tamanho, prefixos de hash/cifra e os hashes completos recebido e calculado com `IGUAIS`. Os horários são de Brasília; `--full` mostra os detalhes técnicos. `src/fleet_node.cpp` implementa a simulação, `src/common.hpp` o transporte protegido e `scripts/fleet.py` a inicialização e exibição.

## Teste e resultados

Na central, execute um comando por vez:

```text
goto B 10 20 5
tamper B 7 8 9
pos B
```

O drone deve indicar `PACOTE REJEITADO` por falha AES-GCM. A consulta deve retornar `(10,20,5)`, demonstrando que o goto adulterado não foi executado. Um goto válido posterior continua funcionando.

Teste automático: `ros2 run pqc_zenoh_demo fleet test`. Na validação de **25/11/2026**, passaram a rejeição de adulteração, posição preservada, isolamento entre drones, cancelamento e perda do enlace; foram conferidos **29 pares criptográficos autenticados**, **5/5 testes criptográficos** e a regressão A–B. [Resultados completos](../ref/test/results/tamper_20261125.json).

A execução é local, sem controle de drone físico. As chaves são aceitas no primeiro uso, sem identidade certificada; os resultados não comprovam segurança criptográfica global. Ctrl+C encerra cada lado; reinicie central e drones para renovar as sessões.
