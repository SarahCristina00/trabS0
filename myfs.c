/*
*  myfs.c - Implementacao do sistema de arquivos MyFS
*
*  Autores: Lara dias - 202376010, Sarah Cristina - 202376034, Willian Santos
*  Projeto: Trabalho Pratico II - Sistemas Operacionais
*  Organizacao: Universidade Federal de Juiz de Fora
*  Departamento: Dep. Ciencia da Computacao
*
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "myfs.h"
#include "vfs.h"
#include "inode.h"
#include "util.h"
#include "disk.h"

//Declaracoes globais
//...
//...
#define MYFS 0x4D794653          // assinatura do nosso SF
#define MAX_FILENAME_LEN 14                // Comprimento máximo do nome de um arquivo
#define DIR_ENTRY_SIZE 16                  // Tamanho da entrada de diretório (2+14 bytes)
#define MAX_OPEN_FILES 20                  // Máximo de arquivos/diretórios abertos
#define ROOT_INODE_NUM 1                   // I-node do diretório raiz (sempre 1)
#define INODE_TYPE_REGULAR 1               // Tipo de i-node: arquivo regular
#define INODE_TYPE_DIRECTORY 2             // Tipo de i-node: diretório

// ================= Estruturas de dados ===============

// Estrutura do superbloco (bloco 0 do disco)
typedef struct {
    unsigned int magic_number;             // Assinatura do nosso sistema de arquivos
    unsigned int block_size;               // Tamanho do bloco (sempre 512)
    unsigned int total_blocks;             // Número total de blocos
    unsigned int inode_start_block;        // Bloco onde os i-nodes começam
    unsigned int inode_count;              // Quantos i-nodes existem
    unsigned int data_start_block;         // Primeiro bloco da área de dados
    unsigned int free_blocks;              // Blocos disponíveis
    unsigned int root_inode;               // I-node do diretório raiz
} superblock_t;

// Entrada de diretório (16 bytes: 2 + 14)
typedef struct {
    unsigned short inode_number;           // Número do i-node (2 bytes)
    char filename[MAX_FILENAME_LEN];       // Nome do arquivo (14 bytes)
} dir_entry_t;

// Controle de arquivo/diretório aberto
typedef struct {
    int is_used;                           // 1=aberto, 0=fechado
    unsigned int inode_number;             // I-node associado
    unsigned int current_position;         // Posição no arquivo (cursor)
    int is_directory;                      // 1=diretorio, 0=arquivo
    unsigned int dir_read_position;        // Posição na leitura do diretório
} open_file_t;

// ================= Variáveis globais ===============

static superblock_t sb_cache;              // Cópia do superbloco em memória
static int fs_mounted = 0;                 // 1=montado, 0=não montado
static unsigned char *block_bitmap = NULL; // Mapa de bits (1 bit por bloco)
static open_file_t open_files_table[MAX_OPEN_FILES]; // Tabela de arquivos abertos

// ================= Funções auxiliares ===============

// Encontra um bloco livre no mapa de bits
// Retorna o número do bloco encontrado ou -1 se não houver blocos livres
static int find_free_block(Disk *d) {
    unsigned int total_blocks = sb_cache.total_blocks;
    
    // Percorre a partir da área de dados
    for (unsigned int block_num = sb_cache.data_start_block; block_num < total_blocks; block_num++) {
        int byte_index = block_num / 8;      // Qual byte no bitmap
        int bit_index = block_num % 8;       // Qual bit dentro do byte
        
        // Verifica se o bit está livre (0)
        if ((block_bitmap[byte_index] & (1 << bit_index)) == 0) {
            // Marca o bloco como ocupado
            block_bitmap[byte_index] |= (1 << bit_index);
            sb_cache.free_blocks--;
            
            // Salva o bitmap atualizado no disco (bloco 1)
            unsigned char block_buffer[512];
            memcpy(block_buffer, block_bitmap, 512);
            diskWriteSector(d, 1, block_buffer);
            
            // Salva o superbloco atualizado no disco (bloco 0)
            unsigned char superblock_buffer[512];
            memset(superblock_buffer, 0, 512);
            
            unsigned int buffer_pos = 0;
            ul2char(sb_cache.magic_number, &superblock_buffer[buffer_pos]); buffer_pos += 4;
            ul2char(sb_cache.block_size, &superblock_buffer[buffer_pos]); buffer_pos += 4;
            ul2char(sb_cache.total_blocks, &superblock_buffer[buffer_pos]); buffer_pos += 4;
            ul2char(sb_cache.inode_start_block, &superblock_buffer[buffer_pos]); buffer_pos += 4;
            ul2char(sb_cache.inode_count, &superblock_buffer[buffer_pos]); buffer_pos += 4;
            ul2char(sb_cache.data_start_block, &superblock_buffer[buffer_pos]); buffer_pos += 4;
            ul2char(sb_cache.free_blocks, &superblock_buffer[buffer_pos]); buffer_pos += 4;
            ul2char(sb_cache.root_inode, &superblock_buffer[buffer_pos]); buffer_pos += 4;
            
            diskWriteSector(d, 0, superblock_buffer);
            
            return block_num;  // Retorna o número do bloco livre encontrado
        }
    }
    
    return -1;  // Não há blocos livres
}

// Encontra um descritor de arquivo livre na tabela
// Retorna o índice da entrada livre ou -1 se a tabela estiver cheia
static int find_free_fd(void) {
    for (int fd_index = 0; fd_index < MAX_OPEN_FILES; fd_index++) {
        if (!open_files_table[fd_index].is_used) {
            return fd_index;
        }
    }
    return -1;  // Tabela de arquivos abertos cheia
}


//Funcao para verificacao se o sistema de arquivos está ocioso, ou seja,
//se nao ha quisquer descritores de arquivos em uso atualmente. Retorna
//um positivo se ocioso ou, caso contrario, 0.

int myFSIsIdle (Disk *d) {
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (open_files_table[i].is_used) {
            return 0;  // Não está ocioso - há arquivos abertos
        }
    }
    return 1;  // Está ocioso - nenhum arquivo aberto
}

//Funcao para formatacao de um disco com o novo sistema de arquivos
//com tamanho de blocos igual a blockSize. Retorna o numero total de
//blocos disponiveis no disco, se formatado com sucesso. Caso contrario,
//retorna -1.
int myFSFormat (Disk *d, unsigned int blockSize) {
	return -1;
}

//Funcao para montagem/desmontagem do sistema de arquivos, se possível.
//Na montagem (x=1) e' a chance de se fazer inicializacoes, como carregar
//o superbloco na memoria. Na desmontagem (x=0), quaisquer dados pendentes
//de gravacao devem ser persistidos no disco. Retorna um positivo se a
//montagem ou desmontagem foi bem sucedida ou, caso contrario, 0.
int myFSxMount (Disk *d, int x) {
	if (x == 1) {  // Operação de montagem
        unsigned char superblock_buffer[512];
        
        // Lê o superbloco do disco (bloco 0)
        if (diskReadSector(d, 0, superblock_buffer) != 0) {
            return 0;
        }
        
        // Decodifica os dados do superbloco
        unsigned int buffer_pos = 0;
        char2ul(&superblock_buffer[buffer_pos], &sb_cache.magic_number); buffer_pos += 4;
        char2ul(&superblock_buffer[buffer_pos], &sb_cache.block_size); buffer_pos += 4;
        char2ul(&superblock_buffer[buffer_pos], &sb_cache.total_blocks); buffer_pos += 4;
        char2ul(&superblock_buffer[buffer_pos], &sb_cache.inode_start_block); buffer_pos += 4;
        char2ul(&superblock_buffer[buffer_pos], &sb_cache.inode_count); buffer_pos += 4;
        char2ul(&superblock_buffer[buffer_pos], &sb_cache.data_start_block); buffer_pos += 4;
        char2ul(&superblock_buffer[buffer_pos], &sb_cache.free_blocks); buffer_pos += 4;
        char2ul(&superblock_buffer[buffer_pos], &sb_cache.root_inode); buffer_pos += 4;
        
        // Verifica se é realmente é o nosso sistema MyFS
        if (sb_cache.magic_number != MYFS) {
            printf("[MyFS] Erro: Disco não contém sistema MyFS\n");
            return 0;
        }
        
        // Verifica o tamanho do bloco (só suportamos 512 bytes)
        if (sb_cache.block_size != 512) {
            printf("[MyFS] Erro: Tamanho de bloco não suportado\n");
            return 0;
        }
        
        // Aloca e carrega o mapa de bits (bloco 1)
        unsigned int bitmap_size = sb_cache.total_blocks / 8 + 1;
        block_bitmap = malloc(bitmap_size);
        if (!block_bitmap) {
            printf("[MyFS] Erro: Memória insuficiente para mapa de bits\n");
            return 0;
        }
        
        unsigned char bitmap_block[512];
        if (diskReadSector(d, 1, bitmap_block) != 0) {
            free(block_bitmap);
            block_bitmap = NULL;
            printf("[MyFS] Erro: Não foi possível ler o mapa de bits\n");
            return 0;
        }
        
        // Copia o primeiro bloco do mapa de bits
        memcpy(block_bitmap, bitmap_block, 512);
        
        // Inicializa a tabela de arquivos abertos
        memset(open_files_table, 0, sizeof(open_files_table));
        
        fs_mounted = 1;
        printf("[MyFS] Sistema montado com sucesso! %u blocos livres\n", sb_cache.free_blocks);
        return 1;
        
    } else if (x == 0) {  // Operação de desmontagem
        // Verifica se há arquivos abertos
        if (!myFSIsIdle(d)) {
            printf("[MyFS] Erro: Não é possível desmontar com arquivos abertos\n");
            return 0;
        }
        
        // Libera o mapa de bits da memória
        if (block_bitmap) {
            free(block_bitmap);
            block_bitmap = NULL;
        }
        
        // Limpa o cache do superbloco
        memset(&sb_cache, 0, sizeof(superblock_t));
        
        fs_mounted = 0;
        printf("[MyFS] Sistema desmontado com sucesso\n");
        return 1;
        
    } else {  // Valor de x inválido
        printf("[MyFS] Erro: Operação de montagem inválida (x=%d)\n", x);
        return 0;
    }
}

//Funcao para abertura de um arquivo, a partir do caminho especificado
//em path, no disco montado especificado em d, no modo Read/Write,
//criando o arquivo se nao existir. Retorna um descritor de arquivo,
//em caso de sucesso. Retorna -1, caso contrario.
int myFSOpen (Disk *d, const char *path) {
	return -1;
}
	
//Funcao para a leitura de um arquivo, a partir de um descritor de arquivo
//existente. Os dados devem ser lidos a partir da posicao atual do cursor
//e copiados para buf. Terao tamanho maximo de nbytes. Ao fim, o cursor
//deve ter posicao atualizada para que a proxima operacao ocorra a partir
//do próximo byte apos o ultimo lido. Retorna o numero de bytes
//efetivamente lidos em caso de sucesso ou -1, caso contrario.
int myFSRead (int fd, char *buf, unsigned int nbytes) {
	return -1;
}

//Funcao para a escrita de um arquivo, a partir de um descritor de arquivo
//existente. Os dados de buf sao copiados para o disco a partir da posição
//atual do cursor e terao tamanho maximo de nbytes. Ao fim, o cursor deve
//ter posicao atualizada para que a proxima operacao ocorra a partir do
//proximo byte apos o ultimo escrito. Retorna o numero de bytes
//efetivamente escritos em caso de sucesso ou -1, caso contrario
int myFSWrite (int fd, const char *buf, unsigned int nbytes) {
	return -1;
}

//Funcao para fechar um arquivo, a partir de um descritor de arquivo
//existente. Retorna 0 caso bem sucedido, ou -1 caso contrario
int myFSClose (int fd) {
	return -1;
}

//Funcao para abertura de um diretorio, a partir do caminho
//especificado em path, no disco indicado por d, no modo Read/Write,
//criando o diretorio se nao existir. Retorna um descritor de arquivo,
//em caso de sucesso. Retorna -1, caso contrario.
int myFSOpenDir (Disk *d, const char *path) {

    //  Verifica se o FS está montado
    if (!mounted || !d || !path)
        return -1;

    //  Por enquanto, só aceita o diretório raiz "/"
    if (strcmp(path, "/") != 0)
        return -1;

    //  Encontra posição livre na tabela de arquivos abertos
    int fd = free_file_find();
    if (fd < 0)
        return -1;

    //  Carrega o i-node do diretório raiz
    Inode *root = inodeLoad(sb.root_inode, d);
    if (!root)
        return -1;

    //  Verifica se realmente é um diretório
    if (inodeGetFileType(root) != TYPEFILE_DIRECTORY) {
        free(root);
        return -1;
    }

    // Preenche a tabela de arquivos abertos
    open[fd].used = 1;
    open[fd].inumber = sb.root_inode;
    open[fd].pointer_file = 0;      // cursor começa no início
    open[fd].is_directory = 1;
    open[fd].position_dir = 0;      // posição da leitura no diretório

    free(root);

    //  Retorna o descritor (fd começa em 0 internamente)
    return fd + 1; // VFS espera descritores iniciando em 1

}

//Funcao para a leitura de um diretorio, identificado por um descritor
//de arquivo existente. Os dados lidos correspondem a uma entrada de
//diretorio na posicao atual do cursor no diretorio. O nome da entrada
//e' copiado para filename, como uma string terminada em \0 (max 255+1).
//O numero do inode correspondente 'a entrada e' copiado para inumber.
//Retorna 1 se uma entrada foi lida, 0 se fim de diretorio ou -1 caso
//mal sucedido
int myFSReadDir (int fd, char *filename, unsigned int *inumber) {
	return -1;
}

//Funcao para adicionar uma entrada a um diretorio, identificado por um
//descritor de arquivo existente. A nova entrada tera' o nome indicado
//por filename e apontara' para o numero de i-node indicado por inumber.
//Retorna 0 caso bem sucedido, ou -1 caso contrario.
int myFSLink (int fd, const char *filename, unsigned int inumber) {
	return -1;
}

//Funcao para remover uma entrada existente em um diretorio, 
//identificado por um descritor de arquivo existente. A entrada e'
//identificada pelo nome indicado em filename. Retorna 0 caso bem
//sucedido, ou -1 caso contrario.
int myFSUnlink (int fd, const char *filename) {
	return -1;
}

//Funcao para fechar um diretorio, identificado por um descritor de
//arquivo existente. Retorna 0 caso bem sucedido, ou -1 caso contrario.	
int myFSCloseDir (int fd) {
	return -1;
}

//Funcao para instalar seu sistema de arquivos no S.O., registrando-o junto
//ao virtual FS (vfs). Retorna um identificador unico (slot), caso
//o sistema de arquivos tenha sido registrado com sucesso.
//Caso contrario, retorna -1
int installMyFS (void) {
	FSInfo* fs_info_ptr = malloc(sizeof(FSInfo));
    if (!fs_info_ptr) {
        printf("[MyFS] Erro: Falha ao alocar memória para estrutura FSInfo\n");
        return -1;
    }
    
    // Configura os identificadores do sistema
    fs_info_ptr->fsid = 'M';      // Identificador único  para MyFS
    fs_info_ptr->fsname = "MyFS"; // Nome do sistema de arquivos
    
    // Associa as funções ao FSInfo
    fs_info_ptr->isidleFn = myFSIsIdle;
    fs_info_ptr->formatFn = myFSFormat;
    fs_info_ptr->xMountFn = myFSxMount;
    fs_info_ptr->openFn = myFSOpen;
    fs_info_ptr->readFn = myFSRead;
    fs_info_ptr->writeFn = myFSWrite;
    fs_info_ptr->closeFn = myFSClose;
    fs_info_ptr->opendirFn = myFSOpenDir;
    fs_info_ptr->readdirFn = myFSReadDir;
    fs_info_ptr->linkFn = myFSLink;
    fs_info_ptr->unlinkFn = myFSUnlink;
    fs_info_ptr->closedirFn = myFSCloseDir;
    
    // Registra o sistema no VFS 
    if (vfsRegisterFS(fs_info_ptr) != 0) {
        printf("[MyFS] Erro: Falha no registro no sistema de arquivos virtual\n");
        free(fs_info_ptr);
        return -1;
    }
    
    printf("[MyFS] Sistema de arquivos registrado com sucesso (ID: 'M')\n");
    return 0;  // Sucesso
}
