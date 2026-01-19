# MyFS - Sistema de Ficheiros Simples

Este projeto consiste na implementação de um sistema de ficheiros (File System) simples, denominado **MyFS**, para um sistema operativo hipotético. O projeto foi desenvolvido no âmbito da disciplina de **Sistemas Operacionais**.

## Autores
* **Lara Dias**
* **Sarah Cristina**
* **Willian Santos**

## Estrutura do Projeto

O código-fonte está dividido nos seguintes módulos:

* **`myfs.c / myfs.h`**: O núcleo do projeto. Contém a implementação das funções do sistema de ficheiros (formatação, montagem, abertura, leitura, escrita, etc.).
* **`vfs.c / vfs.h`**: Interface do Sistema de Ficheiros Virtual (VFS) que abstrai as chamadas para o SO.
* **`inode.c / inode.h`**: API para manipulação de i-nodes (index nodes), responsáveis por guardar metadados dos ficheiros.
* **`disk.c / disk.h`**: Emulador de disco físico, permitindo leitura e escrita em setores.
* **`util.c / util.h`**: Funções utilitárias de conversão de dados.
* **`main.c`**: Simulador interativo (CLI) para testar o sistema manualmente.
* **`test_suite.c`**: Script de teste automatizado para validação de todas as funcionalidades.

## Funcionalidades Implementadas

O **MyFS** suporta as seguintes operações:

1.  **Gestão do Sistema:**
    * **Formatação (`myFSFormat`)**: Inicializa o disco, cria o Superbloco, o mapa de bits (bitmap) e o diretório raiz.
    * **Montagem/Desmontagem (`myFSxMount`)**: Carrega metadados do disco para a memória e vice-versa.
    * **Verificação de Ociosidade (`myFSIsIdle`)**: Impede o desmonte se houver ficheiros abertos.

2.  **Operações sobre Ficheiros:**
    * **Abrir (`myFSOpen`)**: Localiza um ficheiro pelo nome ou cria um novo se não existir.
    * **Ler (`myFSRead`)**: Lê bytes do ficheiro para um buffer.
    * **Escrever (`myFSWrite`)**: Escreve dados no ficheiro, alocando novos blocos de disco conforme necessário.
    * **Fechar (`myFSClose`)**: Liberta o descritor de ficheiro.

3.  **Operações sobre Diretórios:**
    * **Listar (`myFSReadDir`)**: Itera sobre os ficheiros presentes na diretoria raiz.
    * **Links (`myFSLink`)**: Cria hard links (nomes alternativos) para ficheiros existentes.
    * **Remover (`myFSUnlink`)**: Remove uma entrada do diretório (e o ficheiro, se for o último link).

## Como Compilar

Para compilar o projeto, é necessário ter o compilador `gcc` instalado.

### 1. Compilar o Simulador Interativo
Este é o programa principal fornecido pelo professor para testes manuais.

```bash
gcc main.c myfs.c vfs.c inode.c disk.c util.c -o simulador
```
### 2. Compilar o Script de Testes Automatizados
Este script executa um ciclo completo de operações para validar a robustez do código.

```bash
gcc test_suite.c myfs.c vfs.c inode.c disk.c util.c -o teste_auto
```

## Como Executar
### Modo Interativo (Manual)
Permite controlar o SO hipotético via menus.
```bash
./simulador
```

### Modo Automático (Recomendado para Validação) 
Executa uma bateria de testes (Criação de disco, Formatação, Escrita, Leitura, Links e Remoção).

```bash
./teste_auto
```

## Roteiro de utilização no simulador

1.  **D (Disk Operations) -> B (Build):** Crie um disco físico (ex: `disco.dsk`, 100 cilindros).
2.  **C (Connect):** Conecte o disco criado (`disco.dsk`). Volte (`<`).
3.  **F (Filesystem) -> F (Format):**
    * **Disk ID:** `0`
    * **FS ID:** `77` (Código ASCII para 'M')
    * **Block Size:** `1`
4.  **M (Mount):** Monte o sistema (Disk `0`, FS `77`).
5.  **I (File Ops):** Use **O** (Open), **W** (Write), **R** (Read) para manipular ficheiros.

## Detalhes de Implementação

* **Superbloco:** Localizado no setor 0. Guarda o "número mágico" (`0x4D794653`), tamanho do bloco, total de blocos e ponteiros para áreas de dados.
* **Bitmap:** Localizado no setor 1. Gere a alocação de blocos livres no disco.
* **Inode Raiz:** O inode número 1 é reservado para a diretoria raiz (`/`).
* **Diretoria:** O MyFS possui uma estrutura de diretoria plana (*flat directory*). Não suporta subdiretorias reais além da raiz.
* **Persistência:** Todas as operações de escrita (`myFSWrite`, `myFSLink`, criação de ficheiros) forçam a atualização imediata dos i-nodes e blocos de dados no disco virtual para garantir consistência.

## Limitações Conhecidas

* O sistema suporta apenas blocos de **512 bytes**.
* Não é possível criar subdiretorias (apenas ficheiros na raiz `/`).
* O tamanho máximo do nome de ficheiro é **14 caracteres**.