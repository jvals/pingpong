#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mpi.h>
#include <unistd.h>
#include <sched.h>
#include <sys/syscall.h>
#include <sched.h>
#include <time.h>

#define _GNU_SOURCE

// 10000 tests of 1 byte transfers for latency estimate
#define TS_TESTS 10000

// 10 tests of 64 MB for bandwidth estimate
#define BETA_TESTS 25
#define MSG_SIZE 1024*1024*32

int size, rank;

typedef struct CPUINFO {
  unsigned core;
  unsigned numa;
  int rank;
  int node;
} Cpuinfo;

Cpuinfo cpuinfo;
Cpuinfo* cpuinfos;


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

void write_data_int(const char* format, int data, const char* filename) {
  FILE *out = fopen(filename, "a");
  fprintf(out, format, data);
  fclose(out);
}

void write_data_double(const char* format, double data, const char* filename) {
  FILE *out = fopen(filename, "a");
  fprintf(out, format, data);
  fclose(out);
}

void write_data_newline(const char* filename) {
  FILE *out = fopen(filename, "a");
  fprintf(out, "\n");
  fclose(out);
}

void write_data_string(const char* data, const char* filename) {
  FILE *out = fopen(filename, "a");
  fprintf(out, data);
  fclose(out);
}


void all_to_all_pingpong(int n_tests, int msg_size, const char* filename) {
  // Create mpi group from comm_world
  MPI_Group world_group;
  MPI_Comm_group(MPI_COMM_WORLD, &world_group);

  // Run latency tests
  double* data = (double*)calloc(size*size, sizeof(double));
  for (int i = 0; i < size-1; ++i) {
    for (int j = i+1; j < size; ++j) {
      // Rank i and j are the candidates
      if (rank == i || rank == j) {
        // Create custom communicator for i,j pair
        MPI_Group pair_group;
        const int pair_ranks[2] = {i, j};
        MPI_Group_incl(world_group, 2, pair_ranks, &pair_group);
        MPI_Comm pair_comm;
        MPI_Comm_create_group((MPI_COMM_WORLD), pair_group, 0, &pair_comm);
        // Assert that the communicator is not null
        if (MPI_COMM_NULL == pair_comm) {
          fprintf(stderr, "pair_comm was null\n");
          MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
        }

        // i and j runs the benchmark
        if (rank == i) {
          data[i*size+j] = time_pingpong(i, j, n_tests, msg_size, pair_comm);
        } else if (rank == j) {
          data[j*size+i] = time_pingpong(i, j, n_tests, msg_size, pair_comm);
        }

        MPI_Group_free(&pair_group);
        MPI_Comm_free(&pair_comm);
      }
    }
  }

  // Collect data on rank 0
  if (rank == 0) {
    for (int r = 1; r < size; ++r) {
        MPI_Recv(&data[r*size], size, MPI_DOUBLE, r, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    }
  } else {
    MPI_Send(&data[rank*size], size, MPI_DOUBLE, 0, 0, MPI_COMM_WORLD);
  }

  // Print header row
  if (rank == 0) {
    // printf("xx;");
    write_data_string("xx;", filename);
    for (int r = 0; r < size; ++r) {
      // printf("%02d;", cpuinfos[r].rank);
      write_data_int("%02d;", cpuinfos[r].rank, filename);
    }
    write_data_newline(filename);
    // printf("\n");
  }

  // Print data. data sharing node is grouped together,
  // and within that group, data sharing sockets are grouped (optional)
  // together
  if (rank == 0) {
    for (int i = 0; i < size; ++i) {
      // First column
      // printf("%02d;", cpuinfos[i].rank);
      write_data_int("%02d;", cpuinfos[i].rank, filename);
      for (int j = 0; j < size; ++j) {
        // cpuinfo[i].rank is used for node/socket grouping.
        // printf("%e;", data[cpuinfos[i].rank*size+cpuinfos[j].rank]);
        write_data_double("%e;", data[cpuinfos[i].rank*size+cpuinfos[j].rank], filename);
      }
      write_data_newline(filename);
      // printf("\n");
    }
  }

  MPI_Group_free(&world_group);
  free(data);
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

  // Extract node number xx from hostname of the form 'compute-a{1,2}-b{1,2}-x{1,2}.'
  int dashes = 0;
  int t = 0;
  char match[2];
  for (int i = 0; i < 256; ++i) {
    if (hostname[i] == '-') {
      dashes++;
    } else if (dashes == 3 && hostname[i] >= 48 && hostname[i] <= 57) {
      // If we have three dashes and hostname[i] is a digit:
      match[t++] = hostname[i];
    }
    if (hostname[i] == '.') {
      break;
    }
  }

  cpuinfo.node = atoi(match);

}

void collect_CPU_info() {
  // syscall(SYS_getcpu, &cpuinfo.core, &cpuinfo.numa, NULL);
  cpuinfo.core = sched_getcpu();
  if (rank == 0) {
    cpuinfos[0] = cpuinfo;
    for (int r = 1; r < size; ++r) {
      MPI_Recv(&cpuinfos[r], sizeof(cpuinfo), MPI_BYTE, r, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    }
  } else {
    MPI_Send(&cpuinfo, sizeof(cpuinfo), MPI_BYTE, 0, 0, MPI_COMM_WORLD);
  }
}

void print_CPU_info() {
  for (int r = 0; r < size; ++r) {
    printf("RANK:%02d CPU:%02u NUMA:%02u NODE:%02d\n", cpuinfos[r].rank, cpuinfos[r].core, cpuinfos[r].numa, cpuinfos[r].node);
  }
}

void bubble_sort_cpuinfos(Cpuinfo* cpuinfos, int n) {
  // Sort numa
  // for (int i = 0; i < n-1; ++i) {
  //   for (int j = 0; j < n-i-1; ++j) {
  //     if (cpuinfos[j].numa > cpuinfos[j+1].numa) {
  //       Cpuinfo temp = cpuinfos[j];
  //       cpuinfos[j] = cpuinfos[j+1];
  //       cpuinfos[j+1] = temp;
  //     }
  //   }
  // }

  // Sort node
  for (int i = 0; i < n-1; ++i) {
    for (int j = 0; j < n-i-1; ++j) {
      if (cpuinfos[j].node > cpuinfos[j+1].node) {
        Cpuinfo temp = cpuinfos[j];
        cpuinfos[j] = cpuinfos[j+1];
        cpuinfos[j+1] = temp;
      }
    }
  }
}

int
main ( int argc, char **argv )
{
    MPI_Init ( &argc, &argv );
    MPI_Comm_size ( MPI_COMM_WORLD, &size );
    MPI_Comm_rank ( MPI_COMM_WORLD, &rank );

    cpuinfo.rank = rank;
    if (rank == 0) {
      cpuinfos = (Cpuinfo*)malloc(sizeof(cpuinfo)*size);
    }

    all_print_hostname();
    collect_CPU_info();

    if (rank == 0) print_CPU_info();

    // if (rank == 0) {
    //   // Sort the cpuinfos array by socket and then by node.
    //   bubble_sort_cpuinfos(cpuinfos, size);
    //   printf("\n");
    //   for (int r = 0; r < size; ++r) {
    //     printf("RANK:%02d CPU:%02u NUMA:%02u NODE:%02d\n", cpuinfos[r].rank, cpuinfos[r].core, cpuinfos[r].numa, cpuinfos[r].node);
    //   }
    // }

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

    char timetext[100];
    time_t now = time(NULL);
    struct tm* t = localtime(&now);
    strftime(timetext, sizeof(timetext), "%Y%m%d_%H%M", t);

    char filename[256];
    sprintf(filename, "%s_latency", timetext);

    all_to_all_pingpong(TS_TESTS, 1, filename);
    MPI_Barrier(MPI_COMM_WORLD);
    if (rank == 0) printf("========================================\n");

    sprintf(filename, "%s_betainv", timetext);
    all_to_all_pingpong(BETA_TESTS, MSG_SIZE, filename);

    if (rank == 0) {
      free(cpuinfos);
    }

    MPI_Finalize ();
    free ( message );
    exit ( EXIT_SUCCESS );
}
