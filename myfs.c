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
#include "myfs.h"
#include "vfs.h"
#include "inode.h"
#include "util.h"

//Declaracoes globais
//...
//...
#define MYFS 0x4D794653
#define MAX_FILENAME 14
#define ENTRY_SIZE_DIR 16
#define MAX_OPEN 20
#define ROOT_INODE_NUMBER 1
#define TYPEFILE_REGULAR 1
#define TYPEFILE_DIRECTORY 2

//=================Estruturas==============
typedef struct{
	unsigned int number_myfs; // Assinatura do nosso sistema
	unsigned int block_size; // Tamanho do bloco
	unsigned int total_blocks; // Número total de blocos
	unsigned int start_inode; 
	unsigned int count_inode; // Quantidade de i-nodes que cabem
	unsigned int start_data; // Primeiro bloco onde podem ser armazenados arquivos
	unsigned int free_blocks; // blocos ainda livres
	unsigned int root_inode; // i-node do diretório raiz


}Super_block;

//Entrada no diretorio (16 bytes)
typedef struct
{
	unsigned short inumber; // numero do i-node (2bytes)
	char filename[MAX_FILENAME]; // nome do arquivo (14 bytes)
}Entry_dir;

// entrada na tabela de arquivos já abertos
typedef struct{
	int used;
	unsigned int inumber;
	unsigned int pointer_file;
	int is_directory;
	unsigned int position_dir;
}Open_file;

static Super_block sb; // super bloco na memoria
static int mounted = 0; // Flag, 1 se montado e 0 se não
static unsigned char *map_bit = NULL; 
static Open_file open[MAX_OPEN]; // tabela de arquivos abertos

// função para encontrar um bloco livre
static int find_free_fd(Disk *d){
	unsigned int total_blocks = sb.total_blocks;
	// procura um bit 0
	for (unsigned int i = sb.start_data; i<total_blocks; i++){
		int index_byte = i/8;
		int index_bit = i%8;

		if((map_bit[index_byte] & (1 << index_bit)) == 0){
			// marca que o bloco está usado
			map_bit[index_byte] |= (1 << index_bit);
			sb.free_blocks --;
			// salva o bitmap atualizado no disco
			unsigned char block[512];
			memcpy(block, map_bit, 512);
			diskWriteSector(d, 1, block);
			// salva o bloco atualizado
			unsigned char Super_block[512];
			memset(Super_block, 0, 512);
			unsigned int pos = 0;
			ul2char(sb.number_myfs, &Super_block[pos]); pos +=4;
			ul2char(sb.block_size, &Super_block[pos]); pos +=4;
			ul2char(sb.total_blocks, &Super_block[pos]); pos +=4;
			ul2char(sb.start_inode, &Super_block[pos]); pos +=4;
			ul2char(sb.count_inode, &Super_block[pos]); pos +=4;
			ul2char(sb.start_data, &Super_block[pos]); pos +=4;
			ul2char(sb.free_blocks, &Super_block[pos]); pos +=4;
			ul2char(sb.root_inode, &Super_block[pos]); pos +=4;
			diskWriteSector(d,0, Super_block);

			return i;


		}

	}
	return -1; // nenhum bloco está livre

}
// função para encontrar uma entrada livre na tabela de arquivos abertos
static int free_file_find(){
	for(int i = 0; i< MAX_OPEN; i++){
		if(!open[i].used){
			return i;
		}
	}
	return -1;
}


//Funcao para verificacao se o sistema de arquivos está ocioso, ou seja,
//se nao ha quisquer descritores de arquivos em uso atualmente. Retorna
//um positivo se ocioso ou, caso contrario, 0.
int myFSIsIdle (Disk *d) {
	return 0;
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
	return 0;
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
	return -1;
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
	return -1;
}
