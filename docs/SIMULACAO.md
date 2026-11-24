# Simulação ROS2 com Zenoh

[README](../README.md) · [Criptografia](CRIPTOGRAFIA.md)

## Protótipo com Zenoh: tópicos e ação

O pacote `pqc_zenoh_demo` implementa os gateways, tópicos bidirecionais e a ação de exemplo. `pqc_kyber` fornece somente a biblioteca criptográfica e seus testes. A demonstração anterior está preservada na [branch ros2-demo](https://github.com/ftoledin/pqc-kyber/tree/ros2-demo).

O [relatório da integração Zenoh em PDF](relatorio_integracao_zenoh.pdf) apresenta diagramas das comunicações, funções responsáveis por cada etapa e os testes do protótipo.

```text
Operador A → ROS2 local A → gateway criptográfico A
                                 ↕
                          router Zenoh central
                                 ↕
Robô B     ↔ ROS2 local B ↔ gateway criptográfico B
```

Todos os nós deste protótipo usam `rmw_zenoh_cpp`, incluindo as interfaces locais. Existem três routers TCP em loopback: central (7447), local A (7448) e local B (7449). Os dois locais pertencem às interfaces ROS2; somente as sessões nativas dos gateways alcançam o central. Não há conexão entre esses routers nem descoberta multicast/gossip. Domínios ROS2 61/62 dão uma separação adicional. O transporte central recebe chaves públicas e pacotes cifrados, em vez dos tópicos/ações ROS2 em claro.

### Instalação e compilação

No Ubuntu 24.04 com ROS2 Jazzy e repositório APT já configurados:

```bash
sudo apt update
sudo apt install ros-jazzy-ros-base ros-jazzy-ament-cmake \
  ros-jazzy-rmw-zenoh-cpp ros-jazzy-zenoh-cpp-vendor \
  ros-jazzy-rclcpp-action ros-jazzy-example-interfaces \
  build-essential cmake python3-colcon-common-extensions libssl-dev
cd ~/ProjetoIC/pqc-kyber-marcos/pqc-kyber/ros2
source /opt/ros/jazzy/setup.bash
colcon build --packages-up-to pqc_zenoh_demo
source install/setup.bash
```

**Compile uma vez e mantenha `build/` e `install/`.** O resumo `2 packages finished [43.3s]` é tempo de compilação, não de execução da comunicação. Para repetir a demo sem alterar os fontes, passe diretamente aos comandos dos dois terminais abaixo. Se alterar o código, execute o `colcon build` novamente: ele reaproveita a compilação anterior e reconstrói o necessário. Não apague essas pastas entre demonstrações.

Os dois pacotes usam `Release` por padrão quando nenhum tipo de compilação é informado. Isso otimiza a execução, mas não garante uma primeira compilação mais rápida. Para reduzir o pico de memória da compilação em um computador limitado, use o comando abaixo; apenas um trabalho será compilado por vez, o que pode aumentar o tempo total:

```bash
CMAKE_BUILD_PARALLEL_LEVEL=1 colcon build --packages-up-to pqc_zenoh_demo --executor sequential
```

### Demonstração em dois terminais

Em **ambos**, carregue o ambiente:

```bash
cd ~/ProjetoIC/pqc-kyber-marcos/pqc-kyber/ros2
source /opt/ros/jazzy/setup.bash
source install/setup.bash
unset ZENOH_CONFIG_OVERRIDE ZENOH_SESSION_CONFIG_URI ZENOH_ROUTER_CONFIG_URI
```

Inicie **A primeiro**; ele inicia o router central e aguarda B:

```bash
ros2 run pqc_zenoh_demo demo a
```

No outro terminal, inicie **B**:

```bash
ros2 run pqc_zenoh_demo demo b
```

A envia `Subir para 10 metros (simulado)` e recebe telemetria `87.5`. Em seguida, conclui uma ação Fibonacci de ordem 8 e cancela outra de ordem 12 após três feedbacks. B mostra o comando e a execução/cancelamento. As linhas `ZENOH TX cifrado` e `ZENOH RX autenticado` identificam a travessia dos gateways; o log `hashes iguais` confirma a comparação. Os routers e gateways ficam ativos após a demonstração. Ctrl+C encerra cada lado; **reinicie os dois lados para repetir**, pois a chave remota fica fixada por execução. Portas em uso ou B ausente por mais de 45 segundos interrompem a inicialização.

O lançador lê apenas os trechos novos dos logs, evitando reler arquivos inteiros continuamente durante demonstrações longas. Os três routers permanecem para manter as redes locais separadas do enlace criptográfico central.

| Interface ROS2 local | Tipo | Caminho |
| --- | --- | --- |
| `/demo/command` | `std_msgs/msg/String` | A → B |
| `/demo/telemetry` | `std_msgs/msg/Float32` | B → A |
| `/demo/fibonacci` | `example_interfaces/action/Fibonacci` | solicitação/cancelamento A → B; aceitação/feedback/resultado B → A |

O gateway A oferece um servidor de ação ao operador; o B é cliente do servidor de ação do robô. Ambos são clientes Zenoh do router central. A só aceita a meta depois de obter a aceitação remota. O cancelamento local é aceito como encaminhamento; a confirmação remota e o resultado cancelado chegam depois. O exemplo admite uma meta ativa por vez, ordens entre 2 e 30 e timeout de 3 segundos para aceitação.

### Testes automatizados e limitações

Com os setups carregados e sem necessidade dos dois terminais manuais:

```bash
ros2 run pqc_zenoh_demo demo test
```

O teste usa portas livres e prefixo exclusivo; inicia e encerra seus próprios processos. Verifica entrega do comando, telemetria, feedback/resultado, cancelamento, rejeição de tag adulterada e replay, além de desligar o router central e confirmar que um comando novo deixa de chegar. Um observador do enlace busca a frase de comando em claro; essa verificação é pontual, não uma prova de confidencialidade. `Logs:` informa a pasta temporária com as configurações, saídas e `results.json`. Também pode ser executado via `colcon test --packages-select pqc_zenoh_demo` e `colcon test-result --verbose`.

Validação em 24/11/2026: cinco regressões do pacote original e o novo teste de integração aprovados; os dois lançadores manuais também foram executados separadamente. [Resultados e logs consolidados](../ref/test/results/zenoh_20261124.json). Os resultados históricos são anteriores à separação das demos; a nova validação está em [Separação das branches](../ref/test/results/legacy_split_20261124.json).

Os dados ROS2 são serializados em CDR. Tipo, ID da solicitação, identificador da sessão e contador ficam dentro do conteúdo protegido por Kyber experimental + AES-GCM. A sessão remota deve coincidir e o contador deve aumentar; repetições e mensagens fora de ordem são rejeitadas após validar a autenticação. A fila é limitada a 256 eventos. A identificação da sessão evita reutilização acidental entre execuções; as chaves são fixadas no primeiro recebimento (**TOFU, sem identidade certificada**). Quem tiver acesso ao enlace ainda pode anunciar uma chave falsa inicialmente ou criar pacotes usando a chave pública: não há autenticação criptográfica do emissor. Os routers ficam restritos a localhost.

O núcleo criptográfico não foi alterado nesta integração. Permanecem as limitações de segurança do hash experimental discutidas em [Criptografia](CRIPTOGRAFIA.md). O hash externo do pacote criptográfico é autenticado, mas visível; metadados/tamanhos também podem ser observados. A ação Fibonacci não comanda um drone, e o teste não inclui FROTA360, Zenoh entre computadores, Nav2 ou controle de voo. Perda do enlace não garante parada remota: o robô de exemplo apenas termina seu cálculo finito. Autenticação de identidades, reconexão, validade de comandos e comportamento seguro de voo precisam ser projetados antes da aplicação real.

Código: `gateway.cpp` implementa os adaptadores; `common.hpp` encapsula serialização e transporte protegido; `robot.cpp` e `controller.cpp` simulam os sistemas; `probe.cpp` e `scripts/demo.py` verificam o enlace. Referências: [rmw_zenoh Jazzy](https://github.com/ros2/rmw_zenoh/tree/jazzy) e [Zenoh C++](https://github.com/eclipse-zenoh/zenoh-cpp).

## Diagnóstico

| Problema | Ação |
| --- | --- |
| Pacote Jazzy não encontrado no APT | Confira Ubuntu 24.04 e o repositório oficial do ROS2 |
| `pqc_zenoh_demo` não encontrado | Compile com `--packages-up-to pqc_zenoh_demo` e carregue `install/setup.bash` |
| Portas ocupadas | Encerre as execuções anteriores antes de iniciar A e B |
| `LINK READY` não aparece | Inicie A primeiro, depois B; remova configurações Zenoh herdadas com `unset` |
| Troca de chave recusada | Reinicie os dois lados; a chave do par fica fixada por execução |

Para testar somente a ligação C/C++, execute `ros2 run pqc_kyber kem_check`. Para as cinco regressões criptográficas, use `colcon test --packages-select pqc_kyber` e `colcon test-result --verbose`.

A variante padrão é Kyber768. Para Kyber512/1024, recompile ambos os pacotes com `--cmake-args -DKYBER_K=2`/`4`; para voltar, use `3`. Os dois lados devem usar a mesma variante e gerar novas chaves após atualizar.
