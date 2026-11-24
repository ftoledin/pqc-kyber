# Simulação ROS2

[README](../README.md) · [Criptografia](CRIPTOGRAFIA.md)

Dois terminais na mesma distribuição WSL2 representam A (`sender`) e B (`receiver`). Kyber estabelece um segredo; AES-256-GCM cifra a mensagem. B autentica, descriptografa e compara o hash caótico recebido com o recalculado. Hash não é descriptografado.

## Protótipo com Zenoh: tópicos e ação

O pacote `pqc_zenoh_demo` implementa a nova arquitetura. A demonstração anterior `pqc_kyber/sender` e `receiver`, descrita nas seções seguintes, permanece disponível.

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
sudo apt install ros-jazzy-rmw-zenoh-cpp ros-jazzy-zenoh-cpp-vendor ros-jazzy-rclcpp-action ros-jazzy-example-interfaces
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

Validação em 24/11/2026: cinco regressões do pacote original e o novo teste de integração aprovados; os dois lançadores manuais também foram executados separadamente. [Resultados e logs consolidados](../ref/test/results/zenoh_20261124.json). As dependências foram extraídas em uma pasta temporária para essa validação; execute a instalação acima no seu ambiente antes de compilar.

Os dados ROS2 são serializados em CDR. Tipo, ID da solicitação, identificador da sessão e contador ficam dentro do conteúdo protegido por Kyber experimental + AES-GCM. A sessão remota deve coincidir e o contador deve aumentar; repetições e mensagens fora de ordem são rejeitadas após validar a autenticação. A fila é limitada a 256 eventos. A identificação da sessão evita reutilização acidental entre execuções; as chaves são fixadas no primeiro recebimento (**TOFU, sem identidade certificada**). Quem tiver acesso ao enlace ainda pode anunciar uma chave falsa inicialmente ou criar pacotes usando a chave pública: não há autenticação criptográfica do emissor. Os routers ficam restritos a localhost.

O núcleo criptográfico não foi alterado nesta integração. Permanecem as limitações de segurança do hash experimental discutidas em [Criptografia](CRIPTOGRAFIA.md). O hash transportado no formato legado da demo é autenticado, mas visível; metadados/tamanhos também podem ser observados. A ação Fibonacci não comanda um drone, e o teste não inclui FROTA360, Zenoh entre computadores, Nav2 ou controle de voo. Perda do enlace não garante parada remota: o robô de exemplo apenas termina seu cálculo finito. Autenticação de identidades, reconexão, validade de comandos e comportamento seguro de voo precisam ser projetados antes da aplicação real.

Código: `gateway.cpp` implementa os adaptadores; `common.hpp` encapsula serialização e transporte protegido; `robot.cpp` e `controller.cpp` simulam os sistemas; `probe.cpp` e `scripts/demo.py` verificam o enlace. Referências: [rmw_zenoh Jazzy](https://github.com/ros2/rmw_zenoh/tree/jazzy) e [Zenoh C++](https://github.com/eclipse-zenoh/zenoh-cpp).

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

A versão atual usa Q32.32 com xorshift, CHF1 e pacote versão 3. Recompile A e B e gere novas chaves após atualizar; pacotes antigos são rejeitados. A limpeza removeu `build/`, `install/` e `log/`; os comandos seguintes recriam o ambiente local.

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
ros2 run pqc_kyber sender --ros-args -p 'message:="Olá, drone! Altitude: 10 metros"'
```

Use aspas duplas **dentro** das aspas simples: as simples protegem o argumento no Bash, e as duplas fazem o YAML do ROS2 interpretar o valor como texto, inclusive quando contém `: ` ou apenas números.

A mostra texto, hash, cifra e resposta. B mostra texto recuperado, hash recebido e recalculado, concluindo com `AES-GCM autenticado; hashes iguais`. A termina após a resposta; repita o comando para enviar outra mensagem. B permanece ativo até Ctrl+C. O limite é 64 KiB.

## Adulteração: `-p tamper:=true`

`--ros-args` inicia os argumentos ROS2; `-p` define um parâmetro; `tamper:=true` ativa a adulteração implementada neste projeto. O padrão é `false`.

Após cifrar, A executa `packet.data.back() ^= 1`: inverte um bit do último byte da **tag GCM**. B rejeita a autenticação sem exibir a mensagem recuperada. Isso não altera o núcleo caótico nem reproduz as 65 falhas do KEM.

```bash
ros2 run pqc_kyber sender --ros-args -p 'message:="Teste adulterado"' -p tamper:=true
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
ROS_DOMAIN_ID=43 ros2 run pqc_kyber sender --ros-args -p 'message:="Teste de dominio"'
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
