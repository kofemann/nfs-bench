# NFS BENCH

Simple benchmark for synthetic NFS load

## Dependecies

`nfs-bench` use libnfs to perform NFS requests

## Use with SLURM

```
#!/bin/bash
#SBATCH --partition=allcpu,maxcpu
#SBATCH --nodes=8
unset LD_PRELOAD
source /etc/profile.d/modules.sh
module purge
module load mpi/openmpi-x86_64
 
nprocs=$(( $(nproc) ))
 
# -N ensure $nprocs processes per node
mpirun -N $nprocs /usr/local/bin/nfs_bench -f 1000 nfs://lab-host/exports/data/
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
