#!/usr/bin/bash
#SBATCH --partition=EPT
#SBATCH --time=00:30:00
#SBATCH --nodes=2
#SBATCH --ntasks-per-node=20
#SBATCH --cpus-per-task=1
#SBATCH --job-name="pingpong"
#SBATCH --output=PingPong.%j.out


cd ${SLURM_SUBMIT_DIR}
module load intel
make
time srun --mpi=pmi2 ./pingpong
