# Simulação ROS2

[README](../README.md) · [Criptografia](CRIPTOGRAFIA.md)

Dois terminais na mesma distribuição WSL2 representam A (`sender`) e B (`receiver`). Kyber estabelece um segredo; AES-256-GCM cifra a mensagem. B autentica, descriptografa e compara o hash caótico recebido com o recalculado. Hash não é descriptografado.

## Ambiente e instalação

Ambiente validado: **Ubuntu 24.04, WSL2 e ROS2 Jazzy**. Se já estiver instalado, avance à compilação. A instalação deve estar na mesma distribuição WSL do projeto; confira com `wsl --list --verbose` no PowerShell.

Se o APT não encontrar `ros-jazzy-*`, configure o repositório oficial:

```bash
sudo apt update
sudo apt install curl python3 software-properties-common
sudo add-apt-repository universe
ROS_APT_SOURCE_VERSION=$(curl -fsSL https://api.github.com/repos/ros-infrastructure/ros-apt-source/releases/latest | python3 -c 'import json,sys; print(json.load(sys.stdin)["tag_name"])')
curl -fL -o /tmp/ros2-apt-source.deb "https://github.com/ros-infrastructure/ros-apt-source/releases/download/${ROS_APT_SOURCE_VERSION}/ros2-apt-source_${ROS_APT_SOURCE_VERSION}.noble_all.deb"
sudo dpkg -i /tmp/ros2-apt-source.deb
sudo apt update
apt-cache policy ros-jazzy-ros-base
```

Execute na ordem; corrija falhas de download antes de continuar. `Candidate` deve indicar uma versão. Instale os componentes ausentes:

```bash
sudo apt install ros-jazzy-ros-base ros-jazzy-ament-cmake ros-jazzy-rclcpp ros-jazzy-std-msgs build-essential cmake python3-colcon-common-extensions libssl-dev
```

Para locale UTF-8, atualizações e instalações personalizadas, consulte a [instalação oficial Jazzy](https://raw.githubusercontent.com/ros2/ros2_documentation/jazzy/source/Installation/Ubuntu-Install-Debs.rst) e a [configuração do repositório](https://raw.githubusercontent.com/ros2/ros2_documentation/jazzy/source/Installation/_Apt-Repositories.rst).

## Compilação

```bash
cd ~/ProjetoIC/pqc-kyber-marcos/pqc-kyber/ros2
source /opt/ros/jazzy/setup.bash
colcon build --packages-select pqc_kyber
source install/setup.bash
ros2 run pqc_kyber kem_check
```

A saída deve confirmar acordo do segredo e rejeição de uma alteração específica. Para conferir o ambiente, use `ros2 pkg prefix ament_cmake`, `cmake --version` e `colcon --help`.

<a id="execucao"></a>

## Envio e recebimento

Execute em **ambos os terminais**:

```bash
cd ~/ProjetoIC/pqc-kyber-marcos/pqc-kyber/ros2
source /opt/ros/jazzy/setup.bash
source install/setup.bash
export ROS_DOMAIN_ID=42
unset ROS_LOCALHOST_ONLY
export ROS_AUTOMATIC_DISCOVERY_RANGE=LOCALHOST
```

No terminal **B**, inicie o receptor e deixe-o ativo:

```bash
ros2 run pqc_kyber receiver
```

No terminal **A**, envie a mensagem:

```bash
ros2 run pqc_kyber sender --ros-args -p 'message:=Olá, computador B!'
```

A mostra texto, hash, cifra e resposta. B mostra texto recuperado, hash recebido e recalculado, concluindo com `AES-GCM autenticado; hashes iguais`. A termina após a resposta; repita o comando para enviar outra mensagem. B permanece ativo até Ctrl+C. O limite é 64 KiB.

## Adulteração: `-p tamper:=true`

`--ros-args` inicia os argumentos ROS2; `-p` define um parâmetro; `tamper:=true` ativa a adulteração implementada neste projeto. O padrão é `false`.

Após cifrar, A executa `packet.data.back() ^= 1`: inverte um bit do último byte da **tag GCM**. B rejeita a autenticação sem exibir a mensagem recuperada. Isso não altera o núcleo caótico nem reproduz as 65 falhas do KEM.

```bash
ros2 run pqc_kyber sender --ros-args -p 'message:=Teste adulterado' -p tamper:=true
echo "Codigo de saida: $?"
```

Resultado esperado: `Autenticacao AES-GCM falhou: pacote rejeitado`, resposta `ERRO` e saída 1. Para envio normal, omita `tamper` ou use `-p tamper:=false`.

## Outros casos de falha

| Caso | Como reproduzir | Resultado esperado |
| --- | --- | --- |
| Receptor desligado | Encerre B com Ctrl+C e execute um envio normal | Timeout em aproximadamente 30 s; saída 1 |
| Domínios diferentes | Com B no domínio 42, execute o comando abaixo | Mesmo timeout; nenhum recebimento |
| Pacote incompleto | Com B ativo, publique os três bytes abaixo | B registra `Pacote de tamanho invalido` |

Domínio diferente somente para esta execução:

```bash
ROS_DOMAIN_ID=43 ros2 run pqc_kyber sender --ros-args -p 'message:=Teste de dominio'
```

Pacote incompleto, no terminal A preparado anteriormente:

```bash
ros2 topic pub --once --wait-matching-subscriptions 1 --max-wait-time-secs 10 /pqc_demo/encrypted std_msgs/msg/UInt8MultiArray '{data: [1, 2, 3]}'
```

O publicador apenas envia bytes; confira a rejeição no log de B. Para recuperar o funcionamento, mantenha B ativo e os dois terminais no domínio 42.

## Testes e diagnóstico

Na pasta `ros2`, após carregar os setups, execute `python3 pqc_kyber/test/run_demo.py`. Ele inicia seus próprios nós no domínio 87, verifica duas entregas e rejeição de tag adulterada, sem executar a regressão das 65 falhas.

| Problema | Ação |
| --- | --- |
| ROS2 ou setup ausente | Confira a distribuição WSL e a instalação base |
| CMake/colcon/ament ausente | Instale as dependências e carregue o setup ROS2 |
| `pqc_kyber` não encontrado | Compile na pasta `ros2` e carregue `install/setup.bash` |
| APT sem pacotes Jazzy | Confira Ubuntu Noble, repositório e `sudo apt update` |
| Timeout | Confira B ativo, domínio igual e descoberta local |
| `ERROR invalid ciphertext` nos testes C | Consulte [Criptografia](CRIPTOGRAFIA.md); não é erro de instalação |

## Detalhes essenciais

Os tópicos são `/pqc_demo/public_key`, `/pqc_demo/encrypted` e `/pqc_demo/status`. O pacote contém versão/variante, ciphertext Kyber, nonce, hash, mensagem cifrada e tag. O prefixo até o hash é autenticado pelo GCM; a chave privada permanece em B.

A chave pública e o status textual não autenticam a identidade dos dispositivos. Use um receptor e um envio por vez. `LOCALHOST` restringe esta simulação à máquina local; comunicação bidirecional cifrada e drone real ainda não estão implementados.

A variante padrão é Kyber768. Para Kyber512/1024, compile com `--cmake-args -DKYBER_K=2`/`4`; para voltar, use `3`. Reinicie os nós após recompilar. A e B devem usar a mesma variante.
