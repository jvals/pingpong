#!/usr/bin/bash
#SBATCH --partition=EPIC2
#SBATCH --time=00:30:00
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=24
#SBATCH --cpus-per-task=1
#SBATCH --gres=gpu:2
#SBATCH --job-name="pingpong"
#SBATCH --output=PingPong.%j.out


cd ${SLURM_SUBMIT_DIR}
module load intel
make
time srun --mpi=pmi2 ./pingpong
