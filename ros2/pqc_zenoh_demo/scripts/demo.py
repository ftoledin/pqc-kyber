#!/usr/bin/env python3
"""Launch isolated local ROS2-over-Zenoh networks and an encrypted central link."""
import argparse
import json
import os
from pathlib import Path
import signal
import socket
import subprocess
import tempfile
import time
import uuid

from ament_index_python.packages import get_package_prefix


class Demo:
    def __init__(self, args):
        self.args = args
        self.directory = Path(args.logs or tempfile.mkdtemp(prefix='pqc-zenoh-'))
        self.directory.mkdir(parents=True, exist_ok=True)
        self.processes = {}
        self.handles = []
        self.offsets = {}
        self.stopped = set()
        self.prefix = args.prefix
        self.bin = Path(get_package_prefix('pqc_zenoh_demo')) / 'lib/pqc_zenoh_demo'
        self.router = Path(get_package_prefix('rmw_zenoh_cpp')) / 'lib/rmw_zenoh_cpp/rmw_zenohd'
        self.envs = {}
        for role, port, domain in [('a', args.local_a, 61), ('b', args.local_b, 62)]:
            session = self.config('session_' + role, dict(
                mode='client', connect=dict(endpoints=[f'tcp/127.0.0.1:{port}']),
                scouting=dict(multicast=dict(enabled=False), gossip=dict(enabled=False))))
            env = dict(os.environ, RMW_IMPLEMENTATION='rmw_zenoh_cpp', ROS_DOMAIN_ID=str(domain),
                       ZENOH_SESSION_CONFIG_URI=str(session))
            env.pop('ROS_LOCALHOST_ONLY', None)
            env.pop('ROS_AUTOMATIC_DISCOVERY_RANGE', None)
            self.envs[role] = env

    def config(self, name, value):
        path = self.directory / (name + '.json5')
        path.write_text(json.dumps(value))
        return path

    def start_router(self, name, port):
        config = self.config(name, dict(mode='router', listen=dict(endpoints=[f'tcp/127.0.0.1:{port}']),
            connect=dict(endpoints=[]), scouting=dict(multicast=dict(enabled=False), gossip=dict(enabled=False))))
        env = dict(os.environ, ZENOH_ROUTER_CONFIG_URI=str(config))
        self.spawn(name, [self.router], env)
        time.sleep(.5)

    def start_side(self, role):
        self.start_router('router_' + role, self.args.local_a if role == 'a' else self.args.local_b)
        if role == 'b':
            self.spawn('robot', [self.bin / 'robot'], self.envs[role])
        self.spawn('gateway_' + role, [self.bin / 'gateway', '--ros-args', '-p', 'role:=' + role,
            '-p', 'endpoint:="' + self.args.endpoint + '"', '-p', 'prefix:="' + self.prefix + '"'], self.envs[role])

    def controller(self, name, cancel=False):
        p = self.spawn(name, [self.bin / 'controller', '--ros-args', '-p', 'order:=12' if cancel else 'order:=8',
                       '-p', 'cancel_after:=3' if cancel else 'cancel_after:=0'], self.envs['a'])
        self.wait(lambda: p.poll() is not None, 25)
        if p.returncode != 0:
            raise RuntimeError(self.log(name))
        expected = 'ACTION RESULT CANCELED' if cancel else 'ACTION RESULT SUCCEEDED'
        assert expected in self.log(name) and 'TELEMETRY RECEIVED' in self.log(name)

    def log(self, name):
        return (self.directory / (name + '.log')).read_text(errors='replace')

    def spawn(self, name, command, env, *, stdin=None):
        if name in self.processes:
            raise ValueError('Nome de processo repetido: ' + name)
        out = (self.directory / (name + '.log')).open('w')
        self.handles.append(out)
        process = subprocess.Popen([str(x) for x in command], env=env, stdin=stdin, stdout=out,
                                   stderr=subprocess.STDOUT, start_new_session=True)
        self.processes[name] = process
        return process

    def show(self):
        if self.args.mode == 'test':
            return
        for name in self.processes:
            # Read only new bytes: rereading growing logs made idle demos costlier over time.
            with (self.directory / (name + '.log')).open(errors='replace') as stream:
                stream.seek(self.offsets.get(name, 0))
                content = stream.read()
                self.offsets[name] = stream.tell()
            for line in content.splitlines():
                print('[' + name + '] ' + line, flush=True)

    def wait(self, predicate, timeout):
        deadline = time.monotonic() + timeout
        while not predicate():
            self.show()
            for name, process in self.processes.items():
                if name not in self.stopped and name.startswith(('gateway', 'router', 'robot')) and process.poll() is not None:
                    raise RuntimeError(name + ' encerrou: ' + self.log(name))
            if time.monotonic() > deadline:
                raise TimeoutError('Tempo esgotado. Confira os logs em ' + str(self.directory))
            time.sleep(.1)
        self.show()

    def run(self):
        print('Logs:', self.directory, flush=True)
        if self.args.mode in ('a', 'test'):
            self.start_router('router_central', self.args.central)
        if self.args.mode in ('b', 'test'):
            self.start_side('b')
        if self.args.mode in ('a', 'test'):
            self.start_side('a')
            self.wait(lambda: 'LINK READY' in self.log('gateway_a'), 45)
            if self.args.mode == 'test':
                self.spawn('probe', [self.bin / 'probe', self.args.endpoint, self.prefix], os.environ.copy())
                time.sleep(.5)
            self.controller('operator_success')
            self.controller('operator_cancel', cancel=True)
            if self.args.mode == 'test':
                self.wait(lambda: self.processes['probe'].poll() is not None, 20)
                self.wait(lambda: 'REPLAY rejeitado' in self.log('gateway_b'), 10)
                assert self.processes['probe'].returncode == 0
                assert 'Autenticacao AES-GCM falhou' in self.log('gateway_b')
                assert 'COMMAND RECEIVED' in self.log('robot')
                assert self.log('robot').count('COMMAND RECEIVED') == 2
                # Stop the sole central route: a new command must not reach B.
                self.stop('router_central')
                count = self.log('robot').count('COMMAND RECEIVED')
                ros2 = ['ros2', 'topic', 'pub', '--once', '--wait-matching-subscriptions', '1',
                        '--max-wait-time-secs', '5', '/demo/command', 'std_msgs/msg/String',
                        '{data: "NAO_DEVE_ATRAVESSAR_SEM_ROUTER"}']
                p = self.spawn('negative_route', ros2, self.envs['a'])
                self.wait(lambda: p.poll() is not None, 10)
                assert p.returncode == 0
                time.sleep(2)
                assert self.log('robot').count('COMMAND RECEIVED') == count
                assert 'NAO_DEVE_ATRAVESSAR_SEM_ROUTER' not in self.log('robot')
                result = dict(command=True, telemetry=True, action_success=True, feedback=True,
                              cancellation=True, tamper_rejected=True, replay_rejected=True,
                              no_plaintext_literal_in_probe=True, no_bypass_without_central_router=True,
                              rmw='rmw_zenoh_cpp', local_domains=[61, 62], log_directory=str(self.directory))
                (self.directory / 'results.json').write_text(json.dumps(result, indent=2) + '\n')
                print('PASS:', json.dumps(result), flush=True)
                return
            print('Demonstração concluída. Ctrl+C encerra A; reinicie A e B para repetir.', flush=True)
        while True:
            self.show()
            time.sleep(.1)

    def stop(self, name):
        self.stopped.add(name)
        p = self.processes[name]
        if p.poll() is not None:
            return
        os.killpg(p.pid, signal.SIGINT)
        try:
            p.wait(timeout=5)
        except subprocess.TimeoutExpired:
            os.killpg(p.pid, signal.SIGKILL)
            p.wait()

    def close(self):
        for name in reversed(list(self.processes)):
            self.stop(name)
        for out in self.handles:
            out.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=['a', 'b', 'test'])
    parser.add_argument('--central', type=int)
    parser.add_argument('--local-a', type=int)
    parser.add_argument('--local-b', type=int)
    parser.add_argument('--logs')
    parser.add_argument('--prefix', default='pqc/prototype')
    args = parser.parse_args()
    reservations = []
    for name, default in [('central', 7447), ('local_a', 7448), ('local_b', 7449)]:
        if getattr(args, name) is None:
            if args.mode == 'test':
                sock = socket.socket()
                sock.bind(('127.0.0.1', 0))
                reservations.append(sock)
                setattr(args, name, sock.getsockname()[1])
            else:
                setattr(args, name, default)
    for sock in reservations:
        sock.close()
    ports = [args.central, args.local_a, args.local_b]
    if len(set(ports)) != 3 or not all(0 < p < 65536 for p in ports):
        parser.error('Informe tres portas TCP distintas entre 1 e 65535.')
    args.endpoint = f'tcp/127.0.0.1:{args.central}'
    if args.mode == 'test':
        args.prefix += '/' + uuid.uuid4().hex[:12]
    demo = Demo(args)
    try:
        demo.run()
    except KeyboardInterrupt:
        pass
    except Exception as error:
        print('FAIL:', error, flush=True)
        raise
    finally:
        demo.close()


if __name__ == '__main__':
    main()
