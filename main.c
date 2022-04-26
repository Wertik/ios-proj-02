#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <semaphore.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdbool.h>
#include <time.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>

typedef struct
{
    sem_t *sem;
    int *count;
    int size;

    const char *shm_key;
    const char *sem_key;
} barrier_t;

#define BARRIER_ATOM_KEY "/ios-atom-barrier"

#define MUTEX_LOG_KEY "/ios-log-mutex"
#define MUTEX_ATOM_KEY "/ios-atom-mutex"
#define MUTEX_QUEUE_KEY "/ios-atom-queue-mutex"
#define MUTEX_MOL_KEY "/ios-mol-queue-mutex"

#define QUEUE_HYDRO_KEY "/ios-hydro-queue"
#define QUEUE_OXY_KEY "/ios-oxy-queue"

#define SHM_A_KEY "/ios-log-a"
#define SHM_OXY_KEY "/ios-oxy"
#define SHM_HYDRO_KEY "/ios-hydro"
#define SHM_MOL_KEY "/ios-mol"

#define SHM_ATOM_BARRIER_KEY "/ios-atom-barrier"

#define SHM_INT_SIZE sizeof(int)

#define RAND_MICROS(limit) (rand() % (limit + 1)) * 1000

// Parse a long from a string argument.
// Has to be a positive whole number.
bool parse_long(char *str, long *res)
{
    char *ptr;
    long value = strtol(str, &ptr, 10);

    if (ptr[0] != '\0')
    {
        fprintf(stderr, "Not a number.\n");
        return false;
    }

    if (value < 0)
    {
        fprintf(stderr, "Cannot be negative.\n");
        return false;
    }

    *res = value;
    return true;
}

// Semaphor operations guarded by error checks.

void init_sem(const char *key, unsigned int value)
{
    sem_t *sem = sem_open(key, O_CREAT, 0666, value);

    if (sem == SEM_FAILED)
    {
        perror("init_sem (sem_open)");
        _exit(EXIT_FAILURE);
    }

    if (sem_close(sem) == -1)
    {
        perror("init_sem (sem_close)");
        _exit(EXIT_FAILURE);
    }
}

sem_t *open_sem(const char *key)
{
    sem_t *sem = sem_open(key, O_RDWR);

    if (sem == SEM_FAILED)
    {
        perror("open_sem");
        _exit(EXIT_FAILURE);
    }

    return sem;
}

void wait_sem(sem_t *sem)
{
    if (sem_wait(sem) == -1)
    {
        perror("wait_sem");
        _exit(EXIT_FAILURE);
    }
}

void post_sem(sem_t *sem)
{
    if (sem_post(sem) == -1)
    {
        perror("post_sem");
        _exit(EXIT_FAILURE);
    }
}

void close_sem(sem_t *sem)
{
    if (sem_close(sem) == -1)
    {
        perror("close_sem");
        _exit(EXIT_FAILURE);
    }
}

// Shared memory operations.

void unlink_shm(int *a)
{
    if (munmap(a, SHM_INT_SIZE) == -1)
    {
        perror("unlink_shm (munmap)");
        _exit(EXIT_FAILURE);
    }
}

void init_shm_int(const char *key, int initial_value)
{
    int id = shm_open(key, O_CREAT | O_RDWR, S_IWUSR | S_IRUSR);

    if (id == -1)
    {
        perror("init_shm_int (shm_open)");
        _exit(EXIT_FAILURE);
    }

    if (ftruncate(id, SHM_INT_SIZE) == -1)
    {
        perror("init_shm_int (ftruncate)");
        _exit(EXIT_FAILURE);
    }

    int *a = (int *)mmap(NULL, SHM_INT_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, id, 0);

    if (a == MAP_FAILED)
    {
        perror("init_shm_int (mmap)");
        _exit(EXIT_FAILURE);
    }

    if (close(id) == -1)
    {
        perror("init_shm_int (close)");
        _exit(EXIT_FAILURE);
    }

    *a = initial_value;

    unlink_shm(a);
}

int *link_shm_int(const char *key)
{
    int id = shm_open(key, O_RDWR, S_IWUSR | S_IRUSR);

    if (id == -1)
    {
        perror("link_shm_int (shm_open)");
        _exit(EXIT_FAILURE);
    }

    int *a = (int *)mmap(NULL, SHM_INT_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, id, 0);

    if (a == MAP_FAILED)
    {
        perror("link_shm_int (munmap)");
        _exit(EXIT_FAILURE);
    }

    if (close(id) == -1)
    {
        perror("link_shm_int (close)");
        _exit(EXIT_FAILURE);
    }

    return a;
}

// Barrier operations
// Handle errors seperately, allows easier debugging(?).

// Initialize a barrier
void init_barrier(const char *shm_key, const char *sem_key)
{
    init_shm_int(shm_key, 0);
    init_sem(sem_key, 0);
}

// Open a barrier
void open_barrier(barrier_t *barrier, const char *shm_key, const char *sem_key, int size)
{
    // Load shm
    barrier->count = link_shm_int(shm_key);
    barrier->size = size;

    // Setup semaphor
    barrier->sem = sem_open(sem_key, O_RDWR);

    if (barrier->sem == NULL)
    {
        perror("open_barrier (sem_open)");
        _exit(EXIT_FAILURE);
    }
}

// Join processes from barrier
void join_barrier(barrier_t *barrier)
{
    *(barrier->count) = *(barrier->count) + 1;

    if (*(barrier->count) == barrier->size)
    {
        for (int i = 0; i < barrier->size - 1; i++)
        {
            if (sem_post(barrier->sem) == -1)
            {
                perror("join_barrier (sem_post)");
                _exit(EXIT_FAILURE);
            }
        }
        *(barrier->count) = 0;
    }
    else
    {
        if (sem_wait(barrier->sem) == -1)
        {
            perror("join_barrier (sem_wait)");
            _exit(EXIT_FAILURE);
        }
    }
}

void close_barrier(barrier_t *barrier)
{
    if (sem_close(barrier->sem) == -1)
    {
        perror("close_barrier (sem_close)");
        _exit(EXIT_FAILURE);
    }

    unlink_shm(barrier->count);
}

// Unlink the barrier shared memory and semaphor
void unlink_barrier(const char *shm_key, const char *sem_key)
{
    if (shm_unlink(shm_key) == -1)
    {
        // Ignore if it simply doesn't exist.
        if (errno != ENOENT)
        {
            perror("unlink_barrier (shm_unlink)");
            _exit(EXIT_FAILURE);
        }
    }

    if (sem_unlink(sem_key) == -1)
    {
        if (errno != ENOENT)
        {
            perror("unlink_barrier (shm_unlink)");
            _exit(EXIT_FAILURE);
        }
    }
}

// Write an entry into the log file.
void write_log(const char *entry, ...)
{
    sem_t *mutex = open_sem(MUTEX_LOG_KEY);

    va_list v_list;
    va_start(v_list, entry);

    wait_sem(mutex);

// Debug printing into console instead of file.
#ifndef PRINT_CONSOLE
    FILE *log = fopen("proj2.out", "a");

    int *a = link_shm_int(SHM_A_KEY);

    fprintf(log, "%u: ", *a);
    *a = *a + 1;

    unlink_shm(a);

    vfprintf(log, entry, v_list);

    fclose(log);
#else
    vprintf(entry, v_list);
#endif

    post_sem(mutex);

    close_sem(mutex);
}

// Function for Oxygen processes.
void oxy_process(long hydro_count, long intro_wait, long mol_wait, long id)
{
    write_log("O %ld: started\n", id);

    srand(getpid() * time(NULL));
    usleep(RAND_MICROS(intro_wait));

    write_log("O %ld: going to queue\n", id);

    // GOING INTO QUEUE

    sem_t *atom_mutex = open_sem(MUTEX_ATOM_KEY);
    sem_t *hydro_queue = open_sem(QUEUE_HYDRO_KEY);
    sem_t *oxy_queue = open_sem(QUEUE_OXY_KEY);

    barrier_t barrier;
    open_barrier(&barrier, SHM_ATOM_BARRIER_KEY, BARRIER_ATOM_KEY, 3);

    wait_sem(atom_mutex);

    // Increase oxygen count, release atoms for molecule if possible

    int *oxy_q_count = link_shm_int(SHM_OXY_KEY);
    int *hydro_q_count = link_shm_int(SHM_HYDRO_KEY);
    int *mol_count = link_shm_int(SHM_MOL_KEY);

    *oxy_q_count = *oxy_q_count + 1;

    if (*hydro_q_count >= 2)
    {
        // Release atoms

        *mol_count = *mol_count + 1;

        post_sem(hydro_queue);
        post_sem(hydro_queue);
        *hydro_q_count = *hydro_q_count - 2;

        post_sem(oxy_queue);
        *oxy_q_count = *oxy_q_count - 1;
    }
    else
    {
        // Figure whether more bonds are possible

        if (hydro_count - *mol_count * 2 < 2 || *oxy_q_count * 2 + *mol_count * 2 > hydro_count)
        {
            write_log("O %ld: not enough H\n", id);

            unlink_shm(oxy_q_count);
            unlink_shm(hydro_q_count);
            unlink_shm(mol_count);

            post_sem(atom_mutex);

            close_barrier(&barrier);

            close_sem(atom_mutex);
            close_sem(hydro_queue);
            close_sem(oxy_queue);
            return;
        }

        post_sem(atom_mutex);
    }

    unlink_shm(oxy_q_count);
    unlink_shm(hydro_q_count);

    wait_sem(oxy_queue); // WAIT IN QUEUE

    write_log("O %ld: creating molecule %d\n", id, *mol_count);

    usleep(RAND_MICROS(mol_wait));

    join_barrier(&barrier);

    write_log("O %ld: molecule %d created\n", id, *mol_count);

    unlink_shm(mol_count);

    post_sem(atom_mutex);

    // Dispose

    close_barrier(&barrier);

    close_sem(atom_mutex);
    close_sem(hydro_queue);
    close_sem(oxy_queue);
}

// Function for Hydrogen processes.
void hydro_process(long oxy_count, long hydro_count, long intro_wait, long id)
{
    write_log("H %ld: started\n", id);

    srand(getpid() * time(NULL)); // Make atoms get somewhat unique wait times.
    usleep(RAND_MICROS(intro_wait));

    write_log("H %ld: going to queue\n", id);

    // Setup

    sem_t *atom_mutex = open_sem(MUTEX_ATOM_KEY);
    sem_t *hydro_queue = open_sem(QUEUE_HYDRO_KEY);
    sem_t *oxy_queue = open_sem(QUEUE_OXY_KEY);

    barrier_t barrier;
    open_barrier(&barrier, SHM_ATOM_BARRIER_KEY, BARRIER_ATOM_KEY, 3);

    wait_sem(atom_mutex);

    // Increase hydrogen count

    int *oxy_q_count = link_shm_int(SHM_OXY_KEY);
    int *hydro_q_count = link_shm_int(SHM_HYDRO_KEY);
    int *mol_count = link_shm_int(SHM_MOL_KEY);

    *hydro_q_count = *hydro_q_count + 1;

    if (*hydro_q_count >= 2 && *oxy_q_count >= 1)
    {
        *mol_count = *mol_count + 1;

        post_sem(hydro_queue);
        post_sem(hydro_queue);
        *hydro_q_count = *hydro_q_count - 2;

        post_sem(oxy_queue);
        *oxy_q_count = *oxy_q_count - 1;
    }
    else
    {
        // Figure whether more bonds are possible

        // NO MORE OXYGEN IS AVAILABLE
        // QUEUE % 2 = 1 AND NO MORE HYDROGEN IS COMING

        if (oxy_count - *mol_count < 1 || (*hydro_q_count % 2 == 1 && hydro_count - *hydro_q_count - *mol_count * 2 == 0))
        {
            write_log("H %ld: not enough O or H\n", id);

            post_sem(atom_mutex);

            unlink_shm(hydro_q_count);
            unlink_shm(oxy_q_count);
            unlink_shm(mol_count);

            // Dispose

            close_barrier(&barrier);

            close_sem(atom_mutex);
            close_sem(hydro_queue);
            close_sem(oxy_queue);

            return;
        }

        post_sem(atom_mutex);
    }

    unlink_shm(hydro_q_count);
    unlink_shm(oxy_q_count);

    wait_sem(hydro_queue); // QUEUE

    write_log("H %ld: creating molecule %d\n", id, *mol_count);

    join_barrier(&barrier); // When all 3 atoms are ready to create the molecule this passes through.

    write_log("H %ld: molecule %d created\n", id, *mol_count);

    unlink_shm(mol_count);

    // Dispose

    close_barrier(&barrier);

    close_sem(atom_mutex);
    close_sem(hydro_queue);
    close_sem(oxy_queue);
}

void initialize()
{
    // Clear the output file.

    FILE *fd = fopen("proj2.out", "w");
    if (fd == NULL)
    {
        perror("main (fopen)");
        exit(EXIT_FAILURE);
    }

    if (fclose(fd) == EOF)
    {
        perror("main (fclose)");
        exit(EXIT_FAILURE);
    }

    // Initialize all the shared memory and semaphors.

    init_sem(MUTEX_LOG_KEY, 1);
    init_sem(MUTEX_ATOM_KEY, 1);

    init_sem(QUEUE_HYDRO_KEY, 0);
    init_sem(QUEUE_OXY_KEY, 0);

    init_barrier(SHM_ATOM_BARRIER_KEY, BARRIER_ATOM_KEY);

    init_shm_int(SHM_A_KEY, 1);
    init_shm_int(SHM_OXY_KEY, 0);
    init_shm_int(SHM_HYDRO_KEY, 0);
    init_shm_int(SHM_MOL_KEY, 0);
}

void dispose()
{
    // Dispose of shared memory and semaphors.

    sem_unlink(MUTEX_LOG_KEY);
    sem_unlink(MUTEX_ATOM_KEY);
    sem_unlink(QUEUE_HYDRO_KEY);
    sem_unlink(QUEUE_OXY_KEY);

    unlink_barrier(SHM_ATOM_BARRIER_KEY, BARRIER_ATOM_KEY);

    shm_unlink(SHM_A_KEY);
    shm_unlink(SHM_HYDRO_KEY);
    shm_unlink(SHM_OXY_KEY);
    shm_unlink(SHM_MOL_KEY);
}

int main(int argc, char *argv[])
{
    // Process arguments

    if (argc < 4)
    {
        fprintf(stderr, "Invalid number of arguments.\n");
        return EXIT_FAILURE;
    }

    long oxy_count, hydro_count, wait_intro, wait_creat;

    if (!parse_long(argv[1], &oxy_count) ||
        !parse_long(argv[2], &hydro_count) ||
        !parse_long(argv[3], &wait_intro) ||
        !parse_long(argv[4], &wait_creat))
    {
        return EXIT_FAILURE;
    }

    if (wait_intro > 1000)
    {
        fprintf(stderr, "Invalid atom wait time.\n");
        return EXIT_FAILURE;
    }

    if (wait_creat > 1000)
    {
        fprintf(stderr, "Invalid molecule create wait time.\n");
        return EXIT_FAILURE;
    }

    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    dispose();
    initialize();
    atexit(dispose);

    // Create oxygen processes

    pid_t pids[oxy_count + hydro_count];
    long pids_len = 0;

    for (long i = 1; i <= oxy_count; i++)
    {
        pid_t oxy_id = fork();

        if (oxy_id < 0)
        {
            perror("main (fork)");
            return EXIT_FAILURE;
        }

        if (oxy_id == 0)
        {
            oxy_process(hydro_count, wait_intro, wait_creat, i);
            _exit(EXIT_SUCCESS);
        }
        else
        {
            // Parent
            pids[pids_len++] = oxy_id;
        }
    }

    for (long i = 1; i <= hydro_count; i++)
    {
        pid_t hydro_id = fork();

        if (hydro_id < 0)
        {
            perror("main (fork)");
            return EXIT_FAILURE;
        }

        if (hydro_id == 0)
        {
            hydro_process(oxy_count, hydro_count, wait_intro, i);
            _exit(EXIT_SUCCESS);
        }
        else
        {
            // Parent
            pids[pids_len++] = hydro_id;
        }
    }

    // Wait for all the processes to finish.

    for (long i = 0; i < pids_len; i++)
    {
        waitpid(pids[i], NULL, 0);
    }

    return EXIT_SUCCESS;
}
