int set_dirty_context(int record, int client);
int clear_dirty_context(int record, int client);
int find_dirty_context(int client);
int count_dirty_context(int client);
void broadcast_dirty(int record, int except);
void set_vector_when_nodirty(int client);
