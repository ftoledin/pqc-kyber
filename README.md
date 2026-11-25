# PQC Kyber com hashing caótico e Zenoh

| Branch | Conteúdo |
| --- | --- |
| [crypto-only](https://github.com/ftoledin/pqc-kyber/tree/crypto-only) | Criptografia e testes sem ROS2 |
| [ros2-demo](https://github.com/ftoledin/pqc-kyber/tree/ros2-demo) | Simulação ROS2 original com sender/receiver |
| [zenoh-demo](https://github.com/ftoledin/pqc-kyber/tree/zenoh-demo) | Simulação atual com gateways Zenoh |

Kyber em C, com hashing caótico e cabeçalhos compatíveis com C++ por `extern "C"`. Os PDFs orientam a criptografia; ROS2 e a comunicação sistema/drone são requisitos do projeto.

- [Central e frota simulada](docs/FROTA.md): execução em dois terminais, comandos goto/pos e teste de adulteração.
- [Simulação A–B de referência](docs/SIMULACAO.md): instalação Zenoh e teste original com ação Fibonacci.
- [Criptografia](docs/CRIPTOGRAFIA.md): critérios dos PDFs, falhas conhecidas e resultados dos testes.
- [Integração Zenoh A–B em PDF](docs/relatorio_integracao_zenoh.pdf): diagramas e testes do protótipo de referência com Fibonacci; a frota está descrita no tutorial acima.

## Estrutura e estado atual

| Pasta | Conteúdo |
| --- | --- |
| `ref/` | Kyber, adaptadores, Makefile e testes C |
| `hash_options/` | Núcleo do hashing caótico |
| `ros2/pqc_kyber/` | Biblioteca criptográfica compartilhada e testes C/C++ |
| `ros2/pqc_zenoh_demo/` | Central e drones simulados; gateways e ação A–B de referência |
| `docs/` | Tutorial e análise criptográfica |

O [novo protótipo Zenoh](docs/SIMULACAO.md#protótipo-com-zenoh-tópicos-e-ação) usa gateways C++, tópicos bidirecionais e uma ação Fibonacci. A comunicação passa por um router central entre os blocos criptográficos; as interfaces locais também usam `rmw_zenoh_cpp`, sem DDS.

O ambiente validado usa Ubuntu 24.04/WSL2, ROS2 Jazzy e Kyber768. A envia comandos e recebe telemetria; uma ação Fibonacci demonstra aceitação, feedback, resultado e cancelamento. Conexão com drone real permanece uma etapa futura.

A nova demo de frota usa `ros2 run pqc_zenoh_demo fleet central` na central e `fleet drone B`/`fleet drone C` nos outros terminais, com os setups carregados. B e C são os IDs padrão; `--drones` permite escolher outros, e `add D` acrescenta um drone durante a execução. Na central, `goto B 10 20 5` solicita um destino local absoluto; `pos B` mostra a posição e `cancel B` solicita interrupção. Cada drone tem chaves, sessão e contadores próprios. Os comandos de posição desta versão usam tópicos ROS2; não acionam hardware real.

O goto atualiza a posição diretamente após validar o comando. `pos B` faz uma consulta remota cifrada e mostra a resposta validada; `status B` consulta apenas o cache. `tamper B 7 8 9` demonstra um goto com um bit da tag adulterado: confira a rejeição no drone e use `pos B` para verificar a posição inalterada. `--show-crypto` apresenta a ida e a volta cifradas, tamanho do pacote e prefixos do hash e da cifra, com horário de Brasília (HH:MM:SS). Use `--full` para os detalhes completos; `--simulate-motion` no drone ativa o deslocamento gradual opcional.

O núcleo usa **Q32.32 com xorshift 13/17/5**, integrado da versão [fe63e12 do repositório original](https://github.com/marcospgr/pqc-kyber/commit/fe63e12a04f6f70b4e01e9b52ae15b32b151ceda), com a codificação local CHF1 preservada. A lógica nova foi incorporada ao arquivo ativo `hash_options/chaotic_hash.c`; o arquivo Q32.32 do upstream ainda não era selecionado pelo Makefile original.

Na validação de 17/11/2026, **10/10 testes C e 5/5 testes do pacote ROS2 passaram**. Não houve falha observada nas 150000 sessões KEM amostradas nem nos testes de colisão exercitados. Isso não comprova segurança criptográfica; os resultados anteriores permanecem como histórico em [Criptografia](docs/CRIPTOGRAFIA.md).

A atualização muda hashes, chaves e ciphertexts. **Recompile ambos os participantes e gere novas chaves.** A demo agora usa pacote versão 3, rejeitando versões 1 e 2.

## Uso rápido

Com as dependências instaladas, compile a simulação Zenoh e verifique a ligação C/C++:

```bash
cd ~/ProjetoIC/pqc-kyber-marcos/pqc-kyber/ros2
source /opt/ros/jazzy/setup.bash
colcon build --packages-up-to pqc_zenoh_demo
source install/setup.bash
ros2 run pqc_kyber kem_check
```

Continue no [tutorial da central e drones](docs/FROTA.md). O [protótipo A–B com Fibonacci](docs/SIMULACAO.md#demonstração-em-dois-terminais) permanece como referência. Para a bateria C, execute `make -C ref check` na raiz; o comando retorna falha se qualquer regressão reprovar.

Antes de distribuir, preencha mantenedor e licença em `ros2/pqc_kyber/package.xml`.

Preserve `ros2/build` e `ros2/install` entre demonstrações para aproveitar a compilação incremental. Arquivos gerados não são versionados.
