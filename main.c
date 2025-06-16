#include <sys/stat.h>
#include <string.h>

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

void usage(void) {
    printf("Usage: nfs-bench [-f <num>] url\n");
    printf("\n");
    printf("Example:\n");
    printf("   nfs-bench -f 100 nfs://my-nfs-server/test/path\n");
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

int main(int argc, char *argv[]) {


    int files = DEFAULT_FILES;

    int rc = 1;
    struct nfs_context *nfs;
    struct nfs_url *url;
    struct nfsfh *nfsfh;
    int i;
    int c;
    int pid;
    char filename[4096];
    char hostname[1024];
    clock_t rtime;
    struct tms dummy;
    double rate;
    stats_t stats;

    double *rates = NULL;

    int res;
    // in case of MPI it will be reassigned
    int size = 1, rank = 0;

    while ((c = getopt(argc, argv, "f:")) != EOF) {
        switch (c) {
            case 'f':
                files = atoi(optarg);
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

    hostname[1023] = '\0';
    if (gethostname(hostname, 1023) != 0) {
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
    if (rank == 0) {
        fprintf(stdout, "Running %d iterations per process, totally %d processes.\n", files, size);
    }
    rtime = times(&dummy);
    for (i = 0; i < files; i++) {

        sprintf(filename, "%s/%s.file.%d.%d", url->file, hostname, pid, i);
        if (nfs_open2(nfs, filename, O_RDWR | O_CREAT ,0660, &nfsfh) != 0) {
            fprintf(stderr, "Failed to creat file %s: %s\n",
                    filename,
                    nfs_get_error(nfs));
            goto out;
        }
    }

    rate = (double)files / ((double) (times(&dummy) - rtime) / (double) sysconf(_SC_CLK_TCK));
    if (rank == 0) {
        rates = (double *) malloc(sizeof(double) * size);
    }

    stats.min = stats.max = stats.avg = stats.sum = rate;
    stats.err = 0.;
    stats.count = 1;

#ifdef HAVE_MPI
    MPI_Gather(&rate, 1, MPI_DOUBLE, rates, 1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
    if (rank == 0) {
        calculate_stats(rates, size, &stats);
    }
#endif // HAVE_MPI

    if (rank == 0) {
        fprintf(stdout, "Create rate: total: %2.2f, %2.2f rps \u00B1%2.2f, min: %2.2f, max: %2.2f, count: %d\n",
                stats.sum, stats.avg, stats.err, stats.min, stats.max, stats.count);
    }

#ifdef HAVE_MPI
    MPI_Barrier(MPI_COMM_WORLD);
#endif // HAVE_MPI

    rtime = times(&dummy);
    // cleanup ignoring errors
    for (i = 0; i < files; i++) {
        sprintf(filename, "%s/%s.file.%d.%d", url->file, hostname, pid, i);
        if (nfs_unlink(nfs, filename) != 0) {
            fprintf(stderr, "Failed to remove file %s: %s\n",
                    filename,
                    nfs_get_error(nfs));
        }
    }

    rate = (double)files / ((double) (times(&dummy) - rtime) / (double) sysconf(_SC_CLK_TCK));
    if (rank == 0) {
        rates = (double *) malloc(sizeof(double) * size);
    }

    stats.min = stats.max = stats.avg = stats.sum = rate;
    stats.err = 0.;
    stats.count = 1;

#ifdef HAVE_MPI
    MPI_Gather(&rate, 1, MPI_DOUBLE, rates, 1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
    if (rank == 0) {
        calculate_stats(rates, size, &stats);
    }
#endif // HAVE_MPI

    if (rank == 0) {
        fprintf(stdout, "Remove rate: total: %2.2f, %2.2f rps \u00B1%2.2f, min: %2.2f, max: %2.2f, count: %d\n",
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
