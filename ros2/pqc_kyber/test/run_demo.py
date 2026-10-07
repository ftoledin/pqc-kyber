#!/usr/bin/env python3
"""End-to-end check: run after sourcing ROS2 and the package's install/setup.bash."""
import os
import signal
import subprocess
import tempfile


def main():
    env = dict(os.environ, ROS_DOMAIN_ID="87", ROS_AUTOMATIC_DISCOVERY_RANGE="LOCALHOST")
    env.pop("ROS_LOCALHOST_ONLY", None)
    with tempfile.TemporaryDirectory(prefix="kyber-demo-") as directory, tempfile.TemporaryFile(mode="w+") as log:
        env["ROS_LOG_DIR"] = directory
        receiver = subprocess.Popen(["ros2", "run", "pqc_kyber", "receiver"],
                                    stdout=log, stderr=subprocess.STDOUT, env=env,
                                    start_new_session=True)
        try:
            for message in ("Ola, computador B!", "Comunicação ROS2 com acentos"):
                sender = subprocess.run(["ros2", "run", "pqc_kyber", "sender", "--ros-args",
                    "-p", f"message:={message}"], env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=40)
                print(sender.stdout, end="")
                if sender.returncode != 0 or "OK: mensagem recuperada" not in sender.stdout:
                    raise RuntimeError(f"Falha na entrega: {sender.stdout}")
            sender = subprocess.run(["ros2", "run", "pqc_kyber", "sender", "--ros-args",
                "-p", "message:=Pacote adulterado", "-p", "tamper:=true"],
                env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=40)
            print(sender.stdout, end="")
            if sender.returncode == 0 or "Autenticacao AES-GCM falhou" not in sender.stdout:
                raise RuntimeError("Adulteracao nao rejeitada")
        finally:
            os.killpg(receiver.pid, signal.SIGINT)
            try:
                receiver.wait(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(receiver.pid, signal.SIGKILL)
                receiver.wait()
            log.seek(0)
            output = log.read()
            print(output, end="")
        for message in ("Ola, computador B!", "Comunicação ROS2 com acentos"):
            if f"Mensagem descriptografada: {message}" not in output:
                raise RuntimeError("Mensagem nao registrada no receptor")
        if "Mensagem descriptografada: Pacote adulterado" in output:
            raise RuntimeError("Mensagem adulterada exposta")
    print("PASS: dois processos ROS2, entrega, hashes e rejeicao de adulteracao")


if __name__ == "__main__":
    main()
