#include <sys/stat.h>
#include <string.h>

#ifdef HAVE_POLL_H
#include <poll.h>
#endif

#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/times.h>
#include <sys/socket.h>
#include <time.h>
#include <pthread.h>
#include <nfsc/libnfs.h>
#include <getopt.h>


#define DEFAULT_FILES 100

void usage(void) {
    exit(1);
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

    printf("Running %d iterations\n", files);
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
    printf("Speed:  %2.2f rps in %2.2fs\n",
           (double) files / duration, duration);

    out:
    if (nfs != NULL) {
        nfs_destroy_context(nfs);
    }
    if (url != NULL) {
        nfs_destroy_url(url);
    }
    return rc;
}