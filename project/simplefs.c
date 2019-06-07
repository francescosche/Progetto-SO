#include "simplefs.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h> 
#include <stdlib.h>

// initializes a file system on an already made disk
// returns a handle to the top level directory stored in the first block
DirectoryHandle* SimpleFS_init(SimpleFS* fs, DiskDriver* disk) {

	if(fs == NULL || disk == NULL) return NULL;

	// Interpreto il disco passato in parametro come disco principale del FileSystem
	fs->disk = disk;
	DirectoryHandle * directory_handle = malloc(sizeof(DirectoryHandle));	
	directory_handle->sfs = fs;

	// Inserirò la radice sempre al primo posto della bitmap, nel caso già esiste la leggo solamente		
	if(fs->disk->header->first_free_block != 0){
		FirstDirectoryBlock * first_directory_block = malloc(sizeof(FirstDirectoryBlock));
		DiskDriver_readBlock(disk, first_directory_block, 0);
		directory_handle->dcb = first_directory_block;
		directory_handle->directory = NULL;
		directory_handle->current_block = &(directory_handle->dcb->header);
		directory_handle->pos_in_dir = 0;
		directory_handle->pos_in_block = first_directory_block->fcb.block_in_disk;
	}else{ // Nel caso in cui il filesystem non esiste, formatto il FileSystem
	//
		// Mi formatto il FileSystem ricevuto come parametro
		SimpleFS_format(fs);

		// Mi creo un DirectoryHandle e ci collego il FileSystem attuale
		directory_handle->sfs = fs;
	
		// Recupero la FirstDirectoryBlock memorizzata su disco e memorizzo le sue informazioni
		FirstDirectoryBlock * first_directory_block = malloc(sizeof(FirstDirectoryBlock));
		DiskDriver_readBlock(disk, first_directory_block, 0);
		directory_handle->dcb = first_directory_block;
		directory_handle->directory = NULL;
		directory_handle->current_block = &(directory_handle->dcb->header);
		directory_handle->pos_in_dir = 0;
		directory_handle->pos_in_block = first_directory_block->fcb.block_in_disk;
	}

	// Restituisco il DirectoryHandle popolato
	return directory_handle;
}


// creates the inital structures, the top level directory
// has name "/" and its control block is in the first position
// it also clears the bitmap of occupied blocks on the disk
// the current_directory_block is cached in the SimpleFS struct
// and set to the top level directory
void SimpleFS_format(SimpleFS* fs) {

	if(fs == NULL) return;

	// Azzero la BitMap di tutto il disco
	int i;
	BitMap bitmap;
	bitmap.num_bits = fs->disk->header->num_blocks;
	bitmap.entries = fs->disk->bitmap_data;
	for(i = 0; i < bitmap.num_bits; i++) {
		BitMap_set(&bitmap, i, 0);
	}
	fs->disk->bitmap_data = bitmap.entries;
	
	// Creo il primo blocco della cartella "base"
	FirstDirectoryBlock * first_directory_block = malloc(sizeof(FirstDirectoryBlock));

	// Inserisco le informazioni relative all'header
	first_directory_block->header.previous_block = -1;
	first_directory_block->header.next_block = -1;
	first_directory_block->header.block_in_file = 0; 

	// Inserisco le informazioni relative al FileControlBlock
	first_directory_block->fcb.directory_block = -1;
	first_directory_block->fcb.block_in_disk = fs->disk->header->first_free_block;
  strcpy(first_directory_block->fcb.name,"/");
  first_directory_block->fcb.size_in_bytes = sizeof(FirstDirectoryBlock);
  first_directory_block->fcb.size_in_blocks = count_blocks(first_directory_block->fcb.size_in_bytes);
  first_directory_block->fcb.is_dir = 1;
	first_directory_block->num_entries = 0;
	memset(first_directory_block->file_blocks, 0, sizeof(first_directory_block->file_blocks));

	// Memorizziamo la FirstDirectoryBlock nel disco
	DiskDriver_writeBlock(fs->disk, first_directory_block, fs->disk->header->first_free_block);
	DiskDriver_flush(fs->disk);	

	return;
}

// creates an empty file in the directory d
// returns null on error (file existing, no free blocks)
// an empty file consists only of a block of type FirstBlock
FileHandle* SimpleFS_createFile(DirectoryHandle* d, const char* filename) {

	// Se uno dei parametri è vuoto, esco senza fare nulla
	if(d == NULL || filename == NULL) return NULL;

	// Se esiste già un file con lo stesso nome, restituisco errore
	if(SimpleFS_openFile(d, filename) != NULL) return NULL;

	// Se non ci sono blocchi liberi per creare il file, restituisco errore
	if(d->sfs->disk->header->free_blocks < 1) return NULL; 
	
	if(SimpleFS_openFile(d, filename) != NULL) return NULL;

	// Creo il FileHandle e inserisco le informazioni relative
	FileHandle * file_handle = malloc(sizeof(FileHandle));
	file_handle->sfs = d->sfs;
	FirstFileBlock * first_file_block = malloc(sizeof(FirstFileBlock)); 
	first_file_block->header.previous_block = -1;
	first_file_block->header.next_block = -1;
	first_file_block->header.block_in_file = 0;
	first_file_block->fcb.directory_block = d->dcb->fcb.block_in_disk;
	first_file_block->fcb.block_in_disk = DiskDriver_getFreeBlock(d->sfs->disk, 0);
	strcpy(first_file_block->fcb.name, filename);
	first_file_block->fcb.size_in_bytes = 0;
	first_file_block->fcb.size_in_blocks = count_blocks(first_file_block->fcb.size_in_bytes);
  first_file_block->fcb.is_dir = 0;
	strcpy(first_file_block->data,"\0");
	file_handle->fcb = first_file_block;
	file_handle->directory = d->dcb;
	file_handle->current_block = &(first_file_block->header);
	file_handle->pos_in_file = 0;

	// Se c'è spazio nel blocco corrente della cartella attuale
	//    Aggiungo il primo blocco della nuova cartella
	// Altrimenti
	//    Se c'è un blocco successivo
	//       Accedo all'ultimo blocco disponibile della cartella attuale
	//    Altrimenti
  //       Se non c'è abbastanza spazio
	//          Mi creo un nuovo blocco della cartella attuale
  //          Aggiorno il blocco precedente in modo che punti a quello appena creato
	//    Assegno, all'interno dell'ultimo "file_blocks" della cartella corrente,
  //    l'indice del blocco in cui è memorizzata la cartella

	DiskDriver_writeBlock(d->sfs->disk, first_file_block, first_file_block->fcb.block_in_disk);
	if(space_in_dir(d->dcb->file_blocks, sizeof(d->dcb->file_blocks))) {
		//TODO: il file va inserito nel primo spazio libero dentro file blocks
		d->dcb->file_blocks[d->dcb->num_entries] = first_file_block->fcb.block_in_disk;
		d->dcb->num_entries++;
		DiskDriver_writeBlock(d->sfs->disk, d->dcb, d->dcb->fcb.block_in_disk);
	}else{
		int db_block = d->dcb->fcb.block_in_disk;
		DirectoryBlock * db;
		int new_db_block;	
		if(d->dcb->header.next_block != -1){
			db_block = d->dcb->header.next_block;
			db = malloc(sizeof(DirectoryBlock));
			DiskDriver_readBlock(d->sfs->disk, db, d->dcb->header.next_block);
			while(db->header.next_block != -1){
				db_block = db->header.next_block;
				DiskDriver_readBlock(d->sfs->disk, db, db_block);
			}
		}else if(!space_in_dir(db->file_blocks, sizeof(d->dcb->file_blocks))){ // In questo punto, dentro db abbiamo l'ultimo blocco esistente della cartella
			new_db_block = DiskDriver_getFreeBlock(d->sfs->disk, 0);
			DirectoryBlock * directory_block = malloc(sizeof(DirectoryBlock));
			directory_block->header.next_block = -1;
			directory_block->header.previous_block = db_block; 
			directory_block->header.block_in_file = db->header.block_in_file + 1;

			// Aggiorno il next_block del vecchio DirectoryBlock e lo sovrascrivo/aggiorno sul suo blocco
			db->header.next_block = new_db_block;
			DiskDriver_writeBlock(d->sfs->disk, db, db_block);

			// Adatto i puntatori e i valori in modo che possano funzionare all'esterno
			db = directory_block;
			db_block = new_db_block;
		}
		db->file_blocks[d->dcb->num_entries] = file_handle->fcb->fcb.block_in_disk;
		d->dcb->num_entries++;

		// Scrivo, su un nuovo blocco (libero) la DirectoryBlock appena creata
		DiskDriver_writeBlock(d->sfs->disk, db, db_block);
	}
	DiskDriver_flush(d->sfs->disk);
	return file_handle;
}

// reads in the (preallocated) blocks array, the name of all files in a directory
int SimpleFS_readDir(char** names, DirectoryHandle* d) {

	// Se uno dei parametri è vuoto, esco senza fare nulla
	if(names == NULL || d == NULL) return -1;

	FirstDirectoryBlock * db = malloc(sizeof(FirstDirectoryBlock));
	db = d->dcb;

	// Per ogni file
	//    Se è presente nel blocco corrente
	//       Memorizzo il nome nell'array
	//    Se non è presente nel blocco corrente
	//       Entro nel blocco successivo
	//       Memorizzo il nome nell'array
	int i, j = 0;
	for(i = 0; i < d->dcb->num_entries; i++, j++) {
		if(j >= sizeof(db->file_blocks)) {
			DirectoryBlock * db;
			j = j - sizeof(db->file_blocks);
			DiskDriver_readBlock(d->sfs->disk, db, db->header.next_block);
		}
		FirstFileBlock * first_file_block = malloc(sizeof(FirstFileBlock));
		DiskDriver_readBlock(d->sfs->disk, first_file_block, db->file_blocks[j]);
		names[j] = first_file_block->fcb.name;
	}

	return 0;
}

// opens a file in the  directory d. The file should be exisiting
FileHandle* SimpleFS_openFile(DirectoryHandle* d, const char* filename) {

	// Se uno dei parametri è vuoto, esco senza fare nulla
	if(d == NULL || filename == NULL) return NULL;

	// Memorizzo le informazioni della cartella
	FileHandle * file_handle = malloc(sizeof(FileHandle));
	FirstDirectoryBlock * db = malloc(sizeof(FirstDirectoryBlock));
	db = d->dcb;

	// Per ogni file
	//    Se ho superato la dimensione del blocco corrente
	//       Memorizzo le informazioni del blocco successivo
	//    Memorizzo le informazioni del primo blocco del file
	//    Se il nome corrisponde a quello da aprire, e non è una cartella
	//       Memorizzo nel file_handle le informazioni di questo file
	//       Restituisco il file
	//    Altrimenti
	//       Restituisco NULL
	int i, j = 0;
	for(i = 0; i < d->dcb->num_entries; i++, j++) {
		if(j >= sizeof(db->file_blocks)) { 
			DirectoryBlock * db;
			j = j - sizeof(db->file_blocks);
			DiskDriver_readBlock(d->sfs->disk, db, db->header.next_block);
		}
		FirstFileBlock * first_file_block = malloc(sizeof(FirstFileBlock));
		DiskDriver_readBlock(d->sfs->disk, first_file_block, db->file_blocks[j]);
		if(strcmp(first_file_block->fcb.name, filename) == 0 && first_file_block->fcb.is_dir == 0){
			file_handle->sfs = d->sfs;
			file_handle->fcb = first_file_block;
			file_handle->directory = d->dcb;
			file_handle->current_block = &(first_file_block->header);
			file_handle->pos_in_file = 0;
			return file_handle;
		}
	}
	return NULL;
}

// closes a file handle (destroyes it)
int SimpleFS_close(FileHandle* f) {

	// Se il parametro è vuoto, esco senza fare nulla
	if(f == NULL) return -1;

	// Libero tutto lo spazio occupato dal FileHandle
	free(f);

	// Esco dalla funzione
	return 0;
}

// writes in the file, at current position for size bytes stored in data
// overwriting and allocating new space if necessary
// returns the number of bytes written
// TODO: Fare in modo che la scrittura inizi da pos, e non dall'inizio
int SimpleFS_write(FileHandle* f, void* data, int size) {

	// Se uno dei parametri è vuoto, esco senza fare nulla
	if(f == NULL || data == NULL || size < 0) return -1;

	int written_bytes = 0, written_blocks = 0;
	if(size < sizeof(f->fcb->data)) {
		memcpy(f->fcb->data, data, strlen(data));
		DiskDriver_writeBlock(f->sfs->disk, f->fcb, f->fcb->fcb.block_in_disk);
		written_bytes = strlen(data);
		written_blocks++;
	}else{
		int dim, current_block;
		char * copy = malloc(strlen(data));
		strcpy(copy, data);
		int previous_block = f->fcb->fcb.block_in_disk;
		int next_block = f->fcb->header.next_block;
		int previous_index = f->fcb->header.block_in_file;

		dim = sizeof(f->fcb->data);
		memcpy(f->fcb->data, copy, dim);
		DiskDriver_writeBlock(f->sfs->disk, f->fcb, f->fcb->fcb.block_in_disk);
		copy += dim;
		written_bytes += dim;
		written_blocks++;

		FileBlock * file = malloc(sizeof(FileBlock));
		dim = strlen(copy) > sizeof(file->data) ? sizeof(file->data) : strlen(copy);
		while(dim != 0) {
			if(next_block != -1) {
				current_block = next_block;
				DiskDriver_readBlock(f->sfs->disk, file, next_block);
				next_block = file->header.next_block;
				memset(file->data, '\0', sizeof(file->data));
				memcpy(file->data, copy, dim);
				copy += dim;
				written_bytes += dim;
				DiskDriver_writeBlock(f->sfs->disk, file, current_block);
				previous_block = current_block;
			}else{
				current_block = DiskDriver_getFreeBlock(f->sfs->disk, 0);
				if(previous_index == 0) {
					f->fcb->header.next_block = current_block;
					DiskDriver_writeBlock(f->sfs->disk, f->fcb, f->fcb->fcb.block_in_disk);
				}else{
					FileBlock * fb = malloc(sizeof(FileBlock));
					DiskDriver_readBlock(f->sfs->disk, fb, previous_block);
					fb->header.next_block = current_block;
					DiskDriver_writeBlock(f->sfs->disk, fb, previous_block);
				}
				file->header.previous_block = previous_block;
				next_block = -1;
				file->header.next_block = next_block;
				previous_index += 1;
				file->header.block_in_file = previous_index;
				memset(file->data, '\0', sizeof(file->data));
				memcpy(file->data, copy, dim);
				copy += dim;
				written_bytes += dim;
				DiskDriver_writeBlock(f->sfs->disk, file, current_block);
				previous_block = current_block;
			}
			dim = strlen(copy) > sizeof(file->data) ? sizeof(file->data) : strlen(copy);
			written_blocks++;
		}
	}
	f->fcb->fcb.size_in_bytes = written_bytes;
	f->fcb->fcb.size_in_blocks = written_blocks;
	DiskDriver_writeBlock(f->sfs->disk, f->fcb, f->fcb->fcb.block_in_disk);
	return written_bytes;
}

// reads in the file, at current position size bytes stored in data
// returns the number of bytes read
int SimpleFS_read(FileHandle* f, char* data, int size) {

	// Se uno dei parametri è vuoto, esco senza fare nulla
	if(f == NULL || data == NULL || size < 0) return -1;

	// Memorizzo il primo blocco del file da leggere
	FirstFileBlock * ffb = malloc(sizeof(FirstFileBlock));
	ffb = f->fcb;
	int next_block = f->fcb->header.next_block;

	// Formatto la stringa da restituire
	memset(data, '\0', size);
	if(size < strlen(ffb->data)) {
		strcpy(data, ffb->data);
	}else{
		strcpy(data, ffb->data);
		FileBlock * file = malloc(sizeof(FileBlock));
		while(strlen(data) < size && next_block != -1) {
			DiskDriver_readBlock(f->sfs->disk, file, next_block);
			sprintf(data, "%s%s", data, file->data);
			next_block = file->header.next_block;
		}
	}

	// Restituisco la lunghezza della stringa letta
	return strlen(data);
}

// returns the number of bytes read (moving the current pointer to pos)
// returns pos on success
// -1 on error (file too short)
int SimpleFS_seek(FileHandle* f, int pos) {

	// Se uno dei parametri è vuoto, esco senza fare nulla
	if(f == NULL || pos < 0) return -1;

	int dim = 0;

	FirstFileBlock * ffb = malloc(sizeof(FirstFileBlock));
	ffb = f->fcb;
	dim += sizeof(ffb->data);

	if(ffb->header.next_block != -1) {
		FileBlock * file = malloc(sizeof(FileBlock));
		DiskDriver_readBlock(f->sfs->disk, file, ffb->header.next_block);
		dim += sizeof(file->data);
		while(file->header.next_block != -1) {
			DiskDriver_readBlock(f->sfs->disk, file, file->header.next_block);
			dim += sizeof(file->data);
		}
	}

	// Controllo se pos rientra nel file
	//	In caso negativo ritorno -1
	//  Altrimenti sposto il puntatore
	if(pos + f->pos_in_file > dim){
		return -1;	
	}else{
		f->pos_in_file = pos + f->pos_in_file;
		return pos;
	}

}

// seeks for a directory in d. If dirname is equal to ".." it goes one level up
// 0 on success, negative value on error
// it does side effect on the provided handle
int SimpleFS_changeDir(DirectoryHandle* d, char* dirname) {

	// Se uno dei parametri è vuoto, esco senza fare nulla
	if(d == NULL || dirname == NULL) return -1;

	// Nel caso in cui dirname è ".." torno alla cartella genitore modificando Directory_Handle
	if(strcmp(dirname,"..") == 0){
		// Se ci troviamo nella radice restituisco -1
		if(strcmp(d->dcb->fcb.name,"/") == 0)
			return -1;
		else{
			FirstDirectoryBlock * parent_dir = malloc(sizeof(FirstDirectoryBlock));
			DiskDriver_readBlock(d->sfs->disk, parent_dir, d->directory->fcb.block_in_disk);
			d->dcb = d->directory;
			d->current_block = &(d->dcb->header);
			d->pos_in_dir = 0;
			d->pos_in_block = d->dcb->fcb.block_in_disk;
			d->directory = parent_dir;
			return 0;
		}
	}else{ // Caso in cui dirname è diverso da ".."
		// Controllo la directory esiste
		//		In caso positivo mi ci sposto e modifico DirectoryHandle
		// Se non esiste ritorno -1
	 	int block = DirectoryExist(d, dirname);
	 	if(block != -1){
			FirstDirectoryBlock * child_dir = malloc(sizeof(FirstDirectoryBlock));
			DiskDriver_readBlock(d->sfs->disk, child_dir, block);
			d->directory = d->dcb;
			d->current_block = &(d->dcb->header);
			d->pos_in_dir = 0;
			d->pos_in_block = block;
			d->dcb = child_dir;
			return 0;
		}else{
			return -1;			
		}	
	}
}

int DirectoryExist(DirectoryHandle * d, char * dirname){

	// Se uno dei parametri è vuoto, esco senza fare nulla
	if(d == NULL || dirname == NULL) return -1;

	// Se esiste già una cartella con lo stesso nome, restituisco -1
	int i, j;
	FirstDirectoryBlock * db = malloc(sizeof(FirstDirectoryBlock));
	DiskDriver_readBlock(d->sfs->disk, db, d->dcb->fcb.block_in_disk);

	for(i = 0, j = 0; i < d->dcb->num_entries; i++, j++) {
		if(j >= sizeof(db->file_blocks)) { 
			DirectoryBlock * db;
			j = j - sizeof(db->file_blocks);
			DiskDriver_readBlock(d->sfs->disk, db, db->header.next_block);
		}
		FirstDirectoryBlock * first_dir_block = malloc(sizeof(FirstDirectoryBlock));
		DiskDriver_readBlock(d->sfs->disk, first_dir_block, db->file_blocks[j]);

		// Controllo se la directory esiste
		if(strcmp(first_dir_block->fcb.name,dirname)==0 && first_dir_block->fcb.is_dir == 1){
			return first_dir_block->fcb.block_in_disk;
		}
	}
	return -1;
}

// creates a new directory in the current one (stored in fs->current_directory_block)
// 0 on success
// -1 on error
int SimpleFS_mkDir(DirectoryHandle* d, char* dirname) {

	// Se uno dei parametri è vuoto, esco senza fare nulla
	if(d == NULL || dirname == NULL) return -1;

	// Se non ci sono blocchi liberi per creare il file, restituisco errore
	if(d->sfs->disk->header->free_blocks < 1){
		return -1; 
	}

	// Se esiste già una cartella con lo stesso nome, restituisco -1
	if(DirectoryExist(d,dirname) != -1) return -1;

	FirstDirectoryBlock * fdb = malloc(sizeof(FirstDirectoryBlock));
	fdb->header.previous_block = -1;
	fdb->header.next_block = -1;
	fdb->header.block_in_file = 0;
	fdb->fcb.directory_block = d->dcb->fcb.block_in_disk;
	fdb->fcb.block_in_disk = DiskDriver_getFreeBlock(d->sfs->disk, 0);
	strcpy(fdb->fcb.name, dirname);
	fdb->fcb.size_in_bytes = 0;
	fdb->fcb.size_in_blocks = 0;
	fdb->fcb.is_dir = 1;
	fdb->num_entries = 0;
	int i;
	memset(fdb->file_blocks, 0 , sizeof(fdb->file_blocks));
	DiskDriver_writeBlock(d->sfs->disk, fdb, fdb->fcb.block_in_disk);

	// Se c'è spazio nel blocco corrente della cartella attuale
	//    Aggiungo il primo blocco della nuova cartella
	// Altrimenti
	//    Se c'è un blocco successivo
	//       Accedo all'ultimo blocco disponibile della cartella attuale
	//    Altrimenti
  //       Se non c'è abbastanza spazio
	//          Mi creo un nuovo blocco della cartella attuale
  //          Aggiorno il blocco precedente in modo che punti a quello appena creato
	//    Assegno, all'interno dell'ultimo "file_blocks" della cartella corrente,
  //    l'indice del blocco in cui è memorizzata la cartella

	if(space_in_dir(d->dcb->file_blocks, sizeof(d->dcb->file_blocks))) {
		d->dcb->file_blocks[d->dcb->num_entries] = fdb->fcb.block_in_disk;
		d->dcb->num_entries++;
		DiskDriver_writeBlock(d->sfs->disk, d->dcb, d->dcb->fcb.block_in_disk);
	}else{
		int db_block = d->dcb->fcb.block_in_disk;
		DirectoryBlock * db;
		int new_db_block;	
		if(d->dcb->header.next_block != -1){
			db_block = d->dcb->header.next_block;
			db = malloc(sizeof(DirectoryBlock));
			DiskDriver_readBlock(d->sfs->disk, db, d->dcb->header.next_block);
			while(db->header.next_block != -1){
				db_block = db->header.next_block;
				DiskDriver_readBlock(d->sfs->disk, db, db_block);
			}
		}else if(!space_in_dir(db->file_blocks, sizeof(d->dcb->file_blocks))){ // In questo punto, dentro db abbiamo l'ultimo blocco esistente della cartella
			new_db_block = DiskDriver_getFreeBlock(d->sfs->disk, 0);
			DirectoryBlock * directory_block = malloc(sizeof(DirectoryBlock));
			directory_block->header.next_block = -1;
			directory_block->header.previous_block = db_block; 
			directory_block->header.block_in_file = db->header.block_in_file + 1;

			// Aggiorno il next_block del vecchio DirectoryBlock e lo sovrascrivo/aggiorno sul suo blocco
			db->header.next_block = new_db_block;
			DiskDriver_writeBlock(d->sfs->disk, db, db_block);

			// Adatto i puntatori e i valori in modo che possano funzionare all'esterno
			db = directory_block;
			db_block = new_db_block;
		}
		db->file_blocks[d->dcb->num_entries] = fdb->fcb.block_in_disk;
		d->dcb->num_entries++;

		// Scrivo, su un nuovo blocco (libero) la DirectoryBlock appena creata
		DiskDriver_writeBlock(d->sfs->disk, db, db_block);
	}
	DiskDriver_flush(d->sfs->disk);

	return 0;
}

// removes the file in the current directory
// returns -1 on failure 0 on success
// if a directory, it removes recursively all contained files
int SimpleFS_remove(DirectoryHandle* d, char* filename) {

	printf("%s => ", filename);

	// Se uno dei parametri è vuoto, esco senza fare nulla
	if(d == NULL || filename == NULL) return -1;

	int current_block, next_block, i, j;
// TODO: Impossibile verificare questo if
	// Se la cartella un blocco successivo
	if(d->dcb->header.next_block != -1) {
		int next_dir_block = d->dcb->header.next_block;
		DirectoryBlock * db = malloc(sizeof(DirectoryBlock));
		while(next_dir_block != -1) {
			DiskDriver_readBlock(d->sfs->disk, db, next_dir_block);
			for(i = 0; i < sizeof(db->file_blocks); i++) {
				if(db->file_blocks[i] > 0) {

					// Memorizzo le informazioni del primo blocco del file
					FirstFileBlock * file = malloc(sizeof(FirstFileBlock));
					DiskDriver_readBlock(d->sfs->disk, file, db->file_blocks[i]);
					current_block = file->fcb.block_in_disk;

					if(strcmp(file->fcb.name, filename) == 0){
						// Se è un file, cancello tutti i blocchi del file stesso
						if(file->fcb.is_dir == 0) {
							if(file->header.next_block != -1) {
								do {
									next_block = file->header.next_block;
									DiskDriver_freeBlock(d->sfs->disk, current_block);
									if(next_block != -1) DiskDriver_readBlock(d->sfs->disk, file, next_block);
									current_block = next_block;
								} while(next_block != -1);
								return 0;
							}else{
								DiskDriver_freeBlock(d->sfs->disk, current_block);
							}
						}else{ // Se è una cartella, cancello tutti i file al suo interno richiamando la funzione ricorsivamente su tutti i file contenuti
							FirstDirectoryBlock * fdb2 = malloc(sizeof(FirstDirectoryBlock));
							DiskDriver_readBlock(d->sfs->disk, fdb2, db->file_blocks[i]);
							DirectoryHandle * dh = malloc(sizeof(DirectoryHandle));
							dh->sfs = d->sfs;
							dh->dcb = fdb2;
							dh->directory = d->dcb;
							dh->current_block = &fdb2->header;
							dh->pos_in_dir = 0;
							dh->pos_in_block = 0;
							if(fdb2->header.next_block != -1) {
								for(j = 0; j < sizeof(fdb2->file_blocks); j++) {
									if(db->file_blocks[i] > 0) {
										FirstFileBlock * file_to_delete = malloc(sizeof(FirstFileBlock));
										DiskDriver_readBlock(d->sfs->disk, file_to_delete, fdb2->file_blocks[j]);
										SimpleFS_remove(dh, file_to_delete->fcb.name);
									}
								}
								DirectoryBlock * db2 = malloc(sizeof(DirectoryBlock));
								int next_db2_block = fdb2->header.next_block;
								while(next_db2_block != -1) {
									DiskDriver_readBlock(d->sfs->disk, db2, next_db2_block);
									for(j = 0; j < sizeof(db2->file_blocks); j++) {
										if(db->file_blocks[i] > 0) {
											FirstFileBlock * file_to_delete = malloc(sizeof(FirstFileBlock));
											DiskDriver_readBlock(d->sfs->disk, file_to_delete, fdb2->file_blocks[j]);
											SimpleFS_remove(dh, file_to_delete->fcb.name);
										}
									}
								}
							}else{
								for(j = 0; j < sizeof(fdb2->file_blocks); j++) {
									if(fdb2->file_blocks[i] > 0) {
										FirstFileBlock * file_to_delete = malloc(sizeof(FirstFileBlock));
										DiskDriver_readBlock(d->sfs->disk, file_to_delete, fdb2->file_blocks[j]);
										SimpleFS_remove(dh, file_to_delete->fcb.name);
									}
								}
							}
						}
					}
				}
			}
			next_dir_block = db->header.next_block;
		}
// TODO: Fine
//				Perché servirebbero più di 250 blocchi e inserire file nella cartella da cancellare
	}else{
		// Per ogni elemento contenuto nel primo blocco
		int remaining_entries;
		for(i = 0, remaining_entries = d->dcb->num_entries; i < sizeof(d->dcb->file_blocks); i++) {
			// Se c'è un file memorizzato
			if(d->dcb->file_blocks[i] > 0 && remaining_entries) {
				remaining_entries--;
				// Memorizzo le informazioni del file
				FirstFileBlock * file_to_delete = malloc(sizeof(FirstFileBlock));
				DiskDriver_readBlock(d->sfs->disk, file_to_delete, d->dcb->file_blocks[i]);
				current_block = d->dcb->file_blocks[i];
				printf(" %d => %s[%d]",i, file_to_delete->fcb.name, d->dcb->file_blocks[i]);
				// Se è il file che sto cercando
				if(strcmp(file_to_delete->fcb.name, filename) == 0){
					printf(" => CORRISPONDE");
					// Se non si tratta di una cartella
					if(file_to_delete->fcb.is_dir == 0) {
						// Se il file ha più di un blocco
						if(file_to_delete->header.next_block != -1) {
							do {
								// Memorizzo il prossimo blocco
								next_block = file_to_delete->header.next_block;
								// Cancello il blocco attuale
								DiskDriver_freeBlock(d->sfs->disk, current_block);
								// Se esiste un blocco successivo, lo leggo
								if(next_block != -1) DiskDriver_readBlock(d->sfs->disk, file_to_delete, next_block);
								// Imposto il blocco successivo come attuale
								current_block = next_block;
							} while(next_block != -1);
						}else{
							// Se ha un solo blocco, lo cancello
							DiskDriver_freeBlock(d->sfs->disk, current_block);
						}
						d->dcb->file_blocks[i] = 0;
						// Dopo aver canncellato tutti i blocchi del file, restituisco 0
						return 0;
					}else{
						// Se è una cartella, memorizzo le informazioni del suo primo blocco
						FirstDirectoryBlock * fdb2 = malloc(sizeof(FirstDirectoryBlock));
						DiskDriver_readBlock(d->sfs->disk, fdb2, d->dcb->file_blocks[i]);
						// Creo un DirectoryHandle da passare alle chiamate ricorsive
						DirectoryHandle * dh = malloc(sizeof(DirectoryHandle));

						printf("\nPORCAAAAA");
						for(i=0; i<fdb2->num_entries; i++){
							printf("\n%d => %d", i, fdb2->file_blocks[i]);
						}

						dh->sfs = d->sfs;
						dh->dcb = fdb2;
						FirstDirectoryBlock * parent_folder = malloc(sizeof(FirstDirectoryBlock));
						DiskDriver_readBlock(d->sfs->disk, parent_folder, fdb2->fcb.directory_block);
						dh->directory = parent_folder;
						dh->current_block = &fdb2->header;
						dh->pos_in_dir = 0;
						dh->pos_in_block = 0;
						// Se la cartella è formata da più blocchi
// TODO:	INIZIO parte che non possiamo controllare
//				Perché servirebbero più di 250 blocchi e inserire file nella cartella da cancellare
						if(fdb2->header.next_block != -1) {
							for(j = 0; j < sizeof(fdb2->file_blocks); j++) {
								if(d->dcb->file_blocks[i] > 0) {
									FirstFileBlock * file_to_delete = malloc(sizeof(FirstFileBlock));
									DiskDriver_readBlock(d->sfs->disk, file_to_delete, fdb2->file_blocks[j]);
									SimpleFS_remove(dh, file_to_delete->fcb.name);
								}
							}
							DirectoryBlock * db2 = malloc(sizeof(DirectoryBlock));
							int next_db2_block = fdb2->header.next_block;
							while(next_db2_block != -1) {
								DiskDriver_readBlock(d->sfs->disk, db2, next_db2_block);
								for(j = 0; j < sizeof(db2->file_blocks); j++) {
									if(d->dcb->file_blocks[j] > 0) {
										FirstFileBlock * file_to_delete = malloc(sizeof(FirstFileBlock));
										DiskDriver_readBlock(d->sfs->disk, file_to_delete, fdb2->file_blocks[j]);
										SimpleFS_remove(dh, file_to_delete->fcb.name);
									}
								}
							}
// TODO: FINE
						}else{
							int remaining_entries2;
							// Se invece è formata da un solo blocco, leggo tutti i suoi elementi
							for(j = 0, remaining_entries2 = fdb2->num_entries; j < sizeof(fdb2->file_blocks); j++) {
								// Se l'elemento contiene un blocco figlio
								printf("\n%d", fdb2->file_blocks[j]);
								if(fdb2->file_blocks[j] > 0 && remaining_entries2) {
									// Memorizzo le informazioni del suo figlio
									FirstFileBlock * file_to_delete = malloc(sizeof(FirstFileBlock));
									DiskDriver_readBlock(d->sfs->disk, file_to_delete, fdb2->file_blocks[j]);
									printf("\n%d) Cancello %d - %s[%d] => ",remaining_entries2,j,file_to_delete->fcb.name, file_to_delete->fcb.block_in_disk);
									// Chiamo ricorsivamente la funzione con il nome del figlio appena letto
									SimpleFS_remove(dh, file_to_delete->fcb.name);
									remaining_entries2--;
								}
							}
							// TODO: Cancellare, dalla cartella genitore, il record di questa cartella in file_blocks
							// Dopo aver cancellato tutti i figli, cancello anche la cartella stessa
							DiskDriver_freeBlock(d->sfs->disk, fdb2->fcb.block_in_disk);
							return 0;
						}
					}
				}
			}
		}
	}

	// TODO: Se viene cancellato un file e la sua cartella genitore aveva solo quel figlio, cancellare anche la cartella genitore e aggiornare i blocchi precedenti e successivi della stessa cartella

	return -1;
}
