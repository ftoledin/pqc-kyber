# PQC Kyber com hashing caótico e ROS2

Kyber em C, com hashing caótico e cabeçalhos compatíveis com C++ por `extern "C"`. Os PDFs orientam a criptografia; ROS2 e a comunicação sistema/drone são requisitos do projeto.

- [Simulação](docs/SIMULACAO.md): instalação, execução em dois terminais e exemplos de falha.
- [Criptografia](docs/CRIPTOGRAFIA.md): critérios dos PDFs, falhas conhecidas e resultados dos testes.

## Estrutura e estado atual

| Pasta | Conteúdo |
| --- | --- |
| `ref/` | Kyber, adaptadores, Makefile e testes C |
| `hash_options/` | Núcleo do hashing caótico |
| `ros2/pqc_kyber/` | Pacote ROS2, nós C++ e testes da simulação |
| `docs/` | Tutorial e análise criptográfica |

A simulação validada usa Ubuntu 24.04/WSL2, ROS2 Jazzy e Kyber768. A envia dados cifrados a B; B autentica, recupera a mensagem e compara o hash. O retorno atual é um status textual. Dados cifrados nos dois sentidos e conexão com drone real são etapas futuras.

As falhas de rejeição do KEM permanecem abertas. O funcionamento da simulação não comprova a segurança criptográfica da construção.

## Uso rápido

Com as dependências instaladas:

```bash
cd ~/ProjetoIC/pqc-kyber-marcos/pqc-kyber/ros2
source /opt/ros/jazzy/setup.bash
colcon build --packages-select pqc_kyber
source install/setup.bash
ros2 run pqc_kyber kem_check
```

Continue no [tutorial de envio e recebimento](docs/SIMULACAO.md#execucao). Para a bateria C, execute `make -C ref check` na raiz; o comando retorna falha enquanto as regressões de rejeição falharem.

Antes de distribuir, preencha mantenedor e licença em `ros2/pqc_kyber/package.xml`.
