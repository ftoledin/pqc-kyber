#!/usr/bin/env python3
"""Run real ROS2 nodes for a presentation or the delivery regression check."""
import argparse
import json
import os
import signal
import subprocess
import tempfile
import time
import uuid


def stop(process):
    """Also stop ros2 run's child executable, including after a timeout."""
    try:
        os.killpg(process.pid, signal.SIGINT)
    except ProcessLookupError:
        return
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        process.wait()


def read_log(log):
    with open(log.name, encoding="utf-8") as reader:
        return reader.read()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--message', help='Run one presentation with this text')
    parser.add_argument('--tamper', action='store_true', help='Expect rejection of an altered GCM tag')
    args = parser.parse_args()
    if args.message is not None or args.tamper:
        cases = [(args.message if args.message is not None else 'Ola, computador B!', args.tamper)]
    else:
        cases = [('Ola, computador B!', False), ('Comunicação ROS2 com acentos', False),
                 ('Pacote adulterado', True)]
    env = dict(os.environ, ROS_AUTOMATIC_DISCOVERY_RANGE='LOCALHOST')
    env.setdefault('ROS_DOMAIN_ID', '87')
    env.pop('ROS_LOCALHOST_ONLY', None)
    # Isolate each run from manual nodes and previous demos in the same domain.
    namespace = f'__ns:=/pqc_demo_{uuid.uuid4().hex[:12]}'
    with tempfile.TemporaryDirectory(prefix='kyber-demo-') as directory, tempfile.NamedTemporaryFile(mode='w+', encoding='utf-8') as log:
        env['ROS_LOG_DIR'] = directory
        receiver = subprocess.Popen(['ros2', 'run', 'pqc_kyber', 'receiver', '--ros-args', '-r', namespace],
                                    stdout=log, stderr=subprocess.STDOUT, env=env, start_new_session=True)
        try:
            deadline = time.monotonic() + 15
            while True:
                if 'Computador B pronto' in read_log(log):
                    break
                if receiver.poll() is not None:
                    raise RuntimeError('O receptor encerrou antes de ficar pronto; veja o log de B.')
                if time.monotonic() >= deadline:
                    raise RuntimeError('O receptor nao iniciou em 15 segundos; veja o log de B.')
                time.sleep(0.1)
            for message, tamper in cases:
                print('\n=== Computador A: envio' + (' adulterado ===' if tamper else ' normal ==='), flush=True)
                # JSON string quoting also makes numeric and YAML-like messages stay strings.
                command = ['ros2', 'run', 'pqc_kyber', 'sender', '--ros-args', '-r', namespace,
                           '-p', 'message:=' + json.dumps(message, ensure_ascii=False),
                           '-p', f'tamper:={str(tamper).lower()}']
                with tempfile.TemporaryFile(mode='w+') as sender_log:
                    sender = subprocess.Popen(command, env=env, stdout=sender_log,
                                              stderr=subprocess.STDOUT, start_new_session=True)
                    try:
                        code = sender.wait(timeout=40)
                    except subprocess.TimeoutExpired as error:
                        raise RuntimeError('Tempo esgotado no emissor; confira a descoberta DDS.') from error
                    finally:
                        stop(sender)
                        sender_log.seek(0)
                        output = sender_log.read()
                        print(output, end='', flush=True)
                if tamper:
                    if code != 1 or 'ERRO: Autenticacao AES-GCM falhou' not in output:
                        raise RuntimeError('A tag adulterada nao foi rejeitada como esperado.')
                    print('Resultado esperado: adulteracao rejeitada.', flush=True)
                elif code != 0 or 'OK: mensagem recuperada' not in output:
                    raise RuntimeError('Falha na entrega; veja os logs dos dois computadores.')
        finally:
            stop(receiver)
            output = read_log(log)
            print('\n=== Computador B: recebimento ===\n' + output, end='', flush=True)
        for message, tamper in cases:
            shown = f'Mensagem descriptografada: {message}' in output
            if not tamper and not shown:
                raise RuntimeError('Mensagem nao registrada no receptor.')
            if tamper and shown and (message, False) not in cases:
                raise RuntimeError('Mensagem adulterada exposta.')
    print('\nPASS: dois processos ROS2, entrega, hashes e rejeicao de adulteracao'
          if len(cases) > 1 else '\nPASS: demonstracao concluida conforme o esperado')


if __name__ == '__main__':
    try:
        main()
    except KeyboardInterrupt:
        raise SystemExit(130)
    except (RuntimeError, OSError) as error:
        print(f'ERRO: {error}', flush=True)
        raise SystemExit(1)
