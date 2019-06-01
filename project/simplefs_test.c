#include "bitmap.c"
#include "disk_driver.c"
#include "simplefs.c"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h> 
#include <fcntl.h> 
#include <unistd.h>
#include <stdlib.h>     

void stampa_in_binario(char* stringa) {
	int i, j;
	for(i = 0; i < strlen(stringa); i++) {
		char c = stringa[i];
		for (j = 7; j >= 0; j--) {
	      printf("%d", !!((c >> j) & 0x01)); 
	  }
	}
}

int count_blocks(int num_bytes) {
	return num_bytes % BLOCK_SIZE == 0 ? num_bytes / BLOCK_SIZE : ( num_bytes / BLOCK_SIZE ) + 1;
}

int space_in_dir(DirectoryBlock * db) {
	int i, free_spaces;
	while(i < sizeof(db->file_blocks)) {
		if(db->file_blocks[i] == 0) {
			free_spaces++;
		}
		i++;
	}
	return free_spaces;
}

int main(int agc, char** argv) {

	// Test DiskDriver_init   
	printf("\n+++ Test DiskDriver_init()");
	printf("\n+++ Test DiskDriver_getFreeBlock()");
	printf("\n+++ Test BitMap_get()");
	DiskDriver disk;
	DiskDriver_init(&disk, "test.txt", 15);
	BitMap bitmap;
	bitmap.num_bits = disk.header->bitmap_blocks;
	bitmap.entries = disk.bitmap_data;
	printf("\n    BitMap creata e inizializzata correttamente");
	printf("\n    Primo blocco libero => %d", disk.header->first_free_block); 


	// Test BitMap_blockToIndex
	int num = disk.header->num_blocks;
	printf("\n\n+++ Test BitMap_blockToIndex(%d)", num);   
	BitMapEntryKey block = BitMap_blockToIndex(num);
	printf("\n    La posizione del blocco è %d, ovvero entry %d con sfasamento %d", num, block.entry_num, block.bit_num);
 

	// Test BitMap_indexToBlock 
	printf("\n\n+++ Test BitMap_indexToBlock(block)");
	int posizione = BitMap_indexToBlock(block); 
	printf("\n    Abbiamo la entry %d e lo sfasamento %d, ovvero la posizione %d", block.entry_num, block.bit_num, posizione);


	// Test DiskDriver_freeBlock
	printf("\n\n+++ Test DiskDriver_freeBlock()");
	printf("\n    Libero il blocco %d, la funzione ritorna: %d",0,DiskDriver_freeBlock(&disk,0));

	printf("\n    Prima della write => ");
	stampa_in_binario(bitmap.entries);
 

	// Test DiskDriver_writeBlock  
	printf("\n\n+++ Test DiskDriver_writeBlock()");
	printf("\n+++ Test DiskDriver_flush()");
	printf("\n    Il risultato della writeBlock(\"Ciao\", 0) è %d", DiskDriver_writeBlock(&disk, "Ciao", 0));

 
	// Test DiskDriver_readBlock
	printf("\n\n+++ Test DiskDriver_readBlock()");
	void * dest = malloc(BLOCK_SIZE);
	printf("\n    Controlliamo tramite una readBlock(dest, 0)   => %d", DiskDriver_readBlock(&disk, dest, 0));
	printf("\n    Dopo la readBlock, la dest contiene           => %s", (char *) dest);


	// Test BitMap_set
	printf("\n\n+++ Test BitMap_set()"); 
	printf("\n    Prima     => ");
	stampa_in_binario(bitmap.entries);
	BitMap_set(&bitmap, 10, 1); 
	printf("\n    Dopo (10) => ");
	stampa_in_binario(bitmap.entries);

  
	// Test BitMap_get
	printf("\n\n+++ Test BitMap_get()");
	int start = 6, status = 0;    
	printf("\n    Partiamo dalla posizione %d e cerchiamo %d => %d", start, status, BitMap_get(&bitmap, start, status));
	start = 3, status = 1;   
	printf("\n    Partiamo dalla posizione %d e cerchiamo %d => %d", start, status, BitMap_get(&bitmap, start, status));
	start = 12, status = 0;
	printf("\n    Partiamo dalla posizione %d e cerchiamo %d => %d", start, status, BitMap_get(&bitmap, start, status));
	start = 13, status = 1;
	printf("\n    Partiamo dalla posizione %d e cerchiamo %d => %d", start, status, BitMap_get(&bitmap, start, status));

	// Test SimpleFS_init   
	printf("\n\n+++ Test SimpleFS_init()");
	printf("\n+++ Test SimpleFS_format()");
	SimpleFS fs;
	DirectoryHandle * dir_handle = SimpleFS_init(&fs, &disk);
	printf("\n    File System creato e inizializzato correttamente");

	// Test SimpleFS_createFile
	printf("\n\n+++ Test SimpleFS_createFile()");
	SimpleFS_createFile(dir_handle,"ok.txt");
	printf("\nFile creato correttamente");

	// Test SimpleFS_readDir
	printf("\n\n+++ Test SimpleFS_readDir()");
	char ** elenco_files;
	SimpleFS_readDir(elenco_files, dir_handle);

	printf("\n\n");
}
