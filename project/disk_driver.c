#define _GNU_SOURCE
#include "disk_driver.h"
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>


// Apre il file (creandolo, se necessario), allocando lo spazio necessario sul disco e calcolando quanto deve essere grane la mappa se il file è 
// stato appena creato.
// Compila un Disk Header e riempie la Bitmap della dimensione appropriata con tutti 0 (per denotare lo spazio libero)
// opens the file (creating it if necessary) allocates the necessary space on the disk calculates how big the bitmap should be
// If the file was new compiles a disk header, and fills in the bitmap of appropriate size with all 0 (to denote the free space)
void DiskDriver_init(DiskDriver* disk, const char* filename, int num_blocks) {

	// Calcoliamo quanti blocchi dovremo memorizzare nel disco
	int bitmap_entries = num_blocks;

	// Variabile in cui memorizzare il file descriptor che ci aiuterà ad utilizzare il file stesso
	int file;

	// Se il file esiste, calcoliamo alcuni dati, se non esiste, lo creiamo e inseriamo "Ciao mondo"
	if(!access(filename, F_OK)) { // se il file esiste
		file = open(filename, O_RDWR, 0666);
		if(!file) {
			printf("C'è stato un errore nell'apertura del file. Il programma è stato bloccato.");
			return;
		}

		// alloco la memoria necessaria al file per evitare "bus error"
		int ret = posix_fallocate(file, 0, sizeof(DiskHeader) + bitmap_entries + num_blocks*BLOCK_SIZE);
		disk->fd = file;
		disk->header = (DiskHeader*) mmap(0, sizeof(DiskHeader) + bitmap_entries + (num_blocks*BLOCK_SIZE), PROT_READ | PROT_WRITE, MAP_SHARED, file, 0);
		
	}else{
		// Se il file è stato appena creato
		file = open(filename, O_CREAT | O_RDWR, 0666);

		// Verifico che l'apertura sia avvenuta corretamente
		if(!file) {
			printf("C'è stato un errore nell'apertura del file. Il programma è stato bloccato.");
			return;
		}

		// Memorizzo come file descriptor del disco il file appena aperto
		disk->fd=file;

		// Alloco la memoria necessaria al file per evitare "bus error"
		int ret = posix_fallocate(file, 0, sizeof(DiskHeader) + bitmap_entries + num_blocks*BLOCK_SIZE);

		// Creiamo un DiskHeader che andrà inserito nel DiskDriver
		disk->header = (DiskHeader*) mmap(0, sizeof(DiskHeader) + bitmap_entries + (num_blocks*BLOCK_SIZE), PROT_READ | PROT_WRITE, MAP_SHARED, file, 0);
		disk->header->num_blocks = num_blocks;
		disk->header->bitmap_blocks = count_blocks(bitmap_entries);
		disk->header->bitmap_entries = bitmap_entries;
		disk->header->free_blocks = num_blocks ;
	}

	lseek(file, 0, SEEK_SET);
	// Memorizzo in bitmap_data il puntatore alla mmap saltando lo spazio dedicato a DiskHeader
	disk->bitmap_data = (char *) disk->header + sizeof(DiskHeader);

	// Calcolo il primo blocco libero dopo aver assegnato il valore alle entries
	disk->header->first_free_block = DiskDriver_getFreeBlock(disk,0);

	return;
}


// reads the block in position block_num, returns -1 if the block is free accrding to the bitmap 0 otherwise 
int DiskDriver_readBlock(DiskDriver* disk, void * dest, int block_num){

	// Se il blocco da leggere è maggiore del numero di blocchi contenuti, restituisco un errore
	if(block_num >= disk->header->num_blocks) return -1;

	// Creo la bitmap che andrò ad utilizzare per la BitMap_get()
	BitMap bitmap;
	bitmap.num_bits = disk->header->bitmap_entries * 8;
	bitmap.entries = disk->bitmap_data;
	
	// Se il blocco che si vuole leggere è vuoto, restituiamo un errore
	if(BitMap_get(&bitmap, block_num, 0) == block_num) return -1;
	
	// Leggo il blocco block_num e lo inserisco in dest
	memcpy(dest, disk->bitmap_data + disk->header->bitmap_entries + (block_num * BLOCK_SIZE), BLOCK_SIZE);

	// Se non ho restituito nulla finora, vuol dire che la funzione è andata a buon fine
	return 0;
}


// writes a block in position block_num, and alters the bitmap accordingly, returns -1 if operation not possible
int DiskDriver_writeBlock(DiskDriver * disk, void * src, int block_num) {
	
	// Se il numero del blocco da scrivere è maggiore del numero di blocchi esistenti, restituisco un errore
	if(block_num > disk->header->num_blocks) return -1;

	// Creo la bitmap che andrò ad utilizzare per la BitMap_get()
	BitMap bitmap;
	bitmap.num_bits = disk->header->bitmap_entries * 8;
	bitmap.entries = disk->bitmap_data;
	
	// Se il blocco è libero allora decremento free_block
	if(BitMap_get(&bitmap,block_num,0) == block_num) disk->header->free_blocks--;

	if(strlen(src) * 8 > BLOCK_SIZE) return -1;

	// Scrivo che il blocco è occupato
	BitMap_set(&bitmap, block_num, 1);

	// Scrivo il contenuto di src in block_num
	memcpy(disk->bitmap_data + disk->header->bitmap_entries + (block_num * BLOCK_SIZE), src, BLOCK_SIZE);

	// Mi assicuro che il contenuto della write sia memorizzato su disk 
	if(DiskDriver_flush(disk) == -1) return -1;

	disk->header->first_free_block = DiskDriver_getFreeBlock(disk,0);		

  return 0;
}


// frees a block in position block_num, and alters the bitmap accordingly, returns -1 if operation not possible
int DiskDriver_freeBlock(DiskDriver* disk, int block_num) {

	// Se il blocco che devo liberare non fa parte del mio disk, restituisco -1
	if(block_num > disk->header->num_blocks) return -1;

	// Incremento il numero di blocchi liberi nel DiskHeader
	if(DiskDriver_getFreeBlock(disk,block_num-1) != block_num) disk->header->free_blocks++;

	// Creo la bitmap che andrò ad utilizzare per la BitMap_get()
	BitMap bitmap;
	bitmap.num_bits = disk->header->bitmap_entries * 8;
	bitmap.entries = disk->bitmap_data;

	// Imposto il blocco come libero nella BitMap
	BitMap_set(&bitmap, block_num, 0);
	disk->bitmap_data = bitmap.entries;
	DiskDriver_flush(disk);

	// Nel caso in cui il blocco è precedente a quello salvato in DiskHeader lo cambio
	if(block_num < disk->header->first_free_block) disk->header->first_free_block = block_num;

	return 0;
}


// returns the first free block in the disk from position (checking the bitmap)
int DiskDriver_getFreeBlock(DiskDriver* disk, int start) {
	
	// Creo la bitmap che andrò ad utilizzare per la BitMap_get()
	BitMap bitmap;
	bitmap.num_bits = disk->header->bitmap_entries * 8;
	bitmap.entries = disk->bitmap_data;
	
	// Controllo che l'indice start non sia maggiore dei blocchi disponibili
	if(start > disk->header->num_blocks) return -1;

	// Controllo che DiskHeader sia inizzializzato
	if(disk->header->num_blocks <= 0 ) return -1;

	// Controlliamo nella BitMap quale è il primo blocco libero
	return BitMap_get(&bitmap, start, 0);
	
}

// writes the data (flushing the mmaps)
int DiskDriver_flush(DiskDriver* disk) {
	
	// Calcolo la lunghezza della memoria da sincronizzare
	int disk_size = sizeof(DiskHeader) + disk->header->bitmap_entries + (disk->header->num_blocks*BLOCK_SIZE) ;

	// Sincronizzo la memoria modificata ed il file collegato dalla mmap()
	return msync(disk->header, disk_size, MS_SYNC);

}
