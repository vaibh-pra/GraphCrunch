#include "nausparse.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

/* =============================================================
   GLOBAL FILE POINTER
   ============================================================= */
FILE *outfile = NULL;

/* =============================================================
   DATA STRUCTURES
   ============================================================= */

// Simple queue for BFS
typedef struct {
    int *items;
    int front;
    int rear;
    int capacity;
} Queue;

// Dynamic array for Edge Classes
typedef struct {
    int *edges;   // Store edges as pairs (u, v)
    int size;     // Number of edges in this class
    int capacity; 
} EdgeClass;

// Dynamic array for Vertex Classes
typedef struct {
    int *vertices;
    int size;
    int capacity;
} VertexClass;

// Structure to sort edges for binary search (optimization)
typedef struct {
    int u, v;
    int class_id;
} EdgeEntry;

// Global container for graph and generators
typedef struct {
    int n;
    int num_generators;
    int max_generators;
    permutation *generators;
    sparsegraph sg;
} graph_data_t;

static graph_data_t global_graph_data;

/* =============================================================
   HELPER FUNCTIONS
   ============================================================= */

Queue *create_queue(int capacity) {
    Queue *queue = (Queue *)malloc(sizeof(Queue));
    if (!queue) exit(EXIT_FAILURE);
    queue->items = (int *)malloc(capacity * sizeof(int));
    if (!queue->items) exit(EXIT_FAILURE);
    queue->front = 0;
    queue->rear = 0;
    queue->capacity = capacity;
    return queue;
}

void enqueue(Queue *queue, int item) {
    if (queue->rear < queue->capacity) {
        queue->items[queue->rear++] = item;
    }
}

int dequeue(Queue *queue) {
    if (queue->front < queue->rear) return queue->items[queue->front++];
    return -1;
}

bool is_queue_empty(Queue *queue) {
    return queue->front == queue->rear;
}

void free_queue(Queue *queue) {
    free(queue->items);
    free(queue);
}

// --- Edge Class Helpers ---
EdgeClass *create_edge_class() {
    EdgeClass *ec = (EdgeClass *)malloc(sizeof(EdgeClass));
    if (!ec) exit(EXIT_FAILURE);
    ec->capacity = 4;
    ec->size = 0;
    ec->edges = (int *)malloc(ec->capacity * 2 * sizeof(int));
    if (!ec->edges) exit(EXIT_FAILURE);
    return ec;
}

void add_to_edge_class(EdgeClass *ec, int u, int v) {
    if (ec->size == ec->capacity) {
        int new_capacity = ec->capacity + (ec->capacity >> 1) + 1;
        ec->edges = (int *)realloc(ec->edges, new_capacity * 2 * sizeof(int));
        if (!ec->edges) exit(EXIT_FAILURE);
        ec->capacity = new_capacity;
    }
    ec->edges[ec->size * 2] = u;
    ec->edges[ec->size * 2 + 1] = v;
    ec->size++;
}

void free_edge_class(EdgeClass *ec) {
    free(ec->edges);
    free(ec);
}

// Comparator for qsort/bsearch
int compare_edges(const void *a, const void *b) {
    const EdgeEntry *ea = (const EdgeEntry *)a;
    const EdgeEntry *eb = (const EdgeEntry *)b;
    if (ea->u != eb->u) return ea->u - eb->u;
    return ea->v - eb->v;
}

EdgeEntry *find_edge_entry(EdgeEntry *entries, int num_entries, int u, int v) {
    if (u > v) { int temp = u; u = v; v = temp; }
    EdgeEntry key = {u, v, 0};
    return (EdgeEntry *)bsearch(&key, entries, num_entries, sizeof(EdgeEntry), compare_edges);
}

// --- Vertex Class Helpers ---
VertexClass *create_vertex_class() {
    VertexClass *vc = (VertexClass *)malloc(sizeof(VertexClass));
    if (!vc) exit(EXIT_FAILURE);
    vc->capacity = 4;
    vc->size = 0;
    vc->vertices = (int *)malloc(vc->capacity * sizeof(int));
    if (!vc->vertices) exit(EXIT_FAILURE);
    return vc;
}

void add_to_vertex_class(VertexClass *vc, int vertex) {
    if (vc->size == vc->capacity) {
        int new_capacity = vc->capacity + (vc->capacity >> 1) + 1;
        vc->vertices = (int *)realloc(vc->vertices, new_capacity * sizeof(int));
        if (!vc->vertices) exit(EXIT_FAILURE);
        vc->capacity = new_capacity;
    }
    vc->vertices[vc->size++] = vertex;
}

void free_vertex_class(VertexClass *vc) {
    free(vc->vertices);
    free(vc);
}

/* =============================================================
   NAUTY CALLBACK
   ============================================================= */

void my_userautomproc(int count, int *perm, int *orbits, int numorbits, int stabvertex, int n) {
    graph_data_t *data = &global_graph_data;
    
    // Resize generator storage if full
    if (data->num_generators >= data->max_generators) {
        int increment = 10;
        int growth = data->max_generators + (data->max_generators >> 1);
        data->max_generators = (growth > data->max_generators + increment) ? growth : data->max_generators + increment;
        
        permutation *new_generators = realloc(data->generators, data->max_generators * n * sizeof(permutation));
        if (new_generators == NULL) {
            fprintf(stderr, "Memory allocation failed for generators.\n");
            exit(EXIT_FAILURE);
        }
        data->generators = new_generators;
    }
    
    // Store the generator
    int generator_index = data->num_generators++;
    for (int i = 0; i < n; i++) {
        data->generators[generator_index * n + i] = perm[i];
    }
}

/* =============================================================
   COMPUTATION FUNCTIONS
   ============================================================= */

void compute_vertex_equivalence_classes(graph_data_t *data) {
    int n = data->n;
    int num_generators = data->num_generators;
    permutation *generators = data->generators;

    int num_classes = 0;
    int max_classes = 16;

    int *class_map = (int *)calloc(n, sizeof(int));
    VertexClass **vertex_classes = (VertexClass **)malloc(max_classes * sizeof(VertexClass *));
    
    if (!class_map || !vertex_classes) exit(EXIT_FAILURE);

    Queue *queue = create_queue(n);

    for (int v = 0; v < n; v++) {
        if (class_map[v] != 0) continue; // Already classified

        if (num_classes == max_classes) {
            max_classes = max_classes + (max_classes >> 1) + 1;
            vertex_classes = (VertexClass **)realloc(vertex_classes, max_classes * sizeof(VertexClass *));
            if (!vertex_classes) exit(EXIT_FAILURE);
        }

        int class_id = ++num_classes;
        vertex_classes[class_id - 1] = create_vertex_class();

        class_map[v] = class_id;
        add_to_vertex_class(vertex_classes[class_id - 1], v);
        
        queue->front = queue->rear = 0; // Reset queue
        enqueue(queue, v);

        // BFS to find orbit
        while (!is_queue_empty(queue)) {
            int current_v = dequeue(queue);
            for (int gen = 0; gen < num_generators; gen++) {
                int v_perm = generators[gen * n + current_v];
                if (class_map[v_perm] == 0) {
                    class_map[v_perm] = class_id;
                    add_to_vertex_class(vertex_classes[class_id - 1], v_perm);
                    enqueue(queue, v_perm);
                }
            }
        }
    }

    // Print to file
    fprintf(outfile, "Vertex Classes\n");
    fprintf(outfile, "%d\n", num_classes);
    for (int i = 0; i < num_classes; i++) {
        fprintf(outfile, "[");
        for (int j = 0; j < vertex_classes[i]->size; j++) {
            fprintf(outfile, "%d", vertex_classes[i]->vertices[j]);
            if (j < vertex_classes[i]->size - 1) fprintf(outfile, ", ");
        }
        fprintf(outfile, "]\n");
    }

    for (int i = 0; i < num_classes; i++) free_vertex_class(vertex_classes[i]);
    free(vertex_classes);
    free(class_map);
    free_queue(queue);
}

void compute_edge_equivalence_classes(graph_data_t *data) {
    int n = data->n;
    int num_generators = data->num_generators;
    permutation *generators = data->generators;
    sparsegraph *sg = &data->sg;

    int num_classes = 0;
    int max_classes = 16;
    
    // Count total unique edges (u < v)
    int total_edges = 0;
    for (int u = 0; u < n; u++) {
        for (int j = 0; j < sg->d[u]; j++) {
            int v = sg->e[sg->v[u] + j];
            if (u < v) total_edges++;
        }
    }

    EdgeEntry *edge_entries = (EdgeEntry *)malloc(total_edges * sizeof(EdgeEntry));
    if (!edge_entries) exit(EXIT_FAILURE);

    int entry_count = 0;
    for (int u = 0; u < n; u++) {
        for (int j = 0; j < sg->d[u]; j++) {
            int v = sg->e[sg->v[u] + j];
            if (u < v) {
                edge_entries[entry_count].u = u;
                edge_entries[entry_count].v = v;
                edge_entries[entry_count].class_id = 0;
                entry_count++;
            }
        }
    }

    // Sort for fast lookup
    qsort(edge_entries, entry_count, sizeof(EdgeEntry), compare_edges);

    EdgeClass **edge_classes = (EdgeClass **)malloc(max_classes * sizeof(EdgeClass *));
    if (!edge_classes) exit(EXIT_FAILURE);

    int queue_size = (total_edges > 10000) ? 1000 : total_edges * 2;
    Queue *queue = create_queue(queue_size);

    for (int i = 0; i < entry_count; i++) {
        if (edge_entries[i].class_id != 0) continue;
        
        int u = edge_entries[i].u;
        int v = edge_entries[i].v;
        
        if (num_classes == max_classes) {
            max_classes = max_classes + (max_classes >> 1) + 1;
            edge_classes = (EdgeClass **)realloc(edge_classes, max_classes * sizeof(EdgeClass *));
            if (!edge_classes) exit(EXIT_FAILURE);
        }
        
        int class_id = ++num_classes;
        edge_classes[class_id - 1] = create_edge_class();
        
        edge_entries[i].class_id = class_id;
        add_to_edge_class(edge_classes[class_id - 1], u, v);
        
        queue->front = queue->rear = 0;
        enqueue(queue, u);
        enqueue(queue, v);
        
        // BFS for edges
        while (!is_queue_empty(queue)) {
            int edge_u = dequeue(queue);
            int edge_v = dequeue(queue);
            
            for (int gen = 0; gen < num_generators; gen++) {
                int u_perm = generators[gen * n + edge_u];
                int v_perm = generators[gen * n + edge_v];
                
                // Verify edge existence
                bool edge_exists = false;
                for (int k = 0; k < sg->d[u_perm]; k++) {
                    if (sg->e[sg->v[u_perm] + k] == v_perm) {
                        edge_exists = true;
                        break;
                    }
                }
                
                if (edge_exists) {
                    EdgeEntry *found = find_edge_entry(edge_entries, entry_count, u_perm, v_perm);
                    if (found && found->class_id == 0) {
                        found->class_id = class_id;
                        add_to_edge_class(edge_classes[class_id - 1], 
                                        (u_perm < v_perm) ? u_perm : v_perm,
                                        (u_perm < v_perm) ? v_perm : u_perm);
                        
                        if (queue->rear + 1 < queue->capacity) {
                            enqueue(queue, u_perm);
                            enqueue(queue, v_perm);
                        }
                    }
                }
            }
        }
    }

    // Print to file
    fprintf(outfile, "Edge Classes\n");
    fprintf(outfile, "%d\n", num_classes);
    for (int i = 0; i < num_classes; i++) {
        fprintf(outfile, "[");
        for (int j = 0; j < edge_classes[i]->size; j++) {
            fprintf(outfile, "(%d, %d)", 
                   edge_classes[i]->edges[j * 2], 
                   edge_classes[i]->edges[j * 2 + 1]);
            if (j < edge_classes[i]->size - 1) fprintf(outfile, ", ");
        }
        fprintf(outfile, "]\n");
    }

    for (int i = 0; i < num_classes; i++) free_edge_class(edge_classes[i]);
    free(edge_classes);
    free(edge_entries);
    free_queue(queue);
}

/* =============================================================
   MAIN
   ============================================================= */

int main(int argc, char *argv[]) {
    if (argc < 2 || argc > 4) {
        fprintf(stderr, "Usage: %s <graph_file> [colour_file] [output_file]\n",
                argv[0]);
        fprintf(stderr, "  graph_file : \"n m\" header, then m lines of \"u v\", 0-indexed.\n");
        fprintf(stderr, "  colour_file: one integer per line, the colour of vertex i.\n");
        fprintf(stderr, "               Pass \"\" to skip it. Without a colouring the\n");
        fprintf(stderr, "               behaviour is that of an uncoloured run.\n");
        fprintf(stderr, "  output_file: omit it, or pass \"-\", to write to stdout.\n");
        exit(EXIT_FAILURE);
    }
    const char *colourfile = (argc >= 3 && argv[2][0]) ? argv[2] : NULL;
    const char *outpath    = (argc >= 4 && argv[3][0]) ? argv[3] : NULL;

    // --- SETUP OUTPUT STREAM ---
    // Standard filter behaviour. No output file, or "-", means stdout, so the
    // tool composes with pipes and never writes to a path the caller did not ask
    // for. Diagnostics always go to stderr.
    if (outpath == NULL || strcmp(outpath, "-") == 0) {
        outfile = stdout;
    } else {
        outfile = fopen(outpath, "w");
        if (outfile == NULL) {
            perror("Error opening output file");
            fprintf(stderr, "Attempted path: %s\n", outpath);
            exit(EXIT_FAILURE);
        }
    }
    // ---------------------------

    FILE *file = fopen(argv[1], "r");
    if (!file) {
        perror("Error opening input graph file");
        if (outfile != stdout) fclose(outfile);
        exit(EXIT_FAILURE);
    }

    DYNALLSTAT(int, lab, labSz);
    DYNALLSTAT(int, ptn, ptnSz);
    DYNALLSTAT(int, orbits, orbitsSz);
    static DEFAULTOPTIONS_SPARSEGRAPH(options);
    statsblk stats;
    sparsegraph sg;

    int n, m;

    // IMPORTANT: Set this to FALSE so Nauty does not print generators to stdout automatically.
    // We will capture them in the callback and print them manually to the file.
    options.writeautoms = FALSE; 
    options.userautomproc = my_userautomproc;

    SG_INIT(sg);

    char line[256];
    if (!fgets(line, sizeof(line), file)) {
        fprintf(stderr, "Error reading first line.\n");
        if (outfile != stdout) fclose(outfile);
        exit(EXIT_FAILURE);
    }

    // Handle both "Nodes: N Edges: M" and "N M" formats
    if (sscanf(line, "Nodes: %d Edges: %d", &n, &m) == 2) {
    } else if (sscanf(line, "%d %d", &n, &m) == 2) {
    } else {
        fprintf(stderr, "Error parsing first line\n");
        if (outfile != stdout) fclose(outfile);
        exit(EXIT_FAILURE);
    }

    int word_size = SETWORDSNEEDED(n);
    nauty_check(WORDSIZE, word_size, n, NAUTYVERSIONID);

    DYNALLOC1(int, lab, labSz, n, "malloc");
    DYNALLOC1(int, ptn, ptnSz, n, "malloc");
    DYNALLOC1(int, orbits, orbitsSz, n, "malloc");
    SG_ALLOC(sg, n, 2 * m, "malloc");

    sg.nv = n;
    sg.nde = 2 * m;

    // First Pass: Degree Count
    for (int i = 0; i < n; i++) sg.d[i] = 0;
    int u, v;
    for (int i = 0; i < m; i++) {
        if (fscanf(file, "%d %d", &u, &v) != 2) exit(EXIT_FAILURE);
        sg.d[u]++;
        sg.d[v]++;
    }

    // Build Index
    sg.v[0] = 0;
    for (int i = 1; i < n; i++) sg.v[i] = sg.v[i - 1] + sg.d[i - 1];
    for (int i = 0; i < n; i++) sg.d[i] = 0;

    // Second Pass: Add Edges
    rewind(file);
    if (!fgets(line, sizeof(line), file)) {   // skip header
        fprintf(stderr, "Error re-reading header of %s\n", argv[1]);
        exit(EXIT_FAILURE);
    }
    for (int i = 0; i < m; i++) {
        if (fscanf(file, "%d %d", &u, &v) != 2) exit(EXIT_FAILURE);
        if (u >= n || v >= n) continue;
        sg.e[sg.v[u] + sg.d[u]++] = v;
        sg.e[sg.v[v] + sg.d[v]++] = u;
    }
    fclose(file);

    // Init Global Data
    global_graph_data.n = n;
    global_graph_data.num_generators = 0;
    global_graph_data.max_generators = (n < 100) ? n : 100;
    global_graph_data.generators = (permutation *)malloc(global_graph_data.max_generators * n * sizeof(permutation));
    global_graph_data.sg = sg;

    // --- 0. Optional vertex colouring -------------------------------------
    // lab lists the vertices grouped by colour; ptn marks the end of each group
    // with 0. With defaultptn FALSE, nauty may only map a vertex onto another of
    // the same colour. This is what makes a twin-contracted quotient sound: a
    // class of 3 can never be mapped onto a class of 5.
    if (colourfile) {
        int *col = (int *)malloc(n * sizeof(int));
        FILE *cf = fopen(colourfile, "r");
        if (!cf) { perror("Error opening colour file"); exit(EXIT_FAILURE); }
        for (int i = 0; i < n; i++) {
            if (fscanf(cf, "%d", &col[i]) != 1) {
                fprintf(stderr, "Colour file has fewer than %d entries\n", n);
                exit(EXIT_FAILURE);
            }
        }
        fclose(cf);
        // counting sort: one pass to count, one to place. O(n + colours),
        // not O(colours * n).
        int maxc = 0;
        for (int i = 0; i < n; i++) if (col[i] > maxc) maxc = col[i];
        int *cnt = (int *)calloc(maxc + 2, sizeof(int));
        for (int i = 0; i < n; i++) cnt[col[i] + 1]++;
        for (int c = 0; c <= maxc; c++) cnt[c + 1] += cnt[c];
        int *start = (int *)malloc((maxc + 1) * sizeof(int));
        for (int c = 0; c <= maxc; c++) start[c] = cnt[c];
        for (int i = 0; i < n; i++) lab[cnt[col[i]]++] = i;
        for (int i = 0; i < n; i++) ptn[i] = 1;
        for (int c = 0; c <= maxc; c++)
            if (cnt[c] > start[c]) ptn[cnt[c] - 1] = 0;   // end of each colour block
        free(cnt); free(start);
        options.defaultptn = FALSE;
        fprintf(stderr, "Colouring: %d colour classes over %d vertices\n", maxc + 1, n);
        free(col);
    }

    // --- 1. Compute Automorphism Group ---
    sparsenauty(&sg, lab, ptn, orbits, &options, &stats, NULL);

    // --- 2. Print Group Info and Generators to File ---
    fprintf(outfile, "Automorphism Group\n");
    fprintf(outfile, "%d\n", global_graph_data.num_generators);
    fprintf(outfile, "Order: ");
    writegroupsize(outfile, stats.grpsize1, stats.grpsize2);
    fprintf(outfile, "\n");
    
    // Print stored generators in Cycle Notation (default)
    for(int i = 0; i < global_graph_data.num_generators; i++) {
        writeperm(outfile, &global_graph_data.generators[i * n], options.cartesian, options.linelength, n);
    }

    // --- 3. Compute and Print Vertex Classes to File ---
    compute_vertex_equivalence_classes(&global_graph_data);

    // --- 4. Compute and Print Edge Classes to File ---
    compute_edge_equivalence_classes(&global_graph_data);

    // Cleanup
    free(global_graph_data.generators);
    SG_FREE(sg);
    DYNFREE(lab, labSz);
    DYNFREE(ptn, ptnSz);
    DYNFREE(orbits, orbitsSz);
    
    // Close the output stream, unless it is stdout
    if (outfile != stdout) fclose(outfile);
    else fflush(outfile);

    return 0;
}
