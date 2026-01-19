/*
 * test_suite.c - Script de teste automatizado para MyFS
 * Compilar com: gcc test_suite.c myfs.c vfs.c inode.c disk.c util.c -o teste_auto
 * Executar: ./teste_auto
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "myfs.h"
#include "disk.h"
#include "vfs.h"

#define DISK_NAME "autotest.dsk"
#define DISK_CYLINDERS 20
#define MYFS_ID 'M'
#define BLOCK_SIZE 1 // 1 setor (512 bytes)

// ====================================================================
// PROTÓTIPOS MANUAIS (Necessário pois myfs.h só expõe installMyFS)
// ====================================================================
int myFSFormat (Disk *d, unsigned int blockSize);
int myFSxMount (Disk *d, int x);
int myFSOpen (Disk *d, const char *path);
int myFSRead (int fd, char *buf, unsigned int nbytes);
int myFSWrite (int fd, const char *buf, unsigned int nbytes);
int myFSClose (int fd);
int myFSOpenDir (Disk *d, const char *path);
int myFSReadDir (int fd, char *filename, unsigned int *inumber);
int myFSLink (int fd, const char *filename, unsigned int inumber);
int myFSUnlink (int fd, const char *filename);
int myFSCloseDir (int fd);
// ====================================================================

void cleanup_disk() {
    remove(DISK_NAME);
}

int main() {
    printf("=== INICIANDO BATERIA DE TESTES AUTOMATIZADOS ===\n\n");

    // 0. Limpeza inicial
    cleanup_disk();

    // 1. Instalação do MyFS
    printf("[1/10] Instalando MyFS no VFS... ");
    if (installMyFS() != 0) {
        printf("FALHA!\n");
        exit(1);
    }
    printf("SUCESSO.\n");

    // 2. Criação do Disco Físico
    printf("[2/10] Criando disco físico (%s)... ", DISK_NAME);
    if (diskCreateRawDisk(DISK_NAME, DISK_CYLINDERS) != 0) {
        printf("FALHA!\n");
        exit(1);
    }
    printf("SUCESSO.\n");

    // 3. Conexão do Disco
    printf("[3/10] Conectando disco... ");
    Disk *d = diskConnect(0, DISK_NAME);
    if (!d) {
        printf("FALHA!\n");
        exit(1);
    }
    printf("SUCESSO.\n");

    // 4. Formatação
    printf("[4/10] Formatando disco (MyFS)... ");
    if (myFSFormat(d, 512) == -1) {
        printf("FALHA!\n");
        exit(1);
    }
    printf("SUCESSO.\n");

    // 5. Montagem
    printf("[5/10] Montando sistema de arquivos... ");
    if (myFSxMount(d, 1) != 1) { // 1 = Mount
        printf("FALHA!\n");
        exit(1);
    }
    printf("SUCESSO.\n");

    // 6. Teste de Arquivo: Open, Write, Close
    printf("[6/10] Teste de Escrita de Arquivo... ");
    const char *filename = "/teste.txt";
    const char *content = "Ola Mundo! Testando MyFS.";
    
    int fd = myFSOpen(d, filename);
    if (fd == -1) { printf("FALHA no Open!\n"); exit(1); }
    
    int written = myFSWrite(fd, content, strlen(content));
    if (written != strlen(content)) { printf("FALHA no Write! (Escreveu %d, esperava %lu)\n", written, strlen(content)); exit(1); }
    
    myFSClose(fd);
    printf("SUCESSO.\n");

    // 7. Teste de Arquivo: Open, Read, Verify
    printf("[7/10] Teste de Leitura e Verificação... ");
    fd = myFSOpen(d, filename); // Reabre
    if (fd == -1) { printf("FALHA no Re-Open!\n"); exit(1); }
    
    char buffer[100];
    memset(buffer, 0, 100);
    int read_bytes = myFSRead(fd, buffer, sizeof(buffer));
    
    if (strcmp(buffer, content) != 0) {
        printf("FALHA! Conteúdo lido incorreto.\n   Esperado: '%s'\n   Lido:     '%s'\n", content, buffer);
        exit(1);
    }
    myFSClose(fd);
    printf("SUCESSO.\n");

    // 8. Teste de Diretório: Listar e Linkar
    printf("[8/10] Teste de Diretório e Links... ");
    
    // Abre diretório raiz
    int dir_fd = myFSOpenDir(d, "/");
    if (dir_fd == -1) { printf("FALHA no OpenDir!\n"); exit(1); }
    
    // Procura o inode do arquivo criado anteriormente
    char entry_name[256];
    unsigned int inumber, target_inumber = 0;
    while (myFSReadDir(dir_fd, entry_name, &inumber) == 1) {
        if (strcmp(entry_name, "teste.txt") == 0) {
            target_inumber = inumber;
            break;
        }
    }
    
    if (target_inumber == 0) { printf("FALHA! Arquivo 'teste.txt' não encontrado no diretório.\n"); exit(1); }
    
    // Cria um hard link (outro nome para o mesmo arquivo)
    if (myFSLink(dir_fd, "link.txt", target_inumber) != 0) {
        printf("FALHA no Link!\n"); exit(1);
    }
    myFSCloseDir(dir_fd); // Fecha para resetar cursor
    
    // Verifica se o link foi criado
    dir_fd = myFSOpenDir(d, "/");
    int found_link = 0;
    while (myFSReadDir(dir_fd, entry_name, &inumber) == 1) {
        if (strcmp(entry_name, "link.txt") == 0 && inumber == target_inumber) {
            found_link = 1;
            break;
        }
    }
    myFSCloseDir(dir_fd);
    
    if (!found_link) { printf("FALHA! Link 'link.txt' não encontrado.\n"); exit(1); }
    printf("SUCESSO.\n");

    // 9. Teste de Remoção (Unlink)
    printf("[9/10] Teste de Remoção (Unlink)... ");
    dir_fd = myFSOpenDir(d, "/");
    
    if (myFSUnlink(dir_fd, "link.txt") != 0) {
        printf("FALHA no Unlink!\n"); exit(1);
    }
    
    // Verifica se sumiu
    myFSCloseDir(dir_fd);
    dir_fd = myFSOpenDir(d, "/");
    
    found_link = 0;
    while (myFSReadDir(dir_fd, entry_name, &inumber) == 1) {
        if (strcmp(entry_name, "link.txt") == 0) {
            found_link = 1;
        }
    }
    myFSCloseDir(dir_fd);
    
    if (found_link) { printf("FALHA! Arquivo ainda existe após Unlink.\n"); exit(1); }
    printf("SUCESSO.\n");

    // 10. Desmontar e Desconectar
    printf("[10/10] Limpeza Final... ");
    if (myFSxMount(d, 0) != 1) { // 0 = Unmount
        printf("FALHA no Unmount!\n"); exit(1);
    }
    diskDisconnect(d);
    printf("SUCESSO.\n");
    // [TESTE EXTRA] Verificar se myFSIsIdle impede desmontagem com arquivo aberto
    printf("[EXTRA] Teste de IsIdle (Bloqueio de Desmonte)... ");
    int fd_extra = myFSOpen(d, "/temp.txt");
    
    // Tenta desmontar com arquivo aberto (DEVE FALHAR)
    if (myFSxMount(d, 0) == 1) { 
        printf("FALHA! Conseguiu desmontar com arquivo aberto.\n"); 
        exit(1); 
    }
    
    myFSClose(fd_extra); // Fecha para liberar
    
    // Agora o sistema deve estar ocioso (Idle), mas não vamos desmontar ainda
    // Apenas verificamos se a lógica acima funcionou.
    printf("SUCESSO (Bloqueio funcionou).\n");
    
    printf("\n=== TODOS OS TESTES PASSARAM! ===\n");
    return 0;
}