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

#define SHM_KEY "/atom_queue"

typedef struct
{
    sem_t *sem;
    int *shm_count;
    int size;

    const char *shm_key;
    const char *sem_key;
} barrier_t;

#define BARRIER_ATOM_KEY "/ios-atom-barrier"

#define MUTEX_LOG_KEY "/ios-log-mutex"
#define MUTEX_ATOM_KEY "/ios-atom-mutex"

#define QUEUE_HYDRO_KEY "/ios-hydro-queue"
#define QUEUE_OXY_KEY "/ios-oxy-queue"

#define SHM_A_KEY "/ios-log-a"
#define SHM_OXY_KEY "/ios-oxy"
#define SHM_HYDRO_KEY "/ios-hydro"
#define SHM_MOL_KEY "/ios-mol"

#define SHM_ATOM_BARRIER_KEY "/ios-atom-barrier"

#define SHM_INT_SIZE sizeof(int)

#define RAND_MICROS(limit) (rand() % (limit + 1)) * 1000

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

void init_sem(const char *key, unsigned int value)
{
    sem_t *sem = sem_open(key, O_CREAT, 0666, value);
    sem_close(sem);
}

bool unlink_shm(int *a)
{
    if (munmap(a, SHM_INT_SIZE) == -1)
    {
        perror("munmap");
        return false;
    }
    return true;
}

bool init_shm_int(const char *key, int initial_value)
{
    int id = shm_open(key, O_CREAT | O_RDWR, S_IWUSR | S_IRUSR);

    if (id == -1)
    {
        perror("shm_open");
        return false;
    }

    if (ftruncate(id, SHM_INT_SIZE) == -1)
    {
        perror("ftruncate");
        return false;
    }

    int *a = (int *)mmap(NULL, SHM_INT_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, id, 0);

    if (a == MAP_FAILED)
    {
        perror("mmap");
        return false;
    }

    if (close(id) == -1)
    {
        perror("close");
        return false;
    }

    *a = initial_value;

    return unlink_shm(a);
}

int *link_shm_int(const char *key)
{
    int id = shm_open(key, O_RDWR, S_IWUSR | S_IRUSR);

    if (id == -1)
    {
        perror("shm_open");
        return NULL;
    }

    int *a = (int *)mmap(NULL, SHM_INT_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, id, 0);

    if (a == MAP_FAILED)
    {
        perror("munmap");
        return NULL;
    }

    close(id);

    return a;
}

// Open a barrier
bool open_barrier(barrier_t *barrier, const char *shm_key, const char *sem_key, int size)
{
    // Load shm
    barrier->shm_count = link_shm_int(shm_key);
    barrier->size = size;

    // Setup semaphor
    barrier->sem = sem_open(sem_key, O_RDWR);

    return true; // TODO: Handle errors
}

// Initialize a barrier
bool init_barrier(const char *shm_key, const char *sem_key)
{
    init_shm_int(shm_key, 0);
    init_sem(sem_key, 0);

    return true; // TODO: Handle errors
}

// Join processes from barrier
void join_barrier(barrier_t *barrier)
{
    *(barrier->shm_count) = *(barrier->shm_count) + 1;

    if (*(barrier->shm_count) == barrier->size)
    {
        for (int i = 0; i < barrier->size - 1; i++)
        {
            sem_post(barrier->sem);
        }
        *(barrier->shm_count) = 0;
    }
    else
    {
        sem_wait(barrier->sem);
    }
}

void close_barrier(barrier_t *barrier)
{
    unlink_shm(barrier->shm_count);
    sem_close(barrier->sem);
}

// Unlink the barrier shared memory and semaphor
bool unlink_barrier(const char *shm_key, const char *sem_key)
{
    shm_unlink(shm_key);
    sem_unlink(sem_key);

    return true; // TODO: Errors
}

void write_log(const char *entry, ...)
{
    sem_t *mutex = sem_open(MUTEX_LOG_KEY, O_RDWR);

    va_list v_list;
    va_start(v_list, entry);

    sem_wait(mutex);

    FILE *log = fopen("proj2.out", "a");

    int *a = link_shm_int(SHM_A_KEY);

    fprintf(log, "%u: ", *a);
    *a = *a + 1;

    unlink_shm(a);

    vfprintf(log, entry, v_list);

    fclose(log);

    sem_post(mutex);

    sem_close(mutex);
}

void oxy_process(long intro_wait, long mol_wait, long id)
{
    srand(getpid() * time(NULL));
    write_log("O %ld: started\n", id);

    usleep(RAND_MICROS(intro_wait));

    write_log("O %ld: going to queue\n", id);

    sem_t *mutex = sem_open(MUTEX_ATOM_KEY, O_RDWR);
    sem_t *hydro_queue = sem_open(QUEUE_HYDRO_KEY, O_RDWR);
    sem_t *oxy_queue = sem_open(QUEUE_OXY_KEY, O_RDWR);

    barrier_t barrier;
    open_barrier(&barrier, SHM_ATOM_BARRIER_KEY, BARRIER_ATOM_KEY, 3); // TODO: Check errors

    sem_wait(mutex);

    // Increase oxygen count

    int *oxy_count = link_shm_int(SHM_OXY_KEY);
    int *hydro_count = link_shm_int(SHM_HYDRO_KEY);
    int *mol_count = link_shm_int(SHM_MOL_KEY);

    *oxy_count = *oxy_count + 1;

    if (*hydro_count >= 2)
    {
        *mol_count = *mol_count + 1;

        sem_post(hydro_queue);
        sem_post(hydro_queue);
        *hydro_count = *hydro_count - 2;
        sem_post(oxy_queue);
        *oxy_count = *oxy_count - 1;
    }
    else
    {
        sem_post(mutex);
    }

    unlink_shm(oxy_count);
    unlink_shm(hydro_count);

    sem_wait(oxy_queue);

    // Make a bond

    write_log("O %ld: creating molecule %d\n", id, *mol_count);

    usleep(RAND_MICROS(mol_wait));

    join_barrier(&barrier);

    write_log("O %ld: molecule %d created\n", id, *mol_count);

    unlink_shm(mol_count);

    sem_post(mutex);
    
    close_barrier(&barrier);

    sem_close(mutex);
    sem_close(hydro_queue);
    sem_close(oxy_queue);
}

void hydro_process(long intro_wait, long id)
{
    srand(getpid() * time(NULL));
    write_log("H %ld: started\n", id);

    usleep(RAND_MICROS(intro_wait));

    write_log("H %ld: going to queue\n", id);

    sem_t *mutex = sem_open(MUTEX_ATOM_KEY, O_RDWR);
    sem_t *hydro_queue = sem_open(QUEUE_HYDRO_KEY, O_RDWR);
    sem_t *oxy_queue = sem_open(QUEUE_OXY_KEY, O_RDWR);

    barrier_t barrier;
    open_barrier(&barrier, SHM_ATOM_BARRIER_KEY, BARRIER_ATOM_KEY, 3);

    sem_wait(mutex);

    // Increase hydrogen count

    int *oxy_count = link_shm_int(SHM_OXY_KEY);
    int *hydro_count = link_shm_int(SHM_HYDRO_KEY);
    int *mol_count = link_shm_int(SHM_MOL_KEY);

    *hydro_count = *hydro_count + 1;

    if (*hydro_count >= 2 && *oxy_count >= 1)
    {
        *mol_count = *mol_count + 1;

        sem_post(hydro_queue);
        sem_post(hydro_queue);
        *hydro_count = *hydro_count - 2;
        sem_post(oxy_queue);
        *oxy_count = *oxy_count - 1;
    }
    else
    {
        sem_post(mutex);
    }

    unlink_shm(hydro_count);
    unlink_shm(oxy_count);

    sem_wait(hydro_queue);

    // Make a bond
    write_log("H %ld: creating molecule %d\n", id, *mol_count);

    join_barrier(&barrier); // When all 3 atoms are ready to create the molecule this passes through.

    write_log("H %ld: molecule %d created\n", id, *mol_count);

    unlink_shm(mol_count);

    sem_post(mutex);

    close_barrier(&barrier);

    sem_close(mutex);
    sem_close(hydro_queue);
    sem_close(oxy_queue);
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

    fclose(fopen("proj2.out", "w"));

    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

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

    // Create oxygen processes

    pid_t pids[oxy_count + hydro_count];
    long pids_len = 0;

    for (long i = 1; i <= oxy_count; i++)
    {
        pid_t oxy_id = fork();

        if (oxy_id < 0)
        {
            perror("fork");
            return 2;
        }

        if (oxy_id == 0)
        {
            oxy_process(wait_intro, wait_creat, i);
            _exit(0);
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
            perror("fork");
            return 2;
        }

        if (hydro_id == 0)
        {
            hydro_process(wait_intro, i);
            _exit(0);
        }
        else
        {
            // Parent
            pids[pids_len++] = hydro_id;
        }
    }

    for (long i = 0; i < pids_len; i++)
    {
        waitpid(pids[i], NULL, 0);
    }

    int *f_oxy_count = link_shm_int(SHM_OXY_KEY);
    int *f_hydro_count = link_shm_int(SHM_HYDRO_KEY);

    unlink_shm(f_oxy_count);
    unlink_shm(f_hydro_count);

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
    return 0;
}
