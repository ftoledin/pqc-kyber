# PQC Kyber com hashing caótico e Zenoh

| Branch | Conteúdo |
| --- | --- |
| [crypto-only](https://github.com/ftoledin/pqc-kyber/tree/crypto-only) | Criptografia e testes sem ROS2 |
| [ros2-demo](https://github.com/ftoledin/pqc-kyber/tree/ros2-demo) | Simulação ROS2 original com sender/receiver |
| [zenoh-demo](https://github.com/ftoledin/pqc-kyber/tree/zenoh-demo) | Simulação atual com gateways Zenoh |

Kyber em C, com hashing caótico e cabeçalhos compatíveis com C++ por `extern "C"`. Os PDFs orientam a criptografia; ROS2 e a comunicação sistema/drone são requisitos do projeto.

- [Simulação](docs/SIMULACAO.md): instalação, execução em dois terminais e exemplos de falha.
- [Criptografia](docs/CRIPTOGRAFIA.md): critérios dos PDFs, falhas conhecidas e resultados dos testes.
- [Integração Zenoh em PDF](docs/relatorio_integracao_zenoh.pdf): diagramas, funções do código, etapas da comunicação, execução e testes.

## Estrutura e estado atual

| Pasta | Conteúdo |
| --- | --- |
| `ref/` | Kyber, adaptadores, Makefile e testes C |
| `hash_options/` | Núcleo do hashing caótico |
| `ros2/pqc_kyber/` | Biblioteca criptográfica compartilhada e testes C/C++ |
| `ros2/pqc_zenoh_demo/` | Gateways ROS2/Zenoh, tópicos e ação de exemplo |
| `docs/` | Tutorial e análise criptográfica |

O [novo protótipo Zenoh](docs/SIMULACAO.md#protótipo-com-zenoh-tópicos-e-ação) usa gateways C++, tópicos bidirecionais e uma ação Fibonacci. A comunicação passa por um router central entre os blocos criptográficos; as interfaces locais também usam `rmw_zenoh_cpp`, sem DDS.

O ambiente validado usa Ubuntu 24.04/WSL2, ROS2 Jazzy e Kyber768. A envia comandos e recebe telemetria; uma ação Fibonacci demonstra aceitação, feedback, resultado e cancelamento. Conexão com drone real permanece uma etapa futura.

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

Continue no [tutorial da simulação Zenoh](docs/SIMULACAO.md#demonstração-em-dois-terminais). Para a bateria C, execute `make -C ref check` na raiz; o comando retorna falha se qualquer regressão reprovar.

Antes de distribuir, preencha mantenedor e licença em `ros2/pqc_kyber/package.xml`.

Preserve `ros2/build` e `ros2/install` entre demonstrações para aproveitar a compilação incremental. Arquivos gerados não são versionados.
