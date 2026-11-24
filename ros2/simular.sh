#!/usr/bin/env bash
# Build and run a local presentation from any working directory.
set -eo pipefail
workspace_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
  echo "Uso: bash ros2/simular.sh [--message 'Seu texto'] [--tamper]"
  echo "Sem argumentos: duas entregas e uma adulteracao esperada."
  exit 0
fi
if [[ ! -f /opt/ros/jazzy/setup.bash ]]; then
  echo "ERRO: ROS2 Jazzy ausente em /opt/ros/jazzy. Consulte docs/SIMULACAO.md." >&2
  exit 1
fi
source /opt/ros/jazzy/setup.bash
for command_name in colcon cmake python3 ros2; do
  if ! command -v "$command_name" >/dev/null; then
    echo "ERRO: ferramenta ausente: $command_name. Consulte docs/SIMULACAO.md." >&2
    exit 1
  fi
done
cd -- "$workspace_dir"
echo '=== Compilando o pacote ROS2 ==='
colcon build --packages-select pqc_kyber
source install/setup.bash
unset ROS_LOCALHOST_ONLY
export ROS_AUTOMATIC_DISCOVERY_RANGE=LOCALHOST
export ROS_DOMAIN_ID="${ROS_DOMAIN_ID:-87}"
export RCUTILS_COLORIZED_OUTPUT=0
exec python3 pqc_kyber/test/run_demo.py "$@"
