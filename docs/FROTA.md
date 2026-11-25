# Demo Zenoh: central e drones simulados

[README](../README.md) · [Instalação e protótipo A–B](SIMULACAO.md)

A central A envia comandos a B, C e outros drones simulados. Cada ligação usa chaves, sessão e contadores próprios. As interfaces ROS2 usam Zenoh; o tráfego entre os sistemas passa pela criptografia Kyber768 com hashing caótico e AES-256-GCM.

## Preparar

Com ROS2 Jazzy e as dependências do [tutorial de instalação](SIMULACAO.md) disponíveis, compile:

```bash
cd ~/ProjetoIC/pqc-kyber-marcos/pqc-kyber/ros2
source /opt/ros/jazzy/setup.bash
colcon build --packages-up-to pqc_zenoh_demo
```

Mantenha `build/` e `install/` para aproveitar a compilação incremental. Em **cada terminal**:

```bash
cd ~/ProjetoIC/pqc-kyber-marcos/pqc-kyber/ros2
source /opt/ros/jazzy/setup.bash
source install/setup.bash
unset ZENOH_CONFIG_OVERRIDE ZENOH_SESSION_CONFIG_URI ZENOH_ROUTER_CONFIG_URI
```

## Executar em dois terminais

Inicie a central primeiro:

```bash
ros2 run pqc_zenoh_demo fleet central --show-crypto
```

No segundo terminal, inicie B e aguarde `Drone B conectado.` na central:

```bash
ros2 run pqc_zenoh_demo fleet drone B --show-crypto
```

Para acrescentar C, execute `fleet drone C --show-crypto` com o mesmo prefixo `ros2 run pqc_zenoh_demo` em outro terminal. B e C já ficam cadastrados na central; `--drones` é opcional e permite escolher outros IDs iniciais.

Digite os comandos **dentro do terminal da central**:

| Comando | O que faz |
| --- | --- |
| `goto B 10 20 5` | Envia o destino absoluto `(10,20,5)` em metros |
| `pos B` | Consulta B por pedido e resposta cifrados; não retorna apenas o cache |
| `tamper B 7 8 9` | Envia um goto com um bit da tag de autenticação adulterado |
| `status B` / `list` | Mostra dados armazenados e conectividade |
| `add D` | Cadastra D; inicie também `fleet drone D` em outro terminal |
| `cancel B` | Interrompe um goto ainda ativo |
| `help` / `quit` | Lista comandos / encerra a central |

Comandos e IDs aceitam maiúsculas e minúsculas (`pos b` equivale a `POS B`). As coordenadas usam o referencial local `map`: x/y entre −100 e 100 e z entre 0 e 50 metros. O goto atualiza a posição diretamente após validação. Para movimento gradual e cancelamento durante o trajeto, acrescente `--simulate-motion` ao iniciar o drone.

## Como funciona e o que aparece

O terminal publica em uma interface ROS2 local; o callback serializa e cifra os dados, e `Link` os envia pelo router Zenoh. O drone autentica, decifra e executa o comando. A resposta percorre o caminho inverso. `pos B` também faz essa troca completa; se não houver resposta em três segundos, informa timeout sem substituir a resposta pelo cache.

Com `--show-crypto`, aparecem comando/posição, sentido A → B ou B → A, tamanho do pacote, prefixos do hash e da cifra. No recebimento, os hashes completos **recebido** e **calculado** aparecem com `IGUAIS`, junto à confirmação AES-GCM. Compare envio e recebimento no mesmo sentido. O hash cobre o envelope da mensagem, incluindo sessão, sequência e dados ROS2; os valores variam entre execuções.

Os horários usam **HH:MM:SS de Brasília**. Use `--full` para detalhes técnicos, incluindo canais e sequências. Sem `--show-crypto`, aparecem apenas as etapas funcionais. Chaves privadas e segredos compartilhados não são exibidos; os logs completos ficam no caminho indicado por `Logs:`.

`src/fleet_node.cpp` implementa comandos e simulação; `src/common.hpp`, serialização e transporte protegido. `scripts/fleet.py` inicia os processos e apresenta os logs, reutilizando funções de `demo.py`, que também mantém o protótipo A–B com ação Fibonacci. Na frota, goto/cancel usam tópicos e confirmações do protocolo, não uma ação de navegação.

## Teste rápido para apresentação

Execute um comando por vez, aguardando a conclusão:

```text
goto B 10 20 5
tamper B 7 8 9
pos B
```

O primeiro comando posiciona B. O segundo cifra outro goto e inverte **um bit da tag AES-GCM depois da cifragem**, sem alterar o algoritmo. No terminal do drone deve aparecer:

```text
PACOTE REJEITADO | autenticação AES-GCM falhou; comando não executado
```

`pos B` deve recuperar **(10,20,5)**: o pacote adulterado foi descartado e não mudou a posição. Depois, `goto B 7 8 9` deve funcionar normalmente. A rejeição é observada no drone; a central não recebe uma confirmação desse goto inválido. Como a tag é alterada, o preview da cifra não precisa mudar, e o pacote rejeitado não chega à etapa de comparação dos hashes.

Para a bateria automática, com o ambiente preparado:

```bash
ros2 run pqc_zenoh_demo fleet test
```

Resultados de **25/11/2026**: adulteração rejeitada, posição preservada, goto válido posterior e isolamento entre drones aprovados; **29 pares criptográficos autenticados**, **5/5 testes criptográficos** e regressão A–B aprovados. A bateria também verifica cancelamento, coordenadas inválidas, consultas de posição e perda do enlace. [Resultados completos](../ref/test/results/tamper_20261125.json).

Tudo roda na mesma máquina/WSL, em loopback. Há um router central na porta 7447 e routers locais nas portas 7448 (central), 7449 (B), 7450 (C) e 7451 (D); `--central` e `--local-port` permitem alterá-las. As posições são simuladas, sem controle de hardware. As chaves são aceitas no primeiro uso, sem identidade certificada; os testes não comprovam a segurança global da criptografia experimental. Para reiniciar do zero, encerre central e drones com Ctrl+C e inicie ambos novamente.
