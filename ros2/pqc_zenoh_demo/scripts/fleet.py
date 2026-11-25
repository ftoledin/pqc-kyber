#!/usr/bin/env python3
"""Local central-and-drones simulation over independent encrypted Zenoh links."""
import argparse
import codecs
from datetime import datetime
import hashlib
import json
import re
import runpy
import socket
import subprocess
import time
import uuid
from pathlib import Path
from zoneinfo import ZoneInfo

# Reuse process management, incremental log reading and SIGINT cleanup.
directory = Path(__file__).resolve().parent
Demo = runpy.run_path(str(directory / ('demo.py' if (directory / 'demo.py').exists() else 'demo')))['Demo']


def drone_index(identity):
    if len(identity) == 1 and 'B' <= identity <= 'Z':
        return ord(identity) - ord('B')
    return 25 + int.from_bytes(hashlib.sha256(identity.encode()).digest()[:2], 'big') % 1000


class FleetConsole:
    """Presentation only. Original process logs retain the complete crypto evidence."""
    def __init__(self, args, clock=time.time):
        self.args = args
        self.clock = clock
        self.zone = ZoneInfo('America/Sao_Paulo')
        self.packets = {}
        self.registered = []
        self.ready = False

    def format(self, name, line):
        line = re.sub(r'\x1b\[[0-9;]*m', '', line)
        stamp = self.clock()
        severity = ''
        match = re.match(r'^\[(DEBUG|INFO|WARN|ERROR|FATAL)\]\s+\[(\d+(?:\.\d+)?)\]\s+\[[^]]+\]:\s*(.*)$', line)
        if match:
            severity, epoch, line = match.groups()
            stamp = float(epoch)
        text = self.message(name, line, severity)
        if text is None:
            return None
        clock = datetime.fromtimestamp(stamp, self.zone).strftime('%H:%M:%S')
        rows = text if isinstance(text, list) else [text]
        return '\n'.join('[' + clock + '] ' + row for row in rows)

    def message(self, name, line, severity):
        if self.args.verbose:
            return '[' + name + '] ' + line
        header = re.search(r'CRYPTO (TX|RX) channel=(\S+) type=(\d+) id=(\d+) seq=(\d+)', line)
        if header:
            side, channel, kind, identity, seq = header.groups()
            size = re.search(r'packet_bytes=(\d+)', line)
            self.packets[(side, channel, seq)] = dict(kind=int(kind), identity=int(identity),
                                                    size=int(size[1]) if size else None)
            return None
        digest = re.search(r'CRYPTO HASH (TX|RX) channel=(\S+) seq=(\d+) (?:value|received)=([0-9a-f]{64})', line)
        if digest:
            side, channel, seq, value = digest.groups()
            packet = self.packets.get((side, channel, seq))
            if packet is not None:
                packet['hash'] = value
                calculated = re.search(r'calculated=([0-9a-f]{64})', line)
                if calculated:
                    packet['calculated'] = calculated[1]
            return None
        cipher = re.search(r'CRYPTO CIPHER (TX|RX) channel=(\S+) seq=(\d+) AES-256-GCM=([0-9a-f.]+)', line)
        if cipher:
            side, channel, seq, value = cipher.groups()
            packet = self.packets.pop((side, channel, seq), {})
            kind, identity = packet.get('kind', 0), packet.get('identity', 0)
            # One command out and one final position back; don't repeat result packets.
            if not self.args.show_crypto or not (kind in (3, 9, 10) or (kind == 2 and identity)):
                return None
            parts = channel.rsplit('/', 3)
            if len(parts) != 4:
                return None
            drone, role = parts[1:3]
            sender, receiver = ('A', drone) if role == 'a' else (drone, 'A')
            direction = 'ENVIO CIFRADO' if side == 'TX' else 'RECEBIMENTO CIFRADO'
            sample = value[:32] + ('...' if len(value) > 32 else '')
            details = []
            if packet.get('size') is not None:
                details.append(str(packet['size']) + ' bytes')
            if packet.get('hash'):
                details.append('hash=' + packet['hash'][:8] + '...')
            details.append('cifra=' + sample)
            prefix = sender + ' → ' + receiver + ' | '
            summary = prefix + direction + ' | ' + ' | '.join(details)
            if side == 'RX' and packet.get('hash') and packet.get('calculated'):
                equal = packet['hash'] == packet['calculated']
                return [summary, prefix + 'Hash recebido: ' + packet['hash'],
                        prefix + 'Hash calculado: ' + packet['calculated'] + ' | ' + ('IGUAIS' if equal else 'DIFERENTES')]
            return summary
        tamper = re.search(r'^TAMPER REQUEST drone=(\S+) id=\d+ target=(\([^)]*\))', line)
        if tamper:
            return 'A → ' + tamper[1] + ' | TESTE DE ADULTERAÇÃO | goto ' + tamper[2]
        if line.startswith('TAMPER TAG '):
            return None  # Actual before/after tag byte remains available in --full and logs.
        tampered = re.search(r'^TAMPER SENT drone=(\S+) ', line)
        if tampered:
            return 'A → ' + tampered[1] + ' | Tag alterada em 1 bit; confira a rejeição no drone e use pos ' + tampered[1]
        if line.startswith('PACOTE REJEITADO: Autenticacao AES-GCM falhou:'):
            return 'PACOTE REJEITADO | autenticação AES-GCM falhou; comando não executado'
        if line.startswith('CRYPTO '):
            if line.startswith('CRYPTO INPUT '):
                request = re.search(r'drone=(\S+) id=\d+ goto=(\([^)]*\))', line)
                if request:
                    return 'A → ' + request[1] + ' | Comando: goto ' + request[2]
            return None
        registered = re.search(r'DRONE REGISTERED id=(\S+)', line)
        if registered:
            identity = registered[1].rstrip(';')
            self.registered.append(identity)
            return 'Drone ' + identity + ' cadastrado.' if self.ready else None
        if line.startswith('CENTRAL READY'):
            self.ready = True
            return 'Central A pronta | drones: ' + ', '.join(self.registered) + ' | horário de Brasília | use help'
        ready = re.search(r'DRONE READY id=(\S+)', line)
        if ready:
            return 'Drone ' + ready[1].rstrip(';') + ' pronto | horário de Brasília'
        connected = re.search(r'DRONE CONNECTED id=(\S+)', line)
        if connected:
            return 'Drone ' + connected[1] + ' conectado.'
        request = re.search(r'^POSITION REQUEST drone=(\S+) request=(\d+)', line)
        if request:
            return 'A → ' + request[1] + ' | Consulta: pos | pedido ' + request[2]
        query_received = re.search(r'POSITION REQUEST RECEIVED drone=(\S+) request=(\d+)', line)
        if query_received:
            return 'A → ' + query_received[1] + ' | Pedido recuperado: pos | AES-GCM OK; hashes iguais'
        response = re.search(r'POSITION RESPONSE drone=(\S+) request=\d+ xyz=(\([^)]*\))', line)
        if response:
            return response[1] + ' → A | Posição consultada: ' + response[2] + ' | AES-GCM OK; hashes iguais'
        cached = re.search(r'STATUS CACHE drone=(\S+) state=(\S+) known=(\d) xyz=(\([^)]*\))', line)
        if cached:
            identity, state, known, position = cached.groups()
            return 'Cache de ' + identity + ' | ' + state + ' | posição: ' + (position if known == '1' else 'desconhecida')
        request = re.search(r'GOTO SENT drone=(\S+) id=\d+ target=(\([^)]*\))', line)
        if request:
            return None if self.args.show_crypto else 'A → ' + request[1] + ' | Comando: goto ' + request[2]
        accepted = re.search(r'GOTO ACCEPTED drone=(\S+) id=\d+ target=(\([^)]*\))', line)
        if accepted:
            return 'A → ' + accepted[1] + ' | Comando recuperado: goto ' + accepted[2] + ' | AES-GCM OK; hashes iguais'
        result = re.search(r'GOTO RESULT drone=(\S+) id=\d+ status=(\S+) position=(\([^)]*\))', line)
        if result:
            identity, status, position = result.groups()
            if name == 'central':
                outcome = {'REACHED': 'concluído', 'CANCELED': 'cancelado', 'LINK_LOST': 'enlace perdido'}.get(status, status)
                return identity + ' → A | Posição recuperada: ' + position + ' | AES-GCM OK; hashes iguais | ' + outcome
            if status == 'REACHED' and self.args.show_crypto:
                return None
            return 'Drone ' + identity + ' | posição: ' + position + ' | ' + status
        if line.startswith('POSITION RECOVERED') or line.startswith('GOTO ACK'):
            if 'accepted=0' in line:
                return line
            return None
        if line.startswith('GOTO PROGRESS'):
            progress = re.search(r'drone=(\S+) position=(\([^)]*\)) remaining_m=(\S+)', line)
            return 'Drone ' + progress[1] + ' | posição: ' + progress[2] + ' | faltam ' + progress[3] + ' m' if progress else line
        if line.startswith('CANCEL ACK'):
            return None
        if line.startswith('COMMAND ERROR:'):
            return 'Comando inválido: ' + line.partition(':')[2].strip()
        if line.startswith('PROTOTIPO:'):
            return 'Demo local: chaves aceitas no primeiro uso, sem identidade certificada.'
        if name.startswith('router') and severity not in ('WARN', 'ERROR', 'FATAL') and not re.search(r'\b(ERROR|WARN)\b', line):
            return None
        if severity in ('WARN', 'ERROR', 'FATAL'):
            return ('AVISO: ' if severity == 'WARN' else 'ERRO: ') + line
        return line


class Fleet(Demo):
    def __init__(self, args):
        args.local_a = args.local_port if args.mode == 'central' and args.local_port else 7448
        args.local_b = args.local_port if args.mode == 'drone' and args.local_port else 7449 + drone_index(args.id)
        super().__init__(args)
        self.role_env = {}
        self.console = FleetConsole(args)
        self.decoders = {}
        self.pending_lines = {}

    def environment(self, identity, port, domain):
        session = self.config('session_' + identity, dict(mode='client',
            connect=dict(endpoints=[f'tcp/127.0.0.1:{port}']),
            scouting=dict(multicast=dict(enabled=False), gossip=dict(enabled=False))))
        env = dict(self.envs['a'], ROS_DOMAIN_ID=str(domain), ZENOH_SESSION_CONFIG_URI=str(session))
        # Configurations belong to this launch; don't inherit an older experiment's overrides.
        env.pop('ZENOH_CONFIG_OVERRIDE', None)
        env.pop('ZENOH_ROUTER_CONFIG_URI', None)
        env['RCUTILS_CONSOLE_OUTPUT_FORMAT'] = '[{severity}] [{time}] [{name}]: {message}'
        env['RCUTILS_COLORIZED_OUTPUT'] = '0'
        self.role_env[identity] = env
        return env

    def start_router(self, name, port):
        # Fail on occupied ports instead of accidentally attaching to another demo's router.
        with socket.socket() as check:
            check.bind(('127.0.0.1', port))
        super().start_router(name, port)
        self.stopped.discard(name)
        if self.processes[name].poll() is not None:
            raise RuntimeError(self.log(name))

    def node(self, name, role, port, domain, identity='B', *, pipe=False, motion=False):
        self.start_router('router_' + name, port)
        env = self.environment(name, port, domain)
        params = [self.bin / 'fleet_node', '--ros-args', '-p', 'role:=' + role,
                  '-p', 'drone_id:=' + json.dumps(identity), '-p', 'endpoint:=' + json.dumps(self.args.endpoint),
                  '-p', 'prefix:=' + json.dumps(self.prefix), '-p', 'verbose:=' + str(self.args.verbose).lower(),
                  '-p', 'show_crypto:=' + str(self.args.show_crypto).lower(),
                  '-p', 'instant_goto:=' + str(not motion).lower()]
        if role == 'central':
            params += ['-p', 'drone_ids:=' + json.dumps(self.args.drones)]
        return self.spawn(name, params, env, stdin=subprocess.PIPE if pipe else None)

    def start_central(self, port, *, pipe=False):
        self.start_router('router_transport', self.args.central)
        self.node('central', 'central', port, 61, pipe=pipe)

    def send(self, command):
        process = self.processes['central']
        process.stdin.write((command + '\n').encode())
        process.stdin.flush()

    def wait_log(self, name, text, timeout=15):
        self.wait(lambda: text in self.log(name), timeout)

    def show(self):
        if self.args.mode == 'test':
            return
        for name in self.processes:
            with (self.directory / (name + '.log')).open('rb') as stream:
                stream.seek(self.offsets.get(name, 0))
                chunk = stream.read()
                self.offsets[name] = stream.tell()
            decoder = self.decoders.setdefault(name, codecs.getincrementaldecoder('utf-8')(errors='replace'))
            text = self.pending_lines.get(name, '') + decoder.decode(chunk)
            parts = text.split('\n')
            self.pending_lines[name] = parts.pop()
            for raw in parts:
                formatted = self.console.format(name, raw.rstrip('\r'))
                if formatted:
                    print(formatted, flush=True)

    def run_test(self):
        self.args.show_crypto = True
        # Reserve distinct ephemeral ports until all choices have been made.
        reservations = []
        for _ in range(5):
            sock = socket.socket();sock.bind(('127.0.0.1', 0));reservations.append(sock)
        ports = [s.getsockname()[1] for s in reservations]
        for s in reservations:
            s.close()
        self.args.central = ports[0]
        self.args.endpoint = f'tcp/127.0.0.1:{ports[0]}'
        self.prefix += '/' + uuid.uuid4().hex[:12]
        self.start_central(ports[1], pipe=True)
        self.node('drone_B', 'drone', ports[2], 62, 'b')
        self.node('drone_C', 'drone', ports[3], 63, 'C', motion=True)
        for identity in ('B', 'C'):
            self.wait_log('central', 'DRONE CONNECTED id=' + identity)
        self.send('goto B 2 0 1')
        self.wait_log('central', 'GOTO RESULT drone=B id=1 status=REACHED')
        presentation_lines = {}
        for name, expected_count in [('central', 6), ('drone_B', 5)]:
            console = FleetConsole(argparse.Namespace(verbose=False, show_crypto=True))
            rendered = []
            for raw in self.log(name).splitlines():
                line = console.format(name, raw)
                if line and ' → ' in line:
                    rendered.extend(line.splitlines())
                if 'GOTO RESULT drone=B id=1 status=REACHED' in raw:
                    break
            assert len(rendered) == expected_count, rendered
            assert all(re.match(r'^\[\d{2}:\d{2}:\d{2}\] ', line) for line in rendered)
            assert all(not any(field in line for field in ('channel=', 'CRYPTO HASH', 'CRYPTO PLAIN')) for line in rendered)
            received = next(re.search(r'Hash recebido: ([0-9a-f]{64})', line)[1] for line in rendered if 'Hash recebido:' in line)
            calculated = next(re.search(r'Hash calculado: ([0-9a-f]{64})', line)[1] for line in rendered if 'Hash calculado:' in line)
            assert received == calculated and any('| IGUAIS' in line for line in rendered)
            presentation_lines[name] = rendered
        assert 'GOTO ACCEPTED' not in self.log('drone_C')
        self.send('pos b');self.send('POS c')
        self.wait_log('central', 'POSITION RESPONSE drone=B request=1 xyz=(2.00,0.00,1.00)')
        self.wait_log('central', 'POSITION RESPONSE drone=C request=1 xyz=(0.00,0.00,0.00)')
        for identity in ('B', 'C'):
            log = self.log('drone_' + identity)
            assert 'POSITION REQUEST RECEIVED drone=' + identity + ' request=1' in log
            assert re.search(r'CRYPTO RX .*type=9 id=1 ', log)
            assert re.search(r'CRYPTO TX .*type=10 id=1 ', log)
        requests_before = self.log('central').count('POSITION REQUEST drone=B ')
        self.send('status B')
        self.wait_log('central', 'STATUS CACHE drone=B state=ONLINE known=1 xyz=(2,0,1)')
        assert self.log('central').count('POSITION REQUEST drone=B ') == requests_before
        immediate_start = time.monotonic()
        self.send('GoTo b 100 100 50');self.send('goto C -2 1 2')
        self.wait_log('central', 'GOTO RESULT drone=B id=2 status=REACHED', 3)
        immediate_seconds = time.monotonic() - immediate_start
        self.wait_log('central', 'GOTO RESULT drone=C id=1 status=REACHED')
        self.send('goto B nan 0 1');self.send('goto X 1 1 1');self.send('goto B 0 0 -1')
        self.send('tamper B nan 0 1');self.send('tamper X 1 1 1');self.send('tamper B 0 0 -1')
        self.wait(lambda: self.log('central').count('COMMAND ERROR:') >= 6, 5)
        assert 'TAMPER TAG' not in self.log('central')
        assert self.log('drone_B').count('GOTO ACCEPTED') == 2
        self.send('goto C 90 0 5')
        self.wait_log('drone_C', 'GOTO ACCEPTED drone=C id=2')
        self.send('tamper C 1 2 3')
        self.wait_log('central', 'TAMPER REJECTED drone=C: Drone ocupado')
        self.send('pos C')
        self.wait_log('central', 'POSITION RESPONSE drone=C request=2 ')
        self.send('cancel C')
        self.wait_log('central', 'GOTO RESULT drone=C id=2 status=CANCELED')
        self.send('ADD d')
        self.wait_log('central', 'DRONE REGISTERED id=D')
        self.node('drone_D', 'drone', ports[4], 64, 'D')
        self.wait_log('central', 'DRONE CONNECTED id=D')
        self.send('goto D 1 1 1')
        self.wait_log('central', 'GOTO RESULT drone=D id=1 status=REACHED')
        counts_before_tamper = {name: self.log(name).count('GOTO ACCEPTED')
                                for name in ('drone_B', 'drone_C', 'drone_D')}
        self.send('TaMpEr b 7 8 9')
        self.wait_log('central', 'TAMPER SENT drone=B id=3')
        self.wait_log('drone_B', 'PACOTE REJEITADO: Autenticacao AES-GCM falhou: pacote rejeitado')
        self.send('pos B')
        self.wait_log('central', 'POSITION RESPONSE drone=B request=2 xyz=(100.00,100.00,50.00)')
        assert counts_before_tamper == {name: self.log(name).count('GOTO ACCEPTED')
                                       for name in counts_before_tamper}
        assert 'GOTO ACK drone=B id=3' not in self.log('central')
        changed_tag = re.search(r'TAMPER TAG channel=(\S+) seq=(\d+) last_byte_before=([0-9a-f]{2}) last_byte_after=([0-9a-f]{2}) xor_mask=01', self.log('central'))
        assert changed_tag and int(changed_tag[3], 16) ^ int(changed_tag[4], 16) == 1
        tamper_presentation = {}
        for name in ('central', 'drone_B'):
            console = FleetConsole(argparse.Namespace(verbose=False, show_crypto=True))
            lines = [console.format(name, raw) for raw in self.log(name).splitlines()]
            tamper_presentation[name] = [line for line in lines if line and
                                         ('ADULTERAÇÃO' in line or 'Tag alterada' in line or 'PACOTE REJEITADO' in line)]
        assert len(tamper_presentation['central']) == 2
        assert len(tamper_presentation['drone_B']) == 1
        # The failed packet must not block subsequent valid sequence numbers or goals.
        self.send('goto B 4 5 6')
        self.wait_log('central', 'GOTO RESULT drone=B id=4 status=REACHED')
        self.send('pos B')
        self.wait_log('central', 'POSITION RESPONSE drone=B request=3 xyz=(4.00,5.00,6.00)')
        self.send('goto C 80 1 5')
        self.wait_log('drone_C', 'GOTO ACCEPTED drone=C id=3')
        self.stop('router_transport')
        self.wait_log('drone_C', 'status=LINK_LOST', 10)
        self.send('list')
        self.wait_log('central', 'DRONE C OFFLINE')
        counts = {name: self.log(name).count('GOTO ACCEPTED') for name in ('drone_B', 'drone_C', 'drone_D')}
        self.send('goto D 2 2 2')
        self.wait_log('central', 'GOTO REJECTED drone=D: Drone sem telemetria recente')
        self.send('tamper D 2 2 2')
        self.wait_log('central', 'TAMPER REJECTED drone=D: Drone sem telemetria recente')
        self.send('pos d')
        self.wait_log('central', 'POSITION TIMEOUT drone=D request=1', 6)
        assert 'POSITION RESPONSE drone=D' not in self.log('central')
        time.sleep(.5)
        assert counts == {name: self.log(name).count('GOTO ACCEPTED') for name in counts}
        tx_hashes, rx_hashes, tx_cipher, rx_cipher, tx_plain, rx_plain = {}, {}, {}, {}, {}, {}
        for name in ('central', 'drone_B', 'drone_C', 'drone_D'):
            content = self.log(name)
            for channel, seq, digest in re.findall(r'CRYPTO HASH TX channel=(\S+) seq=(\d+) value=([0-9a-f]{64})', content):
                tx_hashes[(channel, seq)] = digest
            for channel, seq, received, calculated in re.findall(r'CRYPTO HASH RX channel=(\S+) seq=(\d+) received=([0-9a-f]{64}) calculated=([0-9a-f]{64}) equal=true', content):
                assert received == calculated
                rx_hashes[(channel, seq)] = received
            for side, table in [('TX', tx_cipher), ('RX', rx_cipher)]:
                for channel, seq, value in re.findall(r'CRYPTO CIPHER '+side+r' channel=(\S+) seq=(\d+) AES-256-GCM=([0-9a-f.]+)', content):
                    table[(channel, seq)] = value
            for side, table in [('TX', tx_plain), ('RX', rx_plain)]:
                for channel, seq, value in re.findall(r'CRYPTO PLAIN '+side+r' channel=(\S+) seq=(\d+) envelope_CDR_hex=([0-9a-f.]+)', content):
                    table[(channel, seq)] = value
        assert len(rx_hashes) >= 9
        assert (changed_tag[1], changed_tag[2]) in tx_hashes
        assert (changed_tag[1], changed_tag[2]) not in rx_hashes
        for key, digest in rx_hashes.items():
            assert tx_hashes[key] == digest
            assert tx_cipher[key] == rx_cipher[key] and tx_cipher[key] != tx_plain[key]
            assert tx_plain[key] == rx_plain[key]
        result = dict(two_initial_drones=True, target_isolation=True, positions_recovered=True,
                      concurrent_goto=True, invalid_coordinates_rejected=True, unknown_id_rejected=True,
                      cancellation=True, dynamic_addition=True, simulated_hold_on_link_loss=True,
                      offline_command_rejected=True, core_crypto_modified=False, units='metres',
                      instant_goto=True, instant_long_goto_seconds=round(immediate_seconds, 3),
                      crypto_trace_pairs=len(rx_hashes), crypto_hashes_match=True,
                      ciphertext_matches_at_receiver=True, plaintext_recovered=True,
                      remote_position_request_reply=True, cached_status_is_explicit=True,
                      position_query_during_goto=True, position_timeout_without_cached_fallback=True,
                      pos_command=True, commands_and_drone_ids_case_insensitive=True,
                      received_and_calculated_hash_visible=True,
                      interactive_tamper=True, tamper_changes_one_tag_bit=True,
                      tampered_packet_rejected_by_aes_gcm=True,
                      tampered_goal_not_acknowledged=True, tamper_does_not_change_position=True,
                      valid_command_after_tamper=True, tamper_busy_and_offline_rejected=True,
                      tamper_invalid_coordinates_and_unknown_id_rejected=True,
                      tamper_console_example=tamper_presentation,
                      console_timezone='America/Sao_Paulo',
                      console_lines_per_goto={'central': 6, 'drone': 5},
                      console_example=presentation_lines,
                      frame='map', local_only=True, logs=str(self.directory))
        (self.directory / 'results.json').write_text(json.dumps(result, indent=2) + '\n')
        print('PASS:', json.dumps(result), flush=True)

    def run(self):
        print('Logs:', self.directory, flush=True)
        if self.args.mode == 'test':
            self.run_test()
            return
        if self.args.mode == 'central':
            self.start_central(self.args.local_a)
        else:
            domain = 62 + drone_index(self.args.id) % 150
            self.node('drone_' + self.args.id, 'drone', self.args.local_b, domain, self.args.id,
                      motion=self.args.simulate_motion)
        while True:
            self.show()
            workers = [p for name, p in self.processes.items() if not name.startswith('router')]
            if any(p.poll() is not None for p in workers):
                if any(p.returncode != 0 for p in workers if p.poll() is not None):
                    raise RuntimeError('Processo encerrou; consulte ' + str(self.directory))
                return
            for name, p in self.processes.items():
                if name.startswith('router') and p.poll() is not None:
                    raise RuntimeError(name + ' encerrou; consulte os logs')
            time.sleep(.1)

    def close(self):
        for p in self.processes.values():
            if p.stdin:
                p.stdin.close()
        super().close()

    def wait(self, predicate, timeout):
        def checked():
            for name, process in self.processes.items():
                if name not in self.stopped and process.poll() is not None:
                    raise RuntimeError(name + ' encerrou: ' + self.log(name))
            return predicate()
        super().wait(checked, timeout)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', type=str.lower, choices=['central', 'drone', 'test'])
    parser.add_argument('id', type=str.upper, nargs='?', default='B')
    parser.add_argument('--drones', type=str.upper, nargs='+', default=['B', 'C'], help='IDs iniciais opcionais (padrao: B C); use add ID para outros')
    parser.add_argument('--central', type=int, default=7447)
    parser.add_argument('--local-port', type=int)
    parser.add_argument('--logs')
    parser.add_argument('--prefix', default='pqc/fleet')
    parser.add_argument('--full', dest='verbose', action='store_true', help='Modo completo: todos os logs, hashes e bytes do envelope')
    parser.add_argument('--verbose', dest='verbose', action='store_true', help=argparse.SUPPRESS)
    parser.add_argument('--show-crypto', action='store_true', help='Mostra comando cifrado e retorno cifrado de forma resumida')
    parser.add_argument('--simulate-motion', action='store_true', help='Optional gradual simulated movement on a drone; default goto is immediate')
    args = parser.parse_args()
    if args.verbose:
        args.show_crypto = True
    for identity in [args.id] + args.drones:
        if not re.fullmatch(r'[A-Za-z][A-Za-z0-9_]{0,23}', identity) or identity == 'A':
            parser.error('IDs: letras, numeros e _, ate 24 caracteres; A e reservado para a central.')
    if len(set(args.drones)) != len(args.drones) or len(args.drones) > 32:
        parser.error('Informe ate 32 IDs de drones distintos.')
    if not 1 <= args.central <= 65535 or (args.local_port is not None and not 1 <= args.local_port <= 65535):
        parser.error('Portas devem estar entre 1 e 65535.')
    if args.mode == 'test':
        args.drones = ['B', 'C']
    args.endpoint = f'tcp/127.0.0.1:{args.central}'
    fleet = Fleet(args)
    try:
        fleet.run()
    except KeyboardInterrupt:
        pass
    finally:
        fleet.close()


if __name__ == '__main__':
    main()
