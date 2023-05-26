# NFS BENCH

Simple benchmark for synthetic NFS load

## Dependecies

`nfs-bench` uses [libnfs][1] to perform NFS requests

## Building

```
cmake .. -DCMAKE_PREFIX_PATH=/usr/lib64/openmpi
```

## Use with SLURM

```
#!/bin/bash
#SBATCH --partition=allcpu,maxcpu
#SBATCH --nodes=8
unset LD_PRELOAD
source /etc/profile.d/modules.sh
module purge
module load maxwell openmpi/4.1.1
 
mpirun -mca pml ucx \
   -mca mpi_cuda_support 0 \
   -mca btl_tcp_if_include ib0 \
   --bind-to none \
   /usr/local/bin/nfs_bench -f 1000 nfs://nfs-lab/exports/data/
```

### Staring the job

```
sbatch 01-test.sh
```

### Checking job status and used resources

```
sacct  --format="partition,jobid,start,end,ElapsedRaw,NCPUS,state" -j 1473540

scontrol show job 1473540
```

## License

This work is licensed under GNU General Public License version 2

[1]: https://github.com/sahlberg/libnfs
