typedef struct {
    void **data;
    unsigned capacity;
    unsigned length;
} arraylist_t;

int al_init (arraylist_t *list, unsigned cap);
int al_destroy (arraylist_t *list);


unsigned al_length (arraylist_t *list);
int al_append (arraylist_t *list, void *item);
void *al_pop (arraylist_t *list);

// int al_pop (arraylist_t *list, void **dest);