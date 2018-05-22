#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mpi.h>

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
time_pingpong ( int rank, int peer, int n_tests, int msg_size )
{
    double start, end;
    MPI_Barrier ( MPI_COMM_WORLD );
    start = MPI_Wtime();
    if ( (rank&1) )
    {
        for ( int i=0; i<n_tests; i++ )
            MPI_Ssend ( message, msg_size, MPI_CHAR, peer, 0, MPI_COMM_WORLD );
        for ( int i=0; i<n_tests; i++ )
            MPI_Recv ( message, msg_size, MPI_CHAR,
                peer, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE
            );
    }
    else
    {
        for ( int i=0; i<n_tests; i++ )
            MPI_Recv ( message, msg_size, MPI_CHAR,
                peer, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE
            );
        for ( int i=0; i<n_tests; i++ )
            MPI_Ssend ( message, msg_size, MPI_CHAR, peer, 0, MPI_COMM_WORLD );
    }
    end = MPI_Wtime();
    return (end-start)/(2.0*n_tests*msg_size);
}


int
main ( int argc, char **argv )
{
    MPI_Init ( &argc, &argv );
    MPI_Comm_size ( MPI_COMM_WORLD, &size );
    MPI_Comm_rank ( MPI_COMM_WORLD, &rank );

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
    Ts = time_pingpong ( rank, peer, TS_TESTS, 1 );
    printf ( "(%d <-> %d) Ts =~ %e [s]\n", rank, peer, Ts );

    // Measure bandwidth:
    // time small number of large messages, data transfer cost
    // dominates total time requirement
    beta_inv = time_pingpong ( rank, peer, BETA_TESTS, MSG_SIZE );
    printf ( "(%d <-> %d) b^-1 =~ %e [s/byte]\n", rank, peer, beta_inv );

    MPI_Finalize ();
    free ( message );
    exit ( EXIT_SUCCESS );
}
