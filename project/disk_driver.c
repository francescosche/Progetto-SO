#define _GNU_SOURCE
#include "disk_driver.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>

// Apre il file (creandolo, se necessario), allocando lo spazio necessario sul disco e calcolando quanto deve essere grane la mappa se il file è stato appena creato.
// Compila un Disk Header e riempie la Bitmap della dimensione appropriata con tutti 0 (per denotare lo spazio libero)
// opens the file (creating it if necessary) allocates the necessary space on the disk calculates how big the bitmap should be
// If the file was new compiles a disk header, and fills in the bitmap of appropriate size with all 0 (to denote the free space)
void DiskDriver_init(DiskDriver* disk, const char* filename, int num_blocks) {

	// Calcoliamo quanti blocchi dovremo memorizzare nel disco
	// Se il numero dei blocchi è multiplo di 8, impostiamo num_blocks/8, altrimenti aggiungiamo 1 (per arrotondare per eccesso)
	int bitmap_entries;
	if(num_blocks % 8 == 0) {
		bitmap_entries = num_blocks / 8; //TODO
	}else{
		bitmap_entries = (num_blocks / 8) + 1;
	}

	// Variabile in cui memorizzare il file descriptor che ci aiuterà ad utilizzare il file stesso
	int file;

	// Se il file esiste, calcoliamo alcuni dati, se non esiste, lo creiamo e inseriamo "Ciao mondo"
	if(!access(filename, F_OK)) { // se il file esiste
		file = open(filename, O_RDWR, 0666);
		if(!file) {
			printf("C'è stato un errore nell'apertura del file. Il programma è stato bloccato.");
			return;
		}
		disk->fd = file;
	}else{ // se il file è stato appena creato
		file = open(filename, O_CREAT | O_RDWR, 0666);
		if(!file) {
			printf("C'è stato un errore nell'apertura del file. Il programma è stato bloccato.");
			return;
		}
		disk->fd=file;
		write(file, "\0", 1); // inizializzo il file per evitare "bus error"
	}

	// Mi calcolo le dimensioni del file aperto
	struct stat file_stat;
	fstat(file, &file_stat);
	int file_size = file_stat.st_size;

	// Ottengo il contenuto del file
	void * buffer = malloc(file_size + 1);
	read(file, buffer, file_size);

	// Creo una BitMap e memorizzo le informazioni e il contenuto del file aperto
	BitMap* bmap = (BitMap*) malloc(sizeof(BitMap));
	BitMap_init(bmap, file_size * 8, buffer);
	disk->bitmap = bmap;

	// Creiamo un DiskHeader che andrà inserito nel DiskDriver
	disk->header = (DiskHeader*) mmap(0, sizeof(DiskHeader) + bitmap_entries, PROT_READ | PROT_WRITE, MAP_SHARED, file, 0);
	disk->header->num_blocks = num_blocks;
	disk->header->bitmap_blocks = num_blocks;
	disk->header->bitmap_entries = bitmap_entries;
	disk->header->free_blocks = num_blocks; 
	disk->header->first_free_block = DiskDriver_getFreeBlock(disk, 0);

	return;
}


// reads the block in position block_num
// returns -1 if the block is free accrding to the bitmap
// 0 otherwise
int DiskDriver_readBlock(DiskDriver* disk, void* dest, int block_num){

	//lseek is a system call that is used to change the location of the read/write pointer of a file descriptor
	off_t start=lseek(disk->fd,block_num,SEEK_SET); //block_num è the offset of the pointer (measured in bytes).
	if(start==-1) return -1;
	//printf("\n start=%ld\n", start);

	int rz= read(disk->fd,dest ,BLOCK_SIZE);

	if(rz!=0) return -1;
	else return 0;
}


// writes a block in position block_num, and alters the bitmap accordingly
// returns -1 if operation not possible
int DiskDriver_writeBlock(DiskDriver* disk, void* src, int block_num) {
	//sposto il puntatore nel blocco che voglio io
	off_t start=lseek(disk->fd,block_num,SEEK_SET);
	if(start==-1) return -1;

	int sz = write(disk->fd,src,BLOCK_SIZE);
  if(sz==-1) return -1;

	if(sz!=BLOCK_SIZE) return -1;
	else return 0;
}

// frees a block in position block_num, and alters the bitmap accordingly
// returns -1 if operation not possible
int DiskDriver_freeBlock(DiskDriver* disk, int block_num) {
	BitMap* bmap= disk->bitmap;
	int i;
	int indice= block_num*BLOCK_SIZE;
	for(i=indice ; i< indice+BLOCK_SIZE; i++){
		int n=BitMap_set(bmap,i,0);
		if(n<0) return -1;
	}
	return 0;

}

// returns the first free blockin the disk from position (checking the bitmap)
int DiskDriver_getFreeBlock(DiskDriver* disk, int start) {
	int num = disk->header->num_blocks, i, j;
	for(i = 0; i < num * BLOCK_SIZE; i = i + BLOCK_SIZE) { // TODO: Fare in modo che il ciclo inizi da "start * BLOCK_SIZE"
		for(j = 0; j <= BLOCK_SIZE; j++){
			if(j == BLOCK_SIZE){
				return i / BLOCK_SIZE;
			}
			int n = BitMap_get(disk->bitmap, i+j, 0);
			if(n != i+j) {
				break;
			}
		}
	}
}

// writes the data (flushing the mmaps)
int DiskDriver_flush(DiskDriver* disk) {
}