# PQC-Kyber — criptografia e testes

Esta branch contém o núcleo Kyber em C, hashing caótico e testes independentes do ROS2. Os cabeçalhos permitem ligação com aplicações C++ por `extern "C"`.

| Branch | Conteúdo |
| --- | --- |
| [`crypto-only`](https://github.com/ftoledin/pqc-kyber/tree/crypto-only) | Biblioteca criptográfica, cabeçalhos C/C++ e testes |
| [`zenoh-demo`](https://github.com/ftoledin/pqc-kyber/tree/zenoh-demo) | Mesmo núcleo, mais simulação ROS2/Zenoh e documentação |

## Compilar e executar os testes

No Ubuntu, instale `build-essential` e `python3`. Na raiz do repositório:

```bash
make -C ref -j2 check
```

O comando compila e executa dez testes: hashing, rejeição de alterações, acordo do segredo e vetores internos nas variantes Kyber512, Kyber768 e Kyber1024. Retorna falha se algum teste reprovar. `make -C ref test` apenas compila os executáveis; não executa a bateria.

Para gerar as três bibliotecas compartilhadas:

```bash
make -C ref -j2 shared
```

Elas são criadas em `ref/lib/`. Os arquivos de `ref/test/` incluem referência Python independente e ferramentas de auditoria e amostragem. Para compilar também os benchmarks e o gerador KAT interno, instale `libssl-dev` e execute `make -C ref -j2 all`.

## Organização

- `ref/`: implementação Kyber, adaptadores, Makefile e testes.
- `hash_options/`: núcleo do hashing caótico.
- `ref/test/results/crypto_only_20261124.json`: validação desta separação de branches.

O núcleo atual utiliza ponto fixo Q32.32, xorshift 13/17/5 e codificação CHF1 para diferenciar comprimento e finalidade. É o mesmo núcleo utilizado na branch Zenoh. A separação não altera algoritmos, parâmetros ou resultados criptográficos.

Esta é uma construção experimental com hash caótico, não uma implementação certificada de ML-KEM. Testes aprovados demonstram funcionamento nos casos exercitados; não comprovam segurança contra ataques nem conformidade com vetores oficiais. Os nomes históricos SHA3 dos wrappers não significam que o SHA-3 padrão esteja sendo usado.

Os artefatos gerados não são versionados. Preserve-os para aproveitar compilações incrementais; `make -C ref clean` os remove quando necessário.
