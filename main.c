#include <sys/stat.h>
#include <string.h>
#include <sys/param.h>

#ifdef HAVE_POLL_H
#include <poll.h>
#endif

#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/times.h>
#include <libnfs.h>
#include <getopt.h>
#include <math.h>

#ifdef HAVE_MPI
#  include <mpi.h>
#endif // HAVE_MPI

#define DEFAULT_FILES 100

#define DEFAULT_WARMUP_LOOPS 0

#define OUT_FORMAT "%16s rate: total: %8.2f\t%8.2f rps \u00B1%8.2f, min: %8.2f, max: %8.2f, count: %8d\n"

void usage(void) {
    printf("Usage: nfs-bench [-f <num>] [-u] [-w <num>] url\n");
    printf("\n");
    printf("  Options:\n");
    printf("    -f <num>  Number of files to create and remove (default: %d)\n", DEFAULT_FILES);
    printf("    -u unique directory per tasks\n");
    printf("    -w <num> number of warmup iterations (default: %d)\n", DEFAULT_WARMUP_LOOPS);
    printf("\n");
    printf("\n");
    printf("Example:\n");
    printf("   nfs-bench -u -f 100 nfs://my-nfs-server/test/path\n");
    exit(1);
}

/**
 * requests statistics record
 */
typedef struct stats {
    double sum;
    double avg;
    double min;
    double max;
    double err;
    int count;
} stats_t;


void calculate_stats(double *array, int num_elements, stats_t *stats) {

    stats->sum = 0.;
    stats->avg = 0.;
    stats->min = 0.;
    stats->max = 0.;
    stats->err = 0.;
    stats->count = num_elements;

    double dsum = 0.;
    int i;
    for (i = 0; i < num_elements; i++) {

        if (array[i] > stats->max) {
            stats->max = array[i];
        }

        if (array[i] < stats->min || stats->min == 0.) {
            stats->min = array[i];
        }

        stats->sum += array[i];
    }

    stats->avg = stats->sum / num_elements;
    for (i = 0; i < num_elements; i++) {
        double diff = array[i] - stats->avg;
        dsum += diff*diff;
    }
    stats->err = sqrt( dsum/num_elements);
}


int createFiles(struct nfs_context* nfs, pid_t pid, int files, const char* hostname, double* rate) {
    struct tms dummy;
    clock_t rtime;
    char filename[FILENAME_MAX];
    struct nfsfh* nfsfh;
    int i;

    rtime = times(&dummy);
    for (i = 0; i < files; i++) {
        sprintf(filename, "%s.file.%d.%d", hostname, pid, i);
        if (nfs_open2(nfs, filename, O_RDWR | O_CREAT, 0660, &nfsfh) != 0) {
            fprintf(stderr, "Failed to creat file %s: %s\n",
                    filename,
                    nfs_get_error(nfs));
            return 1;
        }

        if (nfs_close(nfs, nfsfh)) {
            fprintf(stderr, "Failed to close file %s: %s\n", filename, nfs_get_error(nfs));
            return 1;
        }
    }

    *rate = (double)files / ((double)(times(&dummy) - rtime) / (double)sysconf(_SC_CLK_TCK));
    return 0;
}

int deleteFiles(struct nfs_context* nfs, pid_t pid, int files, const char* hostname, double* rate) {
    struct tms dummy;
    clock_t rtime;
    char filename[FILENAME_MAX];
    int i;

    rtime = times(&dummy);
    for (i = 0; i < files; i++) {
        sprintf(filename, "%s.file.%d.%d", hostname, pid, i);
        if (nfs_unlink(nfs, filename) != 0) {
            fprintf(stderr, "Failed to remove file %s: %s\n",
                    filename,
                    nfs_get_error(nfs));
        }
    }

    *rate = (double)files / ((double)(times(&dummy) - rtime) / (double)sysconf(_SC_CLK_TCK));
    return 0;
}


int statFiles(struct nfs_context* nfs, pid_t pid, int files, const char* hostname, double* rate) {
    struct tms dummy;
    clock_t rtime;
    char filename[FILENAME_MAX];
    struct nfs_stat_64 stat;
    int i;

    rtime = times(&dummy);
    for (i = 0; i < files; i++) {
        sprintf(filename, "%s.file.%d.%d", hostname, pid, i);
        if (nfs_stat64(nfs, filename, &stat) != 0) {
            fprintf(stderr, "Failed to stat file %s: %s\n",
                    filename,
                    nfs_get_error(nfs));
            return 1;
        }
    }

    *rate = (double)files / ((double)(times(&dummy) - rtime) / (double)sysconf(_SC_CLK_TCK));
    return 0;
}


/**
 * Init stats with given rate. It initialized as with a single measurement,
 * as MPI call will update it with values from other nodes.
 */
void init_stats(struct stats *stats, double rate) {
    stats->min = stats->max = stats->avg = stats->sum = rate;
    stats->err = 0.;
    stats->count = 1;
}

int main(int argc, char *argv[]) {


    int files = DEFAULT_FILES;

    int rc = 1;
    int res;
    struct nfs_context *nfs;
    struct nfs_url *url;
    int c;
    int pid;
    char filename[FILENAME_MAX];
    char hostname[MAXHOSTNAMELEN];
    double rate;
    stats_t stats;
    int unique_working_dir = 0;
    unsigned int warmup_loops = DEFAULT_WARMUP_LOOPS;

    double *rates = NULL;

    // in case of MPI it will be reassigned
    int size = 1, rank = 0;

    while ((c = getopt(argc, argv, "f:uw:")) != EOF) {
        switch (c) {
            case 'f':
                files = atoi(optarg);
                break;
            case 'u':
                unique_working_dir = 1;
                break;
            case 'w':
                warmup_loops = atoi(optarg);
                break;
            case '?':
                usage();
        }
    }

    if (((argc - optind) != 1)) {
        usage();
    }

#ifdef HAVE_MPI
    res = MPI_Init(&argc, &argv);
    if (res != MPI_SUCCESS) {
        fprintf (stderr, "MPI_Init failed\n");
        exit(1);
    }

    res = MPI_Comm_size(MPI_COMM_WORLD, &size);
    if (res != MPI_SUCCESS) {
        fprintf (stderr, "MPI_Comm_size failed\n");
        exit(1);
    } 

    res = MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    if (res != MPI_SUCCESS) {
        fprintf (stderr, "MPI_Comm_rank failed\n");
        exit(1);
    }
#endif // HAVE_MPI

    if (gethostname(hostname, MAXHOSTNAMELEN) != 0) {
        perror("Failed to get hostname");
        exit(1);
    }

    pid = getpid();

    nfs = nfs_init_context();
    if (nfs == NULL) {
        fprintf(stderr, "failed to init context\n");
        goto out;
    }

    url = nfs_parse_url_full(nfs, argv[optind]);
    if (url == NULL) {
        fprintf(stderr, "%s\n", nfs_get_error(nfs));
        goto out;
    }

    if (nfs_mount(nfs, url->server,
                  url->path) != 0) {
        fprintf(stderr, "Failed to mount nfs share : %s\n",
                nfs_get_error(nfs));
        goto out;
    }

#ifdef HAVE_MPI
    MPI_Barrier(MPI_COMM_WORLD);

#endif // HAVE_MPI


    if (unique_working_dir) {
        // create unique working directory
        sprintf(filename, "%s/%d", url->file, rank);
        int rc = nfs_mkdir2(nfs, filename, 0755);
        if (rc != 0 && rc != -17  /* NFS_ERR_EEXIST */) {
            fprintf(stderr, "Failed to create directory %s: %s\n",
                    filename,
                    nfs_get_error(nfs));
            goto out;
        }
        if (nfs_chdir(nfs, filename) != 0) {
            fprintf(stderr, "Failed to change directory to %s: %s\n",
                    filename,
                    nfs_get_error(nfs));
            goto out;
        }
    } else {
        if (nfs_chdir(nfs, url->file) != 0) {
            fprintf(stderr, "Failed to change directory to %s: %s\n",
                    filename,
                    nfs_get_error(nfs));
            goto out;
        }
    }


    if (rank == 0) {
        if (warmup_loops) {
            fprintf(stdout, "Warmup. %d iterations per process\n", warmup_loops);
            rc = createFiles(nfs, pid, warmup_loops, hostname, &rate);
            if (rc) {
                goto out;
            }
#ifdef HAVE_MPI
            MPI_Barrier(MPI_COMM_WORLD);
#endif // HAVE_MPI
            rc = statFiles(nfs, pid, warmup_loops, hostname, &rate);
            if (rc) {
                goto out;
            }
#ifdef HAVE_MPI
            MPI_Barrier(MPI_COMM_WORLD);
#endif // HAVE_MPI
            rc = deleteFiles(nfs, pid, warmup_loops, hostname, &rate);
            if (rc) {
                goto out;
            }
#ifdef HAVE_MPI
            MPI_Barrier(MPI_COMM_WORLD);
#endif // HAVE_MPI
        }
        fprintf(stdout, "Running %d iterations per process, totally %d processes.\n\n", files, size);
        rates = (double *) malloc(sizeof(double) * size);
    }

    rc = createFiles(nfs, pid, files, hostname, &rate);
    if (rc) {
        goto out;
    }

    init_stats(&stats, rate);

#ifdef HAVE_MPI
    MPI_Gather(&rate, 1, MPI_DOUBLE, rates, 1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
    if (rank == 0) {
        calculate_stats(rates, size, &stats);
    }
#endif // HAVE_MPI

    if (rank == 0) {
        fprintf(stdout, OUT_FORMAT, "Create",
                stats.sum, stats.avg, stats.err, stats.min, stats.max, stats.count);
    }

#ifdef HAVE_MPI
    MPI_Barrier(MPI_COMM_WORLD);
#endif // HAVE_MPI

    rc = statFiles(nfs, pid, files, hostname, &rate);
    if (rc) {
        goto out;
    }

    init_stats(&stats, rate);

#ifdef HAVE_MPI
    MPI_Gather(&rate, 1, MPI_DOUBLE, rates, 1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
    if (rank == 0) {
        calculate_stats(rates, size, &stats);
    }
#endif // HAVE_MPI

    if (rank == 0) {
        fprintf(stdout, OUT_FORMAT, "Stat",
                stats.sum, stats.avg, stats.err, stats.min, stats.max, stats.count);
    }

#ifdef HAVE_MPI
    MPI_Barrier(MPI_COMM_WORLD);
#endif // HAVE_MPI

    rc = deleteFiles(nfs, pid, files, hostname, &rate);
    if (rc) {
        goto out;
    }

    init_stats(&stats, rate);

#ifdef HAVE_MPI
    MPI_Gather(&rate, 1, MPI_DOUBLE, rates, 1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
    if (rank == 0) {
        calculate_stats(rates, size, &stats);
    }
#endif // HAVE_MPI

    if (rank == 0) {
        fprintf(stdout, OUT_FORMAT, "Remove",
                stats.sum, stats.avg, stats.err, stats.min, stats.max, stats.count);
    }
    rc = 0;
    out:

#ifdef HAVE_MPI
    MPI_Barrier(MPI_COMM_WORLD);
    res = MPI_Finalize();
    if (res != MPI_SUCCESS) {
        fprintf (stderr, "MPI_Finalize failed\n");
    }
#endif // HAVE_MPI

    if (nfs != NULL) {
        nfs_destroy_context(nfs);
    }

    if (url != NULL) {
        nfs_destroy_url(url);
    }
    return rc;
}
