#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mpi.h>
#include <unistd.h>
#include <sched.h>
#include <sys/syscall.h>


#define _GNU_SOURCE

// 10000 tests of 1 byte transfers for latency estimate
#define TS_TESTS 10000

// 10 tests of 64 MB for bandwidth estimate
#define BETA_TESTS 100
#define MSG_SIZE 1024*1024*64

int size, rank;

// Hockney model: T(n) = Ts + n * beta_inv
// T(n) is estimated transfer time for message of n bytes
// Ts is link latency in seconds
// beta_inv is inverse/reciprocal bandwidth, unit [seconds/byte]
// n is message size, unit [bytes]
double Ts, beta_inv, start, end;
char *message;


/* Pingpong timing between odd and even ranks:
 * p0 calls with rank=0 peer=1, p1 calls with rank=1 peer=0,
 * p2 calls with rank=2 peer=3, p3 calls with rank=3 peer=2,
 * etc. etc.
 * n_tests is number of repetitions, msg_size is data transfer per rep.
 */
double
time_pingpong ( int source, int peer, int n_tests, int msg_size, MPI_Comm pair_comm )
{
    double start, end;
    MPI_Barrier ( pair_comm );
    start = MPI_Wtime();
    if ( source == rank )
    {
        for ( int i=0; i<n_tests; i++ )
            MPI_Ssend ( message, msg_size, MPI_CHAR, peer, 0, MPI_COMM_WORLD );
        for ( int i=0; i<n_tests; i++ )
            MPI_Recv ( message, msg_size, MPI_CHAR,
                peer, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE
            );
    }
    else if ( peer == rank )
    {
        for ( int i=0; i<n_tests; i++ )
            MPI_Recv ( message, msg_size, MPI_CHAR,
                source, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE
            );
        for ( int i=0; i<n_tests; i++ )
            MPI_Ssend ( message, msg_size, MPI_CHAR, source, 0, MPI_COMM_WORLD );
    }
    end = MPI_Wtime();
    return (end-start)/(2.0*n_tests*msg_size);
}

void all_to_all_pingpong() {
  // For alle i, i = [0 .. N-1]
  // for alle j, j = [i+1 .. N]
  // i kjører pingpong mellom i og j
  // lagre resultatet
  // barriere
  // samle alt til slutt

  MPI_Group world_group;
  MPI_Comm_group(MPI_COMM_WORLD, &world_group);

  double* timestamps = (double*)calloc(size*size, sizeof(double));
  for (int i = 0; i < size-1; ++i) {
    for (int j = i+1; j < size; ++j) {

      if (rank == i || rank == j) {
        // Create custom communicator for i,j pair
        MPI_Group pair_group;
        const int pair_ranks[2] = {i, j};
        MPI_Group_incl(world_group, 2, pair_ranks, &pair_group);
        MPI_Comm pair_comm;
        MPI_Comm_create_group((MPI_COMM_WORLD), pair_group, 0, &pair_comm);
        if (MPI_COMM_NULL == pair_comm) {
          fprintf(stderr, "pair_comm was null\n");
          exit(1);
        }

        if (rank == i) {
          timestamps[i*size+j] = time_pingpong(i, j, TS_TESTS, 1, pair_comm);
        } else if (rank == j) {
          timestamps[j*size+i] = time_pingpong(i, j, TS_TESTS, 1, pair_comm);
        }

        MPI_Group_free(&pair_group);
        MPI_Comm_free(&pair_comm);
      }
    }
  }

  if (rank == 0) {
    for (int r = 1; r < size; ++r) {
      MPI_Recv(&timestamps[r*size], size, MPI_DOUBLE, r, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    }
  } else {
    MPI_Send(&timestamps[rank*size], size, MPI_DOUBLE, 0, 0, MPI_COMM_WORLD);
  }

  if (rank == 0) {
    for (int i = 0; i < size; ++i) {
      for (int j = 0; j < size; ++j) {
        printf("%e ", timestamps[i*size+j]);
      }
      printf("\n");
    }
  }


  MPI_Group_free(&world_group);
  free(timestamps);
}


void all_print_hostname() {
  char hostname[256];
  gethostname(hostname, 256);
  if (rank == 0) {
    printf("%02d %s\n", rank, hostname);
    char remote_hostname[256];
    for (int r = 1; r < size; ++r) {
      MPI_Recv(remote_hostname, 256, MPI_CHAR, r, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
      printf("%02d %s\n", r, remote_hostname);
    }
  } else {
    MPI_Send(hostname, 256, MPI_CHAR, 0, 0, MPI_COMM_WORLD);
  }
}

void all_print_cpu() {
  unsigned cpu = sched_getcpu();
  // getcpu(&cpu[0], &cpu[1], NULL);
  if (rank == 0) {
    printf("%02d %02u\n", rank, cpu);
    unsigned remote_cpunode;
    for (int r = 1; r < size; ++r) {
      MPI_Recv(&remote_cpunode, 1, MPI_UNSIGNED, r, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
      printf("%02d %02u\n", r, remote_cpunode);
    }
  } else {
    MPI_Send(&cpu, 1, MPI_UNSIGNED, 0, 0, MPI_COMM_WORLD);
  }
}

void all_print_cpunode() {
  unsigned cpunode[2] = {0, 0};
  // getcpu(&cpunode[0], &cpunode[1], NULL);
  syscall(SYS_getcpu, &cpunode[0], &cpunode[1], NULL);
  if (rank == 0) {
    printf("RANK:%02d CPU:%02u NODE:%02u\n", rank, cpunode[0], cpunode[1]);
    unsigned remote_cpunode[2] = {0, 0};
    for (int r = 1; r < size; ++r) {
      MPI_Recv(&remote_cpunode, 2, MPI_UNSIGNED, r, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
      printf("RANK:%02d CPU:%02u NODE:%02u\n", r, remote_cpunode[0], remote_cpunode[1]);
    }
  } else {
    MPI_Send(&cpunode, 2, MPI_UNSIGNED, 0, 0, MPI_COMM_WORLD);
  }
}


int
main ( int argc, char **argv )
{
    MPI_Init ( &argc, &argv );
    MPI_Comm_size ( MPI_COMM_WORLD, &size );
    MPI_Comm_rank ( MPI_COMM_WORLD, &rank );

    all_print_hostname();
    all_print_cpunode();

    if ( (size&1) )
    {
        if ( rank == 0 )
            fprintf (stderr, "Please run with even # of processes\n" );
        MPI_Abort ( MPI_COMM_WORLD, EXIT_FAILURE );
    }

    // Find pairwise partners
    int peer = (rank&1) ? rank-1 : (rank+1);

    // Allocate some dummy data
    message = malloc ( MSG_SIZE );
    // Touch, to make it resident in memory
    memset ( message, 0, MSG_SIZE );

    // Measure latency:
    // time large number of small messages, messaging overhead cost
    // dominates total time requirement
    // Ts = time_pingpong ( rank, peer, TS_TESTS, 1 );
    // printf ( "(%02d <-> %02d) Ts =~ %e [s]\n", rank, peer, Ts );

    // Measure bandwidth:
    // time small number of large messages, data transfer cost
    // dominates total time requirement
    // beta_inv = time_pingpong ( rank, peer, BETA_TESTS, MSG_SIZE );
    // printf ( "(%02d <-> %02d) b^-1 =~ %e [s/byte]\n", rank, peer, beta_inv );
    all_to_all_pingpong();

    MPI_Finalize ();
    free ( message );
    exit ( EXIT_SUCCESS );
}
