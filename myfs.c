/*
*  myfs.c - Implementacao do sistema de arquivos MyFS
*
*  Autores: Lara Dias - 202376010, Sarah Cristina - 202376034, Willian Santos
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
static Disk *current_disk= NULL;              // Ponteiro para o disco atual
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
	if (blockSize != 512) return -1; 

    unsigned long total_sectors = diskGetNumSectors(d);
    
    // 1. Configura e Grava Superbloco
    sb_cache.magic_number = MYFS;
    sb_cache.block_size = blockSize;
    sb_cache.total_blocks = total_sectors;
    sb_cache.inode_start_block = 2; 
    
    // Define 10% do disco para inodes
    unsigned int num_inode_blocks = (total_sectors / 10);
    if (num_inode_blocks < 1) num_inode_blocks = 1;
    
    sb_cache.inode_count = num_inode_blocks * (blockSize / 64); // 64 bytes por inode (assumindo inode.c padrão)
    sb_cache.data_start_block = sb_cache.inode_start_block + num_inode_blocks;
    sb_cache.free_blocks = total_sectors - sb_cache.data_start_block;
    sb_cache.root_inode = ROOT_INODE_NUM;

    unsigned char sb_buf[512] = {0};
    unsigned int pos = 0;
    ul2char(sb_cache.magic_number, &sb_buf[pos]); pos += 4;
    ul2char(sb_cache.block_size, &sb_buf[pos]); pos += 4;
    ul2char(sb_cache.total_blocks, &sb_buf[pos]); pos += 4;
    ul2char(sb_cache.inode_start_block, &sb_buf[pos]); pos += 4;
    ul2char(sb_cache.inode_count, &sb_buf[pos]); pos += 4;
    ul2char(sb_cache.data_start_block, &sb_buf[pos]); pos += 4;
    ul2char(sb_cache.free_blocks, &sb_buf[pos]); pos += 4;
    ul2char(sb_cache.root_inode, &sb_buf[pos]); pos += 4;
    
    if (diskWriteSector(d, 0, sb_buf) < 0) return -1;

    // 2. Inicializa o Bitmap Global (Necessário para find_free_block funcionar agora)
    if (block_bitmap) free(block_bitmap);
    unsigned int bitmap_size = (total_sectors + 7) / 8; // Tamanho em bytes
    // Arredonda para tamanho de setor para escrita
    unsigned int bitmap_sector_size = (bitmap_size + 512 - 1) / 512 * 512;
    block_bitmap = calloc(1, bitmap_sector_size);
    if (!block_bitmap) return -1;

    // Marca blocos ocupados (SB + Bitmap + Inodes)
    for (unsigned int i = 0; i < sb_cache.data_start_block; i++) {
        block_bitmap[i/8] |= (1 << (i%8));
    }
    // Grava Bitmap no disco (Bloco 1)
    if (diskWriteSector(d, 1, block_bitmap) < 0) {
        free(block_bitmap); block_bitmap = NULL; return -1;
    }

    // 3. Zera a área de i-nodes (CRUCIAL: Remove lixo para inodeCreate não falhar ao ler 'next')
    unsigned char zero_buf[512] = {0};
    for (unsigned int i = 0; i < num_inode_blocks; i++) {
        if (diskWriteSector(d, sb_cache.inode_start_block + i, zero_buf) < 0) {
             free(block_bitmap); block_bitmap = NULL; return -1;
        }
    }

    // 4. Inicializa TODOS os i-nodes (CRUCIAL: Escreve os números 1..N no disco)
    // Sem isso, inodeFindFreeInode lê número 0 e acha que é inválido.
    for (unsigned int i = 1; i <= sb_cache.inode_count; i++) {
        Inode *temp = inodeCreate(i, d);
        if (temp) {
            free(temp); // Apenas cria/grava e libera
        }
    }

    // 5. Configura o Diretório Raiz
    Inode *root = inodeLoad(ROOT_INODE_NUM, d);
    if (!root) {
        free(block_bitmap); block_bitmap = NULL; return -1;
    }
    
    inodeSetFileType(root, INODE_TYPE_DIRECTORY);
    
    // Aloca bloco de dados para o diretório
    int root_block = find_free_block(d);
    if (root_block != -1) {
        diskWriteSector(d, root_block, zero_buf); // Limpa conteúdo do diretório
        inodeAddBlock(root, root_block);
    }
    
    inodeSetFileSize(root, 0);
    inodeSave(root);
    free(root);

    // Limpeza: Libera bitmap global pois o mount irá carregá-lo novamente depois
    free(block_bitmap);
    block_bitmap = NULL;

    return sb_cache.free_blocks;

}

//Funcao para montagem/desmontagem do sistema de arquivos, se possível.
//Na montagem (x=1) e' a chance de se fazer inicializacoes, como carregar
//o superbloco na memoria. Na desmontagem (x=0), quaisquer dados pendentes
//de gravacao devem ser persistidos no disco. Retorna um positivo se a
//montagem ou desmontagem foi bem sucedida ou, caso contrario, 0.
int myFSxMount (Disk *d, int x) {
	if (x == 1) { // Mount
        unsigned char buf[512];
        if (diskReadSector(d, 0, buf) != 0) return 0;
        
        unsigned int pos = 0;
        char2ul(&buf[pos], &sb_cache.magic_number); pos += 4;
        
        if (sb_cache.magic_number != MYFS) {
            printf("[MyFS] Erro: Assinatura inválida.\n");
            return 0;
        }
        char2ul(&buf[pos], &sb_cache.block_size); pos += 4;
        char2ul(&buf[pos], &sb_cache.total_blocks); pos += 4;
        char2ul(&buf[pos], &sb_cache.inode_start_block); pos += 4;
        char2ul(&buf[pos], &sb_cache.inode_count); pos += 4;
        char2ul(&buf[pos], &sb_cache.data_start_block); pos += 4;
        char2ul(&buf[pos], &sb_cache.free_blocks); pos += 4;
        char2ul(&buf[pos], &sb_cache.root_inode); pos += 4;

        if (block_bitmap) free(block_bitmap);
        block_bitmap = calloc(1, 512); 
        if (!block_bitmap) return 0;

        if (diskReadSector(d, 1, block_bitmap) != 0) {
            free(block_bitmap);
            block_bitmap = NULL;
            return 0;
        }
        
        memset(open_files_table, 0, sizeof(open_files_table));
        current_disk = d;
        fs_mounted = 1;
        printf("[MyFS] Sistema montado com sucesso! %u blocos livres\n", sb_cache.free_blocks);
        return 1;
    } else { // Unmount
        if (!myFSIsIdle(d)) return 0;
        
        if (block_bitmap) {
            free(block_bitmap);
            block_bitmap = NULL;
        }
        
        current_disk = NULL;
        fs_mounted = 0;
        printf("[MyFS] Sistema desmontado.\n");
        return 1;
    }

}

//Funcao para abertura de um arquivo, a partir do caminho especificado
//em path, no disco montado especificado em d, no modo Read/Write,
//criando o arquivo se nao existir. Retorna um descritor de arquivo,
//em caso de sucesso. Retorna -1, caso contrario.
int myFSOpen (Disk *d, const char *path) {
	if (!fs_mounted || !path || path[0] != '/') return -1;
    const char *name = path + 1;
    if (strlen(name) > MAX_FILENAME_LEN || strlen(name) == 0) return -1;

	//carrega inode do diretorio raiz
    Inode *root = inodeLoad(sb_cache.root_inode, d);
    if (!root) {
        fprintf(stderr, "[Open] Falha ao carregar inode raiz (SB: %u)\n", sb_cache.root_inode);
        return -1;
    }

	//procura entrada de diretorio com o nome solicitado
    unsigned int found_inumber = 0;
    unsigned int size = inodeGetFileSize(root);
	unsigned int num_blocks = (size + sb_cache.block_size - 1) / sb_cache.block_size;
    unsigned char block_buf[512];

	//percorre todos os blocos do diretorio raiz
    for (unsigned int i = 0; i < num_blocks; i++) {
        unsigned int addr = inodeGetBlockAddr(root, i);
        if (addr == 0) continue;
        //le blocos do diretorio
		diskReadSector(d, addr, block_buf);
        
		//percorre todas as entradas do bloco
        for (int off = 0; off < 512; off += sizeof(dir_entry_t)) {
            dir_entry_t *entry = (dir_entry_t *)(block_buf + off);
            if (entry->inode_number != 0 && strncmp(entry->filename, name, MAX_FILENAME_LEN) == 0) {
                fprintf(stderr, "[DEBUG Open] Lendo entrada: '%s' (inode %u)\n", entry->filename, entry->inode_number);
                //arquivo encontrado
				if (strncmp(entry->filename, name, MAX_FILENAME_LEN) == 0) {
				found_inumber = entry->inode_number;
                break;
            }
        }
        }
        if (found_inumber) break;
    }

    if (found_inumber == 0) {
        found_inumber = inodeFindFreeInode(1, d);
        if (found_inumber == 0) { 
            fprintf(stderr, "[Open] Erro: Sem inodes livres\n");
            free(root); return -1; 
        }
        // cria novo arquivo
        Inode *new_file = inodeCreate(found_inumber, d);
        if (!new_file) { free(root); return -1; }
        inodeSetFileType(new_file, INODE_TYPE_REGULAR);
        inodeSave(new_file);
        free(new_file);
		// adiciona entrada no diretorio raiz
        unsigned int entries_per_block = sb_cache.block_size / sizeof(dir_entry_t);
        int added = 0;
        
        for (unsigned int pos = 0; ; pos++) {
            unsigned int block_idx = pos / entries_per_block;
            unsigned int offset = (pos % entries_per_block) * sizeof(dir_entry_t);
            unsigned int block_addr = inodeGetBlockAddr(root, block_idx);

            // Se precisa alocar novo bloco para o diretório
            if (block_addr == 0) {
                int new_block = find_free_block(d);
                if (new_block == -1) break; // Disco cheio
                
                memset(block_buf, 0, 512);
                diskWriteSector(d, new_block, block_buf);
                if (inodeAddBlock(root, new_block) < 0) break;
                block_addr = new_block;
            } else {
                diskReadSector(d, block_addr, block_buf);
            }

            dir_entry_t *entry = (dir_entry_t *)(block_buf + offset);
            
            // Encontrou slot livre
            if (entry->inode_number == 0) {
                entry->inode_number = (unsigned short)found_inumber;
                memset(entry->filename, 0, MAX_FILENAME_LEN);
                strncpy(entry->filename, name, MAX_FILENAME_LEN);
                
                if (diskWriteSector(d, block_addr, block_buf) < 0) {
                    fprintf(stderr, "[Open] Erro critico: falha ao gravar entrada no dir\n");
                    free(root); return -1;
        }

                unsigned int cur_size = inodeGetFileSize(root);
                unsigned int end_pos = (pos + 1) * sizeof(dir_entry_t);
                if (end_pos > cur_size) {
                    inodeSetFileSize(root, end_pos);
                }
                inodeSave(root);
                added = 1;
                break;
            }
        }
        if (!added) { free(root); return -1; }
    }
    free(root);

    int fd = find_free_fd();
    if (fd == -1) return -1;
	// preenche a tabela de arquivos abertos
    open_files_table[fd].is_used = 1;
    open_files_table[fd].inode_number = found_inumber;
    open_files_table[fd].current_position = 0;
    open_files_table[fd].is_directory = 0;
    
    return fd + 1; // VFS espera descritores iniciando em 1
}
	
//Funcao para a leitura de um arquivo, a partir de um descritor de arquivo
//existente. Os dados devem ser lidos a partir da posicao atual do cursor
//e copiados para buf. Terao tamanho maximo de nbytes. Ao fim, o cursor
//deve ter posicao atualizada para que a proxima operacao ocorra a partir
//do próximo byte apos o ultimo lido. Retorna o numero de bytes
//efetivamente lidos em caso de sucesso ou -1, caso contrario.
int myFSRead (int fd, char *buf, unsigned int nbytes) {
	int idx = fd - 1;
    if (idx < 0 || idx >= MAX_OPEN_FILES || !open_files_table[idx].is_used) return -1;
    if (open_files_table[idx].is_directory) return -1;
    if (current_disk == NULL) return -1;

    Inode *inode = inodeLoad(open_files_table[idx].inode_number, current_disk);
    if (!inode) return -1;

    unsigned int size = inodeGetFileSize(inode);
    unsigned int pos = open_files_table[idx].current_position;
    
    if (pos >= size) { free(inode); return 0; }
    if (pos + nbytes > size) nbytes = size - pos;

    unsigned int read_count = 0;
    unsigned char block_buf[512];

    while (read_count < nbytes) {
        unsigned int blk_idx = pos / 512;
        unsigned int offset = pos % 512;
        unsigned int chunk = 512 - offset;
        if (chunk > nbytes - read_count) chunk = nbytes - read_count;

        unsigned int addr = inodeGetBlockAddr(inode, blk_idx);
        if (addr != 0) {
            if (diskReadSector(current_disk, addr, block_buf) < 0) {
                free(inode); return -1;
            }
            memcpy(buf + read_count, block_buf + offset, chunk);
        } else {
            memset(buf + read_count, 0, chunk);
        }
        
        pos += chunk;
        read_count += chunk;
    }

    open_files_table[idx].current_position = pos;
    free(inode);
    return read_count;

}

//Funcao para a escrita de um arquivo, a partir de um descritor de arquivo
//existente. Os dados de buf sao copiados para o disco a partir da posição
//atual do cursor e terao tamanho maximo de nbytes. Ao fim, o cursor deve
//ter posicao atualizada para que a proxima operacao ocorra a partir do
//proximo byte apos o ultimo escrito. Retorna o numero de bytes
//efetivamente escritos em caso de sucesso ou -1, caso contrario
int myFSWrite (int fd, const char *buf, unsigned int nbytes) {
    int idx = fd - 1;
    
    if (idx < 0 || idx >= MAX_OPEN_FILES) {
        printf("[Write] Erro: FD invalido (%d -> idx %d)\n", fd, idx);
	return -1;
}

    if (!open_files_table[idx].is_used) {
        printf("[Write] Erro: Arquivo nao esta aberto (idx %d)\n", idx);
        return -1;
    }
    
    if (open_files_table[idx].is_directory) {
        printf("[Write] Erro: Tentativa de escrita em diretorio\n");
        return -1;
    }
    
    if (current_disk == NULL) {
        printf("[Write] Erro: current_disk eh NULL\n");
        return -1;
    }

    unsigned int inum = open_files_table[idx].inode_number;
    Inode *inode = inodeLoad(inum, current_disk);
    if (!inode) {
        printf("[Write] Erro: inodeLoad falhou para inumber %u\n", inum);
        return -1;
    }

    unsigned int pos = open_files_table[idx].current_position;
    unsigned int written_count = 0;
    unsigned char block_buf[512];

    while (written_count < nbytes) {
        unsigned int blk_idx = pos / 512;
        unsigned int offset = pos % 512;
        unsigned int chunk = 512 - offset;
        if (chunk > nbytes - written_count) chunk = nbytes - written_count;

        unsigned int addr = inodeGetBlockAddr(inode, blk_idx);
        
        if (addr == 0) {
            int new_blk = find_free_block(current_disk);
            if (new_blk == -1) {
                printf("[Write] Erro: Disco cheio (find_free_block)\n");
                break; 
            }
            
            memset(block_buf, 0, 512);
            diskWriteSector(current_disk, new_blk, block_buf);
            
            if (inodeAddBlock(inode, new_blk) < 0) {
                printf("[Write] Erro: inodeAddBlock falhou\n");
                break; 
            }
            addr = new_blk;
        } else {
            if (chunk < 512) {
                diskReadSector(current_disk, addr, block_buf);
            }
        }

        memcpy(block_buf + offset, buf + written_count, chunk);
        if (diskWriteSector(current_disk, addr, block_buf) < 0) {
            printf("[Write] Erro: diskWriteSector falhou\n");
            break;
        }

        pos += chunk;
        written_count += chunk;
    }

    if (pos > inodeGetFileSize(inode)) {
        inodeSetFileSize(inode, pos);
        inodeSave(inode);
    }

    open_files_table[idx].current_position = pos;
    free(inode);
    return written_count;
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

	   // Verifica se o descritor de diretório é válido
    if (fd < 0 || fd >= MAX_OPEN)
        return -1;

    // Verifica se o descritor está em uso e se realmente é um diretório
    if (!open[fd].used || !open[fd].is_directory)
        return -1;

    // Carrega o i-node do diretório a partir do número armazenado na tabela open[]
    Inode *dir_inode = inodeLoad(open[fd].inumber, NULL);
    if (!dir_inode)
        return -1;

    // Posição atual do cursor no diretório (qual entrada será lida)
    unsigned int pos = open[fd].position_dir;

    // Tamanho de uma entrada de diretório (16 bytes)
    unsigned int entry_size = sizeof(Entry_dir);

    // Calcula qual bloco do diretório contém a entrada atual
    unsigned int block_index = (pos * entry_size) / sb.block_size;

    // Calcula o deslocamento da entrada dentro do bloco
    unsigned int offset = (pos * entry_size) % sb.block_size;

    // Obtém o endereço do bloco no disco a partir do i-node
    unsigned int block_addr = inodeGetBlockAddr(dir_inode, block_index);

    // Se não houver bloco associado, chegou ao fim do diretório
    if (block_addr == 0) {
        free(dir_inode);
        return 0; // fim do diretório
    }

    // Buffer para leitura do bloco do disco
    unsigned char block[512];

    // Lê o bloco do disco
    if (diskReadSector(dir_inode->d, block_addr, block) < 0) {
        free(dir_inode);
        return -1;
    }

    // Estrutura para armazenar a entrada de diretório lida
    Entry_dir entry;

    // Copia os dados da entrada a partir do bloco
    memcpy(&entry, block + offset, entry_size);

    // Se o número do i-node for 0, a entrada é vazia → fim do diretório
    if (entry.inumber == 0) {
        free(dir_inode);
        return 0;
    }

    // Copia o nome do arquivo para o buffer de saída
    strncpy(filename, entry.filename, MAX_FILENAME);

    // Garante que o nome termine com '\0'
    filename[MAX_FILENAME] = '\0';

    // Retorna o número do i-node associado à entrada
    *inumber = entry.inumber;

    // Avança o cursor do diretório para a próxima entrada
    open[fd].position_dir++;

    // Libera o i-node carregado
    free(dir_inode);


    return 1;

}

//Funcao para adicionar uma entrada a um diretorio, identificado por um
//descritor de arquivo existente. A nova entrada tera' o nome indicado
//por filename e apontara' para o numero de i-node indicado por inumber.
//Retorna 0 caso bem sucedido, ou -1 caso contrario.
int myFSLink (int fd, const char *filename, unsigned int inumber) {
	
	// Verifica se o descritor é válido
    if (fd < 0 || fd >= MAX_OPEN)
        return -1;

    // Verifica se está em uso e se é um diretório
    if (!open[fd].used || !open[fd].is_directory)
        return -1;

    // Nome inválido
    if (!filename || strlen(filename) == 0 || strlen(filename) > MAX_FILENAME)
        return -1;

    // Carrega o i-node do diretório
    Inode *dir_inode = inodeLoad(open[fd].inumber, NULL);
    if (!dir_inode)
        return -1;

    unsigned int entry_size = sizeof(Entry_dir);
    unsigned int entries_per_block = sb.block_size / entry_size;

    // Percorre todas as entradas do diretório
    for (unsigned int pos = 0; ; pos++) {

        unsigned int block_index = pos / entries_per_block;
        unsigned int offset = (pos % entries_per_block) * entry_size;

        // Obtém o endereço do bloco
        unsigned int block_addr = inodeGetBlockAddr(dir_inode, block_index);

        // Se o bloco não existir, cria um novo
        if (block_addr == 0) {
            block_addr = find_free_fd(dir_inode->d);
            if (block_addr < 0) {
                free(dir_inode);
                return -1;
            }

            // Associa o novo bloco ao diretório
            if (inodeAddBlock(dir_inode, block_addr) < 0) {
                free(dir_inode);
                return -1;
            }

            // Inicializa o bloco com zeros
            unsigned char empty_block[512] = {0};
            diskWriteSector(dir_inode->d, block_addr, empty_block);
        }

        // Lê o bloco do diretório
        unsigned char block[512];
        if (diskReadSector(dir_inode->d, block_addr, block) < 0) {
            free(dir_inode);
            return -1;
        }

        Entry_dir *entry = (Entry_dir *)(block + offset);

        // Se já existir uma entrada com esse nome → erro
        if (entry->inumber != 0 &&
            strncmp(entry->filename, filename, MAX_FILENAME) == 0) {
            free(dir_inode);
            return -1;
        }

        // Encontrou uma entrada livre
        if (entry->inumber == 0) {

            entry->inumber = inumber;
            memset(entry->filename, 0, MAX_FILENAME);
            strncpy(entry->filename, filename, MAX_FILENAME);

            // Escreve o bloco de volta no disco
            if (diskWriteSector(dir_inode->d, block_addr, block) < 0) {
                free(dir_inode);
                return -1;
            }

            // Atualiza tamanho do diretório (opcional, mas correto)
            unsigned int size = inodeGetFileSize(dir_inode);
            inodeSetFileSize(dir_inode, size + entry_size);
            inodeSave(dir_inode);

            free(dir_inode);
            return 0; 
        }
    }
}

//Funcao para remover uma entrada existente em um diretorio, 
//identificado por um descritor de arquivo existente. A entrada e'
//identificada pelo nome indicado em filename. Retorna 0 caso bem
//sucedido, ou -1 caso contrario.
int myFSUnlink (int fd, const char *filename) {
	
	// Verifica se o descritor é válido
    if (fd < 0 || fd >= MAX_OPEN)
        return -1;

    // Verifica se o descritor está em uso e se é um diretório
    if (!open[fd].used || !open[fd].is_directory)
        return -1;

    // Nome inválido
    if (!filename || strlen(filename) == 0 || strlen(filename) > MAX_FILENAME)
        return -1;

    // Carrega o i-node do diretório
    Inode *dir_inode = inodeLoad(open[fd].inumber, NULL);
    if (!dir_inode)
        return -1;

    unsigned int entry_size = sizeof(Entry_dir);
    unsigned int entries_per_block = sb.block_size / entry_size;

    // Percorre todas as entradas do diretório
    for (unsigned int pos = 0; ; pos++) {

        unsigned int block_index = pos / entries_per_block;
        unsigned int offset = (pos % entries_per_block) * entry_size;

        // Obtém o endereço do bloco do diretório
        unsigned int block_addr = inodeGetBlockAddr(dir_inode, block_index);

        // Se não houver mais blocos, a entrada não existe
        if (block_addr == 0) {
            free(dir_inode);
            return -1;
        }

        // Lê o bloco do disco
        unsigned char block[512];
        if (diskReadSector(dir_inode->d, block_addr, block) < 0) {
            free(dir_inode);
            return -1;
        }

        Entry_dir *entry = (Entry_dir *)(block + offset);

        // Se a entrada for válida e o nome bater
        if (entry->inumber != 0 &&
            strncmp(entry->filename, filename, MAX_FILENAME) == 0) {

            // Remove a entrada: zera o i-node e o nome
            entry->inumber = 0;
            memset(entry->filename, 0, MAX_FILENAME);

            // Grava o bloco atualizado no disco
            if (diskWriteSector(dir_inode->d, block_addr, block) < 0) {
                free(dir_inode);
                return -1;
            }

            free(dir_inode);
            return 0; // 
        }
    }
}

//Funcao para fechar um diretorio, identificado por um descritor de
//arquivo existente. Retorna 0 caso bem sucedido, ou -1 caso contrario.	
int myFSCloseDir (int fd) {
	
	 // Verifica se o descritor é válido
    if (fd < 0 || fd >= MAX_OPEN)
        return -1;

    // Verifica se o descritor está em uso e se é um diretório
    if (!open[fd].used || !open[fd].is_directory)
        return -1;

    // Libera a entrada da tabela de arquivos abertos
    open[fd].used = 0;
    open[fd].inumber = 0;
    open[fd].position_dir = 0;
    open[fd].is_directory = 0;

    return 0;

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
