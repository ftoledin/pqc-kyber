# PQC-Kyber — simulação ROS2 original

Esta branch preserva a demonstração em dois terminais com `sender` e `receiver`, usando ROS2 DDS e o núcleo atual Q32.32 + xorshift + CHF1. A envia uma mensagem protegida por Kyber experimental e AES-GCM; B recupera o texto e compara os hashes.

| Branch | Conteúdo |
| --- | --- |
| [crypto-only](https://github.com/ftoledin/pqc-kyber/tree/crypto-only) | Criptografia e testes sem ROS2 |
| [ros2-demo](https://github.com/ftoledin/pqc-kyber/tree/ros2-demo) | Simulação ROS2 original com sender/receiver |
| [zenoh-demo](https://github.com/ftoledin/pqc-kyber/tree/zenoh-demo) | Simulação atual com tópicos, ação e gateways Zenoh |

Com ROS2 Jazzy e as dependências instaladas, na pasta `ros2`:

```bash
source /opt/ros/jazzy/setup.bash
colcon build --packages-select pqc_kyber
source install/setup.bash
export RMW_IMPLEMENTATION=rmw_fastrtps_cpp
python3 pqc_kyber/test/run_demo.py
```

Para executar em dois terminais e ver comandos completos de instalação, siga [Simulação original](docs/SIMULACAO.md). A variante padrão é Kyber768. Os arquivos de simulação foram preservados antes de sua remoção da branch Zenoh.

Os testes aprovados verificam funcionamento, sem certificar a segurança do hash experimental. Consulte [Criptografia](docs/CRIPTOGRAFIA.md) para as alterações e resultados históricos. Preserve `build/` e `install/` para não recompilar tudo entre demonstrações.
