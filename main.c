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
#include <nfsc/libnfs.h>
#include <getopt.h>

#ifdef HAVE_MPI
#  include <mpi.h>
#endif // HAVE_MPI

#define DEFAULT_FILES 100

void usage(void) {
    exit(1);
}


double avg(double *array, int num_elements) {
    double sum = 0.;
    int i;
    for (i = 0; i < num_elements; i++) {
        sum += array[i];
    }
    return sum / num_elements;
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
    double duration;
    double duration_avg;

    double *durations = NULL;

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
        if (nfs_create(nfs, filename,
                       O_WRONLY | O_CREAT | O_EXCL, 0660,
                       &nfsfh) != 0) {
            fprintf(stderr, "Failed to creat file %s: %s\n",
                    url->file,
                    nfs_get_error(nfs));
            goto out;
        }
    }

    duration = ((double) (times(&dummy) - rtime) / (double) sysconf(_SC_CLK_TCK));
    if (rank == 0) {
        durations = (double *) malloc(sizeof(double) * size);
    }

    duration_avg = duration;

#ifdef HAVE_MPI
    MPI_Gather(&duration, 1, MPI_DOUBLE, durations, 1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
    if (rank == 0) {
        duration_avg = avg(durations, size);
    }
#endif // HAVE_MPI

    if (rank == 0) {
        fprintf(stdout, "Speed:  %2.2f rps in %2.2fs. Avg %2.2f rps per process.\n",
                (double) (files * size) / duration, duration, (double) files / duration_avg);
    }

    // cleanup ignoring errors
    for (i = 0; i < files; i++) {
        sprintf(filename, "%s/%s.file.%d.%d", url->file, hostname, pid, i);
        if (nfs_unlink(nfs, filename) != 0) {
            fprintf(stderr, "Failed to remove file %s: %s\n",
                    url->file,
                    nfs_get_error(nfs));
        }
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
