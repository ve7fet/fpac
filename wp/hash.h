int hash_open(int hash_bits, int record_size, int key_offset, int key_size, void **record_table);
int hash_close(int handle);
int hash_put(int handle, unsigned short index);
int hash_search(int handle, void *key);
void hash_dump(int handle, void (*record_dump)(int index));
