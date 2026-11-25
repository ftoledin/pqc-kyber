# Instalação e protótipo A–B

[README](../README.md) · [Central e drones](FROTA.md) · [Criptografia](CRIPTOGRAFIA.md)

## Instalação e ambiente

No Ubuntu 24.04, com ROS2 Jazzy e seu repositório APT configurados:

```bash
sudo apt update
sudo apt install ros-jazzy-ros-base ros-jazzy-ament-cmake \
  ros-jazzy-rmw-zenoh-cpp ros-jazzy-zenoh-cpp-vendor \
  ros-jazzy-rclcpp-action ros-jazzy-example-interfaces ros-jazzy-geometry-msgs \
  build-essential cmake python3-colcon-common-extensions libssl-dev
cd ~/ProjetoIC/pqc-kyber-marcos/pqc-kyber/ros2
source /opt/ros/jazzy/setup.bash
colcon build --packages-up-to pqc_zenoh_demo
```

Mantenha `build/` e `install/` para aproveitar a compilação incremental. Para executar qualquer demo, prepare **cada terminal**:

```bash
cd ~/ProjetoIC/pqc-kyber-marcos/pqc-kyber/ros2
source /opt/ros/jazzy/setup.bash
source install/setup.bash
unset ZENOH_CONFIG_OVERRIDE ZENOH_SESSION_CONFIG_URI ZENOH_ROUTER_CONFIG_URI
```

## Protótipo com Zenoh: tópicos e ação

O protótipo A–B verifica tópicos bidirecionais e uma ação ROS2 Fibonacci, que calcula uma sequência numérica para demonstrar aceitação, progresso, resultado e cancelamento. Os gateways protegem as mensagens com Kyber/hashing caótico e AES-GCM; as interfaces locais usam `rmw_zenoh_cpp`.

Fluxo: **ROS2 A ↔ gateway A ↔ router Zenoh ↔ gateway B ↔ ROS2 B**. `src/gateway.cpp` e `src/common.hpp` implementam a integração; `robot.cpp` e `controller.cpp` são os participantes de exemplo. A simulação de drones com goto/pos está em [FROTA.md](FROTA.md).

## Demonstração em dois terminais

Com o ambiente preparado, execute em terminais separados, iniciando A primeiro:

```bash
# Terminal A
ros2 run pqc_zenoh_demo demo a
# Terminal B
ros2 run pqc_zenoh_demo demo b
```

A envia um comando, recebe telemetria, conclui uma ação Fibonacci e cancela outra. Os logs mostram `ZENOH TX cifrado`, `ZENOH RX autenticado` e a comparação dos hashes. Ctrl+C encerra cada lado; reinicie ambos para repetir.

Teste automático: `ros2 run pqc_zenoh_demo demo test`. Na validação de **25/11/2026**, passaram comando, telemetria, ação, cancelamento, rejeição de adulteração/replay e interrupção da entrega sem o router central. [Resultados](../ref/test/results/tamper_20261125.json).

Tudo roda em loopback; não execute esta demo e a frota simultaneamente nas mesmas portas. Se o pacote não for encontrado, confira a compilação e `install/setup.bash`; para porta ocupada ou troca de chave recusada, encerre e reinicie ambos os lados. As chaves são aceitas no primeiro uso, sem autenticação de identidade. [Relatório técnico A–B](relatorio_integracao_zenoh.pdf).
